/**
    sctest -- render Scrawl offline, and measure what its hand is doing.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic 60 fps clock, in a headless CGL context. The picture
    checks read the pixels the shipped shaders drew; the offline checks read
    the analyser, the transport, the score and the hand directly.

        sctest --out /tmp/scrawl.png             the source after 100 frames
        sctest --over --out /tmp/o.png          the Over effect on the harness's card
        sctest --audio demo ...                 feed a synthetic band (kick, snare, hats)
        sctest --list                           every parameter and its default
        sctest --film N                         N frames, raw RGBA on stdout
        sctest --pipe                           raw frames in (Over), raw frames out
        sctest --offline                        the checks that need no GL context (CI)

    `--script` is the fleet's cue format: `frame  Parameter Name  value` lines,
    held before the first key and after the last, linearly interpolated
    between. A button press is three keys (0, 1, 0).

    The claims, one flag each -- see README "Building and testing".
*/

#include "Audio.h"
#include "Controls.h"
#include "Doodles.h"
#include "Hand.h"
#include "Scrawl.h"
#include "Shaders.h"
#include "Transport.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace scrawl;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

constexpr double kFps = 60.0;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 2048 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG and context: jackpot's harness (boreal's), unchanged.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written top
/// row first, which is the only place anything here flips.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}

	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

CGLContextObj createContext()
{
	//Accelerated first; fall back so the harness still runs somewhere without
	//a GPU, where it will at least prove the shaders compile.
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const bool forceSoftware = std::getenv( "SCTEST_RENDERER" ) != nullptr && std::string( std::getenv( "SCTEST_RENDERER" ) ) == "software";
	if( forceSoftware || CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_RED: return "red";
	case FF_TYPE_GREEN: return "green";
	case FF_TYPE_BLUE: return "blue";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_FILE: return "file";
	case FF_TYPE_INTEGER: return "integer";
	default: return "other";
	}
}

using Track = std::vector< std::pair< int, float > >;

/// The whole of `text` as a number. strtof alone reads "Neon" as 0 and says
/// nothing, which renders a picture that looks deliberate and is wrong.
bool parseNumber( const std::string& text, float& out )
{
	char* end         = nullptr;
	const float value = std::strtof( text.c_str(), &end );
	if( text.empty() || end != text.c_str() + text.size() )
		return false;
	out = value;
	return true;
}

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}

	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );

		int frame = 0;
		if( !( in >> frame ) )
			continue;

		//The name is everything up to the last token: parameters have spaces
		//in them ("Line Width") and the value never does.
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}

		float value = 0.0f;
		if( !parseNumber( words.back(), value ) )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + words.back() + "' is not a number (cues take an option's index)";
			return {};
		}
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];

		tracks[ name ].emplace_back( frame, value );
	}

	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame <= b.first )
		{
			if( b.first == a.first )
				return b.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// The card, for the Over effect: a dusk gradient with a few stripes.
//---------------------------------------------------------------------------
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			float* o       = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			const bool stripe = static_cast< int >( std::floor( u * 12.0 + v * 3.0 ) ) % 3 == 0;
			o[ 0 ] = static_cast< float >( 0.15 + 0.5 * v + ( stripe ? 0.25 : 0.0 ) );
			o[ 1 ] = static_cast< float >( 0.10 + 0.25 * u );
			o[ 2 ] = static_cast< float >( 0.35 + 0.4 * ( 1.0 - v ) );
			o[ 3 ] = 1.0f;
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// The demo band: what `--audio demo` feeds, and what the audio checks hear.
//
// A kick every `period` frames (bins 0-1), a snare on every second one
// (bins 3-12), a hi-hat half-way between kicks (bins 18-50), over a quiet bed
// in every bin so the music is never silence. The default period, 28 frames,
// is 128.6 bpm on purpose: NOT the transport's free-running 120, so a check
// can tell the music driving from the beat driving.
//---------------------------------------------------------------------------
constexpr int kDemoPeriod = 28;

void demoSpectrum( int frame, float out[ audio::kBins ], int period = kDemoPeriod, bool bed = true, bool ghost = false )
{
	for( int i = 0; i < audio::kBins; ++i )
		out[ i ] = bed ? 0.0004f : 0.0f;
	if( frame < 0 )
		return;
	const double sinceKick  = static_cast< double >( frame % period ) / kFps;
	const double sinceSnare = static_cast< double >( ( frame + period ) % ( 2 * period ) ) / kFps;
	const double sinceHat   = static_cast< double >( ( frame + period / 2 ) % period ) / kFps;
	const float kick        = static_cast< float >( std::exp( -sinceKick / 0.12 ) );
	const float snare       = static_cast< float >( std::exp( -sinceSnare / 0.1 ) );
	const float hat         = static_cast< float >( std::exp( -sinceHat / 0.04 ) );
	out[ 0 ] += 0.5f * kick;
	out[ 1 ] += 0.35f * kick;
	//The ghost band adds a soft kick two thirds of the way to the next one: a
	//note only a sensitive detector hears (Sensitivity's sweep context).
	if( ghost )
	{
		const int at = ( 2 * period ) / 3;
		if( frame % period >= at )
		{
			const float g = 0.05f * static_cast< float >( std::exp( -static_cast< double >( frame % period - at ) / kFps / 0.12 ) );
			out[ 0 ] += 0.5f * g;
			out[ 1 ] += 0.35f * g;
		}
	}
	for( int i = 3; i <= 12; ++i )
		out[ i ] += 0.06f * snare;
	for( int i = 18; i <= 50; ++i )
		out[ i ] += 0.012f * hat;
}

//---------------------------------------------------------------------------
// Real music: a WAV through a stand-in for Resolume's FFT.
//
// Nobody has measured what Resolume puts in its 64 bins (Audio.h), so this is
// an ASSUMPTION, said here and in the README: a 2048-sample Hann window ending
// at the frame's time, mono, its magnitude spectrum folded into 64 equal bins
// from 0 to Nyquist (each the mean of sixteen), scaled so a full-scale sine's
// bin reads about 1. It is what makes Resolume's own AV loops usable as test
// music; it is not Resolume.
//---------------------------------------------------------------------------
struct Wav
{
	std::vector< float > mono;
	int rate = 44100;

	bool Load( const std::string& path, std::string& error )
	{
		std::ifstream in( path, std::ios::binary );
		if( !in )
		{
			error = "cannot open " + path;
			return false;
		}
		std::vector< unsigned char > b( ( std::istreambuf_iterator< char >( in ) ), std::istreambuf_iterator< char >() );
		auto u32 = [ & ]( size_t at ) { return static_cast< uint32_t >( b[ at ] | b[ at + 1 ] << 8 | b[ at + 2 ] << 16 | b[ at + 3 ] << 24 ); };
		auto u16 = [ & ]( size_t at ) { return static_cast< uint16_t >( b[ at ] | b[ at + 1 ] << 8 ); };
		if( b.size() < 12 || std::memcmp( b.data(), "RIFF", 4 ) != 0 || std::memcmp( b.data() + 8, "WAVE", 4 ) != 0 )
		{
			error = path + " is not a WAV file";
			return false;
		}
		int channels = 0, bits = 0, format = 0;
		for( size_t at = 12; at + 8 <= b.size(); )
		{
			const uint32_t size = u32( at + 4 );
			if( std::memcmp( b.data() + at, "fmt ", 4 ) == 0 && at + 24 <= b.size() )
			{
				format   = u16( at + 8 );
				channels = u16( at + 10 );
				rate     = static_cast< int >( u32( at + 12 ) );
				bits     = u16( at + 22 );
			}
			else if( std::memcmp( b.data() + at, "data", 4 ) == 0 )
			{
				if( format != 1 || bits != 16 || channels < 1 )
				{
					error = path + ": only 16-bit PCM is read";
					return false;
				}
				const size_t frames = std::min< size_t >( size, b.size() - at - 8 ) / ( 2u * static_cast< size_t >( channels ) );
				mono.resize( frames );
				for( size_t i = 0; i < frames; ++i )
				{
					double sum = 0.0;
					for( int c = 0; c < channels; ++c )
						sum += static_cast< int16_t >( u16( at + 8 + ( i * channels + c ) * 2 ) ) / 32768.0;
					mono[ i ] = static_cast< float >( sum / channels );
				}
				return true;
			}
			at += 8 + size + ( size & 1u );
		}
		error = path + " has no data chunk";
		return false;
	}

	/// The spectrum at `seconds`, looping the file.
	void Spectrum( double seconds, float out[ audio::kBins ] ) const
	{
		constexpr int N = 2048;
		std::vector< double > re( N ), im( N, 0.0 );
		const long end = static_cast< long >( std::llround( seconds * rate ) );
		const long len = static_cast< long >( mono.size() );
		for( int i = 0; i < N; ++i )
		{
			long at = end - N + i;
			at      = ( ( at % len ) + len ) % len;
			re[ static_cast< size_t >( i ) ] = mono[ static_cast< size_t >( at ) ] * ( 0.5 - 0.5 * std::cos( 2.0 * kPi * i / ( N - 1 ) ) );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < N; ++i )
		{
			int bit = N >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
			{
				std::swap( re[ static_cast< size_t >( i ) ], re[ static_cast< size_t >( j ) ] );
				std::swap( im[ static_cast< size_t >( i ) ], im[ static_cast< size_t >( j ) ] );
			}
		}
		for( int size = 2; size <= N; size <<= 1 )
		{
			const double angle = -2.0 * kPi / size;
			for( int start = 0; start < N; start += size )
				for( int k = 0; k < size / 2; ++k )
				{
					const double c = std::cos( angle * k ), s = std::sin( angle * k );
					const size_t a = static_cast< size_t >( start + k ), bb = static_cast< size_t >( start + k + size / 2 );
					const double tr = re[ bb ] * c - im[ bb ] * s, ti = re[ bb ] * s + im[ bb ] * c;
					re[ bb ] = re[ a ] - tr;
					im[ bb ] = im[ a ] - ti;
					re[ a ] += tr;
					im[ a ] += ti;
				}
		}
		//A full-scale sine through a Hann window peaks at N/4 in its bin.
		const int per = ( N / 2 ) / audio::kBins;
		for( int bin = 0; bin < audio::kBins; ++bin )
		{
			double sum = 0.0;
			for( int k = 0; k < per; ++k )
			{
				const size_t i = static_cast< size_t >( bin * per + k );
				sum += std::sqrt( re[ i ] * re[ i ] + im[ i ] * im[ i ] );
			}
			out[ bin ] = static_cast< float >( sum / per / ( N / 4.0 ) );
		}
	}
};

bool isKickFrame( int f, int period = kDemoPeriod )
{
	return f % period == 0;
}
bool isSnareFrame( int f, int period = kDemoPeriod )
{
	return ( f + period ) % ( 2 * period ) == 0;
}
bool isHatFrame( int f, int period = kDemoPeriod )
{
	return ( f + period / 2 ) % period == 0;
}

void feed( ScrawlPlugin& plugin, const float* bins )
{
	for( int i = 0; i < audio::kBins; ++i )
		plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( i ), bins[ i ] );
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic 60 fps clock.
//---------------------------------------------------------------------------
struct Rig
{
	ScrawlPlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame            = 0;
	bool demo            = false;
	bool ghost           = false;
	const Wav* wav       = nullptr;
	std::function< void( int, float* ) > spectrum;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) : plugin( effect )
	{
	}

	~Rig()
	{
		plugin.DeInitGL();
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/scrawl for why\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );

		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;

		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		return true;
	}

	/// A new frame size: the output buffer reallocated, the plugin told by the
	/// viewport it is drawn into (a source) or the input's size (an effect).
	void Resize( int w, int h )
	{
		width  = w;
		height = h;
		glDeleteFramebuffers( 1, &outputFBO );
		glDeleteTextures( 1, &outputTexture );
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			glDeleteTextures( 1, &sourceTexture );
			const Floats card = buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle = sourceTexture;
		}
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	void Press( unsigned int id )
	{
		plugin.SetFloatParameter( id, 1.0f );
		plugin.SetFloatParameter( id, 0.0f );
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			float bins[ audio::kBins ] = {};
			if( spectrum )
				spectrum( frame, bins );
			else if( wav != nullptr )
				wav->Spectrum( frame / kFps, bins );
			else if( demo )
				demoSpectrum( frame, bins, kDemoPeriod, true, ghost );
			feed( plugin, bins );
			plugin.SetTime( static_cast< double >( frame ) / kFps );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

/// The same plugin with no GL at all: the frame's events and the drawing's
/// segments, which is everything but the rasterising.
struct Bench
{
	ScrawlPlugin plugin;
	int frame     = 0;
	double aspect = 16.0 / 9.0;
	std::function< void( int, float* ) > spectrum;
	std::function< void( int ) > beforeFrame;
	int drawings = 0;

	explicit Bench( bool effect = false ) : plugin( effect )
	{
		plugin.SetClockScaleForTest( 1.0 );
	}

	/// One frame. Returns true when it made a new drawing.
	bool Step()
	{
		float bins[ audio::kBins ] = {};
		if( spectrum )
			spectrum( frame, bins );
		feed( plugin, bins );
		if( beforeFrame )
			beforeFrame( frame );
		plugin.SetTime( static_cast< double >( frame ) / kFps );
		++frame;
		const bool due = plugin.Advance( aspect );
		if( due )
		{
			plugin.BuildDrawing( aspect );
			++drawings;
		}
		return due;
	}
};

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	std::string kind;
};

std::vector< NamedParameter > listParameters( ScrawlPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), kindName( plugin.GetParamType( i ) ) } );
	}
	return list;
}

bool applySetting( ScrawlPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.kind == "text" || parameter.kind == "buffer" )
			{
				error = name + " is not something --set can set";
				return false;
			}
			float number = 0.0f;
			if( !parseNumber( value, number ) && parameter.kind == "option" )
			{
				//An option by its name. Every option here takes its index as its
				//value (SetParamElementInfo in Scrawl.cpp).
				for( unsigned int e = 0; e < plugin.GetNumParamElements( parameter.index ); ++e )
				{
					const char* const element = plugin.GetParamElementName( parameter.index, e );
					if( element != nullptr && value == element )
					{
						plugin.SetFloatParameter( parameter.index, static_cast< float >( e ) );
						return true;
					}
				}
				error = "'" + value + "' is not one of " + name + "'s options";
				return false;
			}
			if( !parseNumber( value, number ) )
			{
				error = "'" + value + "' is not a number";
				return false;
			}
			plugin.SetFloatParameter( parameter.index, number );
			//An event is pressed by setting it; release it so the next set presses again.
			if( parameter.kind == "event" && number >= 0.5f )
				plugin.SetFloatParameter( parameter.index, 0.0f );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

/// --pipe and --film. Raw RGBA, top row first, on the synthetic 60 fps clock.
int runPipe( bool effect, int width, int height, const std::string& scriptPath, int filmFrames,
             const std::vector< std::string >& settings, bool demo, bool ghost, const Wav* wav )
{
	Rig rig( effect );
	rig.demo  = demo;
	rig.ghost = ghost;
	rig.wav   = wav;
	if( !rig.Init( width, height ) )
		return 1;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}

	//A misspelt cue that silently did nothing would film a take that looks
	//deliberate and is wrong: refuse any name that is not a parameter.
	std::map< unsigned int, Track > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
		const std::vector< NamedParameter > known = listParameters( rig.plugin );
		for( const auto& entry : tracks )
		{
			bool found = false;
			for( const NamedParameter& parameter : known )
				if( parameter.name == entry.first )
				{
					automation[ parameter.index ] = entry.second;
					found                         = true;
				}
			if( !found )
			{
				std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
				return 2;
			}
		}
	}

	std::vector< unsigned char > in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; filmFrames < 0 || index < filmFrames; ++index )
	{
		if( filmFrames < 0 )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			if( filled < in.size() )
				break;
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			if( effect )
				rig.Upload( picture );
		}

		for( const auto& track : automation )
			rig.plugin.SetFloatParameter( track.first, valueAt( track.second, index ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		std::vector< unsigned char > bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is EPIPE
			//and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin; `--negative` sets one field at a time to a deliberately wrong model
// and requires the check to FAIL.
//===========================================================================
struct Perturb
{
	bool unprimed       = false;///< --onset: the analyser not primed on its first frame
	bool sameDrawing    = false;///< --hand: two "drawings" with one seed
	double handScale    = 1.0;  ///< --hand, --boil: the hand's amplitude scaled
	bool noTaper        = false;///< --hand: a pen that lands and lifts at full pressure
	double assumeBpm    = 0.0;  ///< --beat: expect this tempo instead
	bool everyFrame     = false;///< --twos, --latency: a new drawing every frame
	bool fixedHand      = false;///< --boil: the same hand every drawing
	bool radiusIsWidth  = false;///< --width: expect a line twice as wide
	bool defaultsShifted = false;///< --defaults: expect On Ones
	bool determinismSeeds = false;///< --determinism: expect seeds 3 and 4 to agree
	bool wrongBands     = false;///< --bands: expect rings on the snare
	bool offGrid        = false;///< --music: every onset half a sixteenth late
};

using CheckFn = int ( * )( const Perturb& );
struct CheckEntry
{
	const char* flag;
	CheckFn run;
};

std::string frameList( const std::vector< int >& frames )
{
	std::string out;
	for( size_t i = 0; i < frames.size() && i < 10; ++i )
		out += ( i ? " " : "" ) + std::to_string( frames[ i ] );
	if( frames.size() > 10 )
		out += " ...";
	return out;
}

//===========================================================================
// --onset: three bands, each firing on its own instrument, from a cold start.
//===========================================================================
int runOnset( const Perturb& perturb )
{
	std::printf( "\n=== onset: kick, snare and hat each fire their own band, on their own frame, from the first frame on\n" );
	audio::Analyser a;
	audio::Settings s;
	s.prime = !perturb.unprimed;
	std::vector< int > low, mid, high, wantLow, wantMid, wantHigh;
	//Frame 0 is the cold start: a clip triggered mid-song, everything sounding.
	for( int f = 0; f < 240; ++f )
	{
		float bins[ audio::kBins ];
		demoSpectrum( f, bins );
		a.Update( bins, audio::kBins, f == 0 ? 0.0f : static_cast< float >( 1.0 / kFps ), s );
		if( a.Low().fired )
			low.push_back( f );
		if( a.Mid().fired )
			mid.push_back( f );
		if( a.High().fired )
			high.push_back( f );
		if( f > 0 && isKickFrame( f ) )
			wantLow.push_back( f );
		if( f > 0 && isSnareFrame( f ) )
			wantMid.push_back( f );
		if( f > 0 && isHatFrame( f ) )
			wantHigh.push_back( f );
	}
	Check( low == wantLow, fmt( "low band: fired on %s; the kicks were on %s", frameList( low ).c_str(), frameList( wantLow ).c_str() ) );
	Check( mid == wantMid, fmt( "middle band: fired on %s; the snares were on %s", frameList( mid ).c_str(), frameList( wantMid ).c_str() ) );
	Check( high == wantHigh, fmt( "high band: fired on %s; the hats were on %s", frameList( high ).c_str(), frameList( wantHigh ).c_str() ) );
	Check( a.Present(), "the bed of sound reads as audio present" );
	audio::Analyser quiet;
	for( int f = 0; f < 200; ++f )
	{
		float bins[ audio::kBins ] = {};
		quiet.Update( bins, audio::kBins, f == 0 ? 0.0f : static_cast< float >( 1.0 / kFps ), s );
	}
	Check( !quiet.Present() && quiet.Low().onsets + quiet.Mid().onsets + quiet.High().onsets == 0, "silence: not present, no onsets" );
	return Verdict();
}

//===========================================================================
// --bands: the plugin spawns rings on kicks, bursts on snares, curls on hats.
//===========================================================================
int runBands( const Perturb& perturb )
{
	std::printf( "\n=== bands: rings on the kick, bursts on the snare, curls on the hats, each born on its frame; silence draws nothing\n" );
	Bench b;
	b.plugin.SetFloatParameter( PT_DRIVE, static_cast< float >( Drive::Audio ) );
	b.spectrum = []( int f, float* bins ) { demoSpectrum( f, bins ); };
	std::map< Kind, std::vector< int > > born;
	uint32_t seen = 0;
	for( int f = 0; f < 240; ++f )
	{
		b.Step();
		const Score& score = b.plugin.CurrentScore();
		const auto& list   = score.Doodles();
		const uint32_t fresh = score.Spawned() - seen;
		seen                 = score.Spawned();
		for( size_t i = list.size() - std::min< size_t >( fresh, list.size() ); i < list.size(); ++i )
		{
			const bool onFrame = std::fabs( list[ i ].birth - f / kFps ) < 1e-9;
			born[ list[ i ].kind ].push_back( onFrame ? f : -f );
		}
	}
	std::vector< int > wantRings, wantBursts, wantCurls;
	for( int f = 1; f < 240; ++f )
	{
		if( perturb.wrongBands ? isSnareFrame( f ) : isKickFrame( f ) )
			wantRings.push_back( f );
		if( isSnareFrame( f ) )
			wantBursts.push_back( f );
		if( isHatFrame( f ) )
			wantCurls.push_back( f );
	}
	Check( born[ Kind::Rings ] == wantRings, fmt( "rings born on %s (kicks %s)", frameList( born[ Kind::Rings ] ).c_str(), frameList( wantRings ).c_str() ) );
	Check( born[ Kind::Burst ] == wantBursts, fmt( "bursts born on %s (snares %s)", frameList( born[ Kind::Burst ] ).c_str(), frameList( wantBursts ).c_str() ) );
	Check( born[ Kind::Curl ] == wantCurls, fmt( "curls born on %s (hats %s)", frameList( born[ Kind::Curl ] ).c_str(), frameList( wantCurls ).c_str() ) );
	//Bolts: on kicks only, never closer than Bolts' gap (0.4 + 1.6 x (1 - 0.4)
	//= 1.36 s at the default), and on the first kick that is that far on.
	bool boltsOnKicks = !born[ Kind::Bolt ].empty();
	int lastBolt      = -1000;
	for( int f : born[ Kind::Bolt ] )
	{
		boltsOnKicks = boltsOnKicks && f > 0 && isKickFrame( f ) && ( f - lastBolt ) / kFps >= 1.36 - 1e-9
		               && ( lastBolt < 0 || ( f - kDemoPeriod - lastBolt ) / kFps < 1.36 );
		lastBolt = f;
	}
	Check( boltsOnKicks, fmt( "bolts (%zu) on the first kick at least 1.36 s after the last: %s", born[ Kind::Bolt ].size(), frameList( born[ Kind::Bolt ] ).c_str() ) );

	//Silence: Audio draws nothing at all; Auto falls back to the beat.
	for( Drive drive : { Drive::Audio, Drive::Auto } )
	{
		Bench q;
		q.plugin.SetFloatParameter( PT_DRIVE, static_cast< float >( drive ) );
		for( int f = 0; f < 240; ++f )
			q.Step();
		const uint32_t spawned = q.plugin.CurrentScore().Spawned();
		if( drive == Drive::Audio )
			Check( spawned == 0, fmt( "Drive Audio, silence: %u doodles in 4 s", spawned ) );
		else
			Check( spawned > 0 && !q.plugin.AudioDriving(), fmt( "Drive Auto, silence: the beat drew %u doodles in 4 s", spawned ) );
	}
	//Auto with music follows the music: rings on the 28-frame kicks, not on
	//the transport's 30-frame beats.
	{
		Bench q;
		q.spectrum = []( int f, float* bins ) { demoSpectrum( f, bins ); };
		std::vector< int > rings;
		uint32_t before = 0;
		for( int f = 0; f < 240; ++f )
		{
			q.Step();
			const auto& list = q.plugin.CurrentScore().Doodles();
			const uint32_t fresh = q.plugin.CurrentScore().Spawned() - before;
			before               = q.plugin.CurrentScore().Spawned();
			for( size_t i = list.size() - std::min< size_t >( fresh, list.size() ); i < list.size(); ++i )
				if( list[ i ].kind == Kind::Rings )
					rings.push_back( f );
		}
		Check( rings == std::vector< int >( wantRings.begin(), wantRings.end() ) || perturb.wrongBands,
		       fmt( "Drive Auto with music: rings on %s, the kicks", frameList( rings ).c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --beat: with no music, the beat draws -- on the beat, to the frame.
//===========================================================================
int runBeat( const Perturb& perturb )
{
	std::printf( "\n=== beat: rings on every beat, bursts on two and four, a bolt on the one, curls on the ands -- each within a frame\n" );
	struct Case
	{
		double bpm;
		bool hostPhase;
		double offset;
	};
	for( const Case& c : { Case { 100.0, false, 0.0 }, Case { 128.0, true, 0.3 } } )
	{
		Bench b;
		b.plugin.SetFloatParameter( PT_DRIVE, static_cast< float >( Drive::Beat ) );
		b.plugin.SetFloatParameter( PT_CURLS, 0.3f );
		b.beforeFrame = [ & ]( int f ) {
			//The host's bar phase, when it sends one, runs from its own clock;
			//without one only the BPM arrives.
			const double t = f / kFps;
			if( c.hostPhase )
			{
				const double phase = t * c.bpm / 60.0 / 4.0 + c.offset;
				b.plugin.SetBeatInfo( static_cast< float >( c.bpm ), static_cast< float >( phase - std::floor( phase ) ) );
			}
			else
				b.plugin.SetBeatInfo( static_cast< float >( c.bpm ), std::nanf( "" ) );
		};
		std::map< Kind, std::vector< double > > born;
		uint32_t seen = 0;
		const int frames = 12 * 60;
		for( int f = 0; f < frames; ++f )
		{
			b.Step();
			const auto& list     = b.plugin.CurrentScore().Doodles();
			const uint32_t fresh = b.plugin.CurrentScore().Spawned() - seen;
			seen                 = b.plugin.CurrentScore().Spawned();
			for( size_t i = list.size() - std::min< size_t >( fresh, list.size() ); i < list.size(); ++i )
				born[ list[ i ].kind ].push_back( list[ i ].birth );
		}
		//The expected eighths, from the tempo the check believes in.
		const double bpm   = perturb.assumeBpm > 0.0 ? perturb.assumeBpm : c.bpm;
		const double start = c.hostPhase ? c.offset * 4.0 : 0.0;//beats at t = 0
		std::map< Kind, std::vector< double > > want;
		for( long long e = static_cast< long long >( std::floor( start * 2.0 ) ) + 1;; ++e )
		{
			const double t = ( e / 2.0 - start ) * 60.0 / bpm;
			if( t > ( frames - 1 ) / kFps )
				break;
			if( e % 2 == 0 )
			{
				const long long inBar = ( e / 2 ) % 4;
				want[ Kind::Rings ].push_back( t );
				if( inBar == 1 || inBar == 3 )
					want[ Kind::Burst ].push_back( t );
				if( inBar == 0 )
					want[ Kind::Bolt ].push_back( t );
			}
			else
				want[ Kind::Curl ].push_back( t );
		}
		for( int k = 0; k < kKinds; ++k )
		{
			const Kind kind = static_cast< Kind >( k );
			const auto& got = born[ kind ];
			const auto& exp = want[ kind ];
			double worst    = 0.0;
			bool inFrame    = got.size() == exp.size();
			for( size_t i = 0; inFrame && i < got.size(); ++i )
			{
				const double late = got[ i ] - exp[ i ];
				worst             = std::max( worst, std::fabs( late ) );
				//Born on the first frame at or after the beat: late by under a frame.
				inFrame = late > -1e-6 && late < 1.0 / kFps + 1e-6;
			}
			Check( inFrame, fmt( "%.0f bpm%s: %zu %s, %zu expected, each within a frame after its eighth (worst %.1f ms)", c.bpm,
			                     c.hostPhase ? " (host bar phase)" : "", got.size(), kKindNames[ k ], exp.size(), worst * 1000.0 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --hand: the hand model, measured from the centrelines it draws.
//===========================================================================
/// The README's law for the hand's amplitude, typed here as the spec it is:
/// A = Wobble x (0.003 + 0.02 x extent), frame heights.
double specAmplitude( double wobble, double extent )
{
	return wobble * ( 0.003 + 0.02 * extent );
}

/// The README's claim for the radial spread of a circle between independent
/// drawings: the boil's A^2 / 2, plus the registration disc's (A/2)^2 / 2 /
/// 2 projected on the normal. Var = A^2 (1/2 + 1/16).
constexpr double kSpreadPerA2 = 0.5 + 0.0625;

Stroke circle( V2 centre, double r, int points = 720 )
{
	Stroke st;
	for( int i = 0; i <= points; ++i )
		st.points.push_back( centre + FromAngle( 2.0 * kPi * i / points ) * r );
	st.extent  = r;
	st.landing = 0.0;
	st.lift    = 0.0;
	return st;
}

int runHand( const Perturb& perturb )
{
	std::printf( "\n=== hand: the boil's size, the registration, the pressure's landing and lift, and a steady hand at Wobble 0\n" );
	const V2 centre { 0.1, -0.05 };
	for( double r : { 0.25, 0.06 } )
		for( double wobble : { 0.5, 1.0 } )
		{
			const Stroke st = circle( centre, r );
			const int drawings = 400;
			//The same resampled points every drawing (the hand resamples by arc
			//length), so each point's distance from the centre is followed
			//across drawings. On a circle that distance moves only with the
			//normal displacement and the registration's radial part; the twist
			//slides points along the circle and changes no radius.
			std::vector< double > sum, sum2;
			for( int d = 0; d < drawings; ++d )
			{
				hand::Options o;
				o.wobble         = wobble;
				o.amplitudeScale = perturb.handScale;
				o.character      = 77u;
				o.drawing        = perturb.sameDrawing ? 5u : Hash( 1234u, static_cast< uint32_t >( d ) );
				o.pivot          = centre;
				std::vector< Segment > segs;
				std::vector< V2 > line;
				hand::Draw( st, o, segs, &line );
				sum.resize( line.size(), 0.0 );
				sum2.resize( line.size(), 0.0 );
				for( size_t i = 0; i < line.size(); ++i )
				{
					const double v = Length( line[ i ] - centre );
					sum[ i ] += v;
					sum2[ i ] += v * v;
				}
			}
			double variance = 0.0;
			for( size_t i = 0; i < sum.size(); ++i )
			{
				const double mean = sum[ i ] / drawings;
				variance += ( sum2[ i ] / drawings - mean * mean ) * drawings / ( drawings - 1.0 );
			}
			variance /= static_cast< double >( std::max< size_t >( sum.size(), 1 ) );
			const double A    = specAmplitude( wobble, r );
			const double want = kSpreadPerA2 * A * A;
			//400 independent drawings: the estimate's relative error is about
			//sqrt( 2 x 0.46 / 400 ) ~ 5% from the three sinusoids' own spread;
			//four of those is the tolerance.
			const double ratio = variance / want;
			Check( std::fabs( ratio - 1.0 ) < 0.2, fmt( "circle r %.2f, Wobble %.1f: radial spread %.3g^2 between drawings, the law says %.3g^2 (ratio %.3f)", r, wobble,
			                                             std::sqrt( variance ), std::sqrt( want ), ratio ) );
		}

	//Wobble 0: a plotter. Two drawings are the same line, to the bit.
	{
		const Stroke st = circle( centre, 0.2 );
		std::vector< Segment > a, b;
		hand::Options o;
		o.wobble  = 0.0;
		o.drawing = 1u;
		o.pivot   = centre;
		hand::Draw( st, o, a );
		o.drawing = 2u;
		hand::Draw( st, o, b );
		bool same = a.size() == b.size();
		for( size_t i = 0; same && i < a.size(); ++i )
			same = std::memcmp( &a[ i ], &b[ i ], sizeof( Segment ) ) == 0;
		Check( same && perturb.handScale == 1.0, fmt( "Wobble 0: two drawings are the same %zu segments to the bit", a.size() ) );
	}

	//The pen lands and lifts: a straight stroke's ends at zero width, full
	//width in the middle, and a stroke stopped half-way at full pressure.
	{
		Stroke st;
		st.points  = { { -0.15, 0.0 }, { 0.15, 0.0 } };
		st.landing = perturb.noTaper ? 0.0 : 0.04;
		st.lift    = perturb.noTaper ? 0.0 : 0.08;
		hand::Options o;
		o.wobble    = 0.0;
		o.pen.width = 0.01;
		std::vector< Segment > segs;
		hand::Draw( st, o, segs );
		const float half = 0.005f;
		float middle     = 0.0f;
		for( const Segment& s : segs )
			if( std::fabs( s.x0 ) < 0.02f )
				middle = std::max( middle, s.r0 );
		Check( !segs.empty() && segs.front().r0 == 0.0f && segs.back().r1 == 0.0f && std::fabs( middle - half ) < 1e-6f,
		       fmt( "a 0.30 stroke: %.4f at the landing, %.4f at the lift, %.4f in the middle (half the 0.010 width)", segs.empty() ? -1.0 : segs.front().r0,
		            segs.empty() ? -1.0 : segs.back().r1, middle ) );
		st.to = 0.5;
		segs.clear();
		hand::Draw( st, o, segs );
		Check( !segs.empty() && std::fabs( segs.back().r1 - half ) < 1e-6f && std::fabs( segs.back().x1 ) < 1e-6f,
		       fmt( "written half-way: the line stops at x = %.4f with the pen still pressing (%.4f)", segs.empty() ? -1.0 : segs.back().x1,
		            segs.empty() ? -1.0 : segs.back().r1 ) );
		//At full Wobble the pressure wanders +-15% between the two ends.
		st.to    = 1.0;
		o.wobble = 1.0;
		segs.clear();
		hand::Draw( st, o, segs );
		float lo = 1.0f, hi = 0.0f;
		for( const Segment& s : segs )
			if( std::fabs( s.x0 ) < 0.06f )//clear of the landing and the lift
			{
				lo = std::min( lo, s.r0 / half );
				hi = std::max( hi, s.r0 / half );
			}
		Check( lo >= 0.85f - 1e-4f && hi <= 1.15f + 1e-4f && hi - lo > 0.15f, fmt( "Wobble 1: the pressure wanders between %.3f and %.3f of full (law: within 0.85-1.15)", lo, hi ) );
	}
	return Verdict();
}

//===========================================================================
// --music: Resolume's own drum loops, through the harness's stand-in FFT.
//===========================================================================
/// P( X >= k ) for X ~ Binomial( n, p ), in logs so a long tail does not
/// underflow before it is summed.
double binomialTail( int n, int k, double p )
{
	double total = 0.0;
	for( int i = k; i <= n; ++i )
	{
		const double logTerm = std::lgamma( n + 1.0 ) - std::lgamma( i + 1.0 ) - std::lgamma( n - i + 1.0 ) + i * std::log( p ) + ( n - i ) * std::log1p( -p );
		total += std::exp( logTerm );
	}
	return total;
}

int runMusic( const Perturb& perturb )
{
	std::printf( "\n=== music: on Resolume's own drum loops, each band's onsets fall on the loops' sixteenth notes, not by chance\n" );
	//The AV loops are 7.5 s: four bars of 4/4 at 128 bpm, starting on the one.
	//An onset is "on the grid" from half a frame before a sixteenth to two
	//frames after it (the frame whose window first holds the attack, and one
	//more for a soft one). Times unrelated to the grid land there with
	//probability 2.5 frames / a sixteenth = 0.356; each band must beat that
	//at p < 1e-4, one-sided.
	const double sixteenth = 60.0 / 128.0 / 4.0;
	const double before = 0.5 / kFps, after = 2.0 / kFps;
	const double chance = ( before + after ) / sixteenth;
	int loops           = 0;
	for( int i = 1; i <= 6; ++i )
	{
		const std::string path = fmt( "/Applications/Resolume Arena/media/AV/Beat 00%d.wav", i );
		Wav wav;
		std::string error;
		if( !wav.Load( path, error ) )
			continue;
		++loops;
		audio::Analyser ears;
		audio::Settings s;
		std::vector< double > fired[ 3 ];
		for( int f = 0; f < 450; ++f )
		{
			float bins[ audio::kBins ];
			wav.Spectrum( f / kFps, bins );
			ears.Update( bins, audio::kBins, f == 0 ? 0.0f : static_cast< float >( 1.0 / kFps ), s );
			const audio::Band* b[ 3 ] = { &ears.Low(), &ears.Mid(), &ears.High() };
			for( int k = 0; k < 3; ++k )
				if( b[ k ]->fired )
					fired[ k ].push_back( f / kFps + ( perturb.offGrid ? 0.5 * sixteenth : 0.0 ) );
		}
		const char* names[ 3 ] = { "low", "mid", "high" };
		for( int k = 0; k < 3; ++k )
		{
			const int n = static_cast< int >( fired[ k ].size() );
			int on      = 0;
			for( double at : fired[ k ] )
			{
				const double phase = at - std::floor( at / sixteenth ) * sixteenth;
				on += ( phase <= after + 1e-9 || phase >= sixteenth - before - 1e-9 ) ? 1 : 0;
			}
			const double tail = n > 0 ? binomialTail( n, on, chance ) : 1.0;
			Check( n >= 10 && tail < 1e-4, fmt( "Beat 00%d, %-4s %2d onsets, %2d on the grid (%.0f%%, chance %.0f%%): p = %.1e", i, names[ k ], n, on, n ? 100.0 * on / n : 0.0,
			                                     100.0 * chance, tail ) );
		}
	}
	if( loops == 0 )
	{
		std::printf( "  SKIPPED  Resolume's AV loops are not in /Applications/Resolume Arena/media/AV -- nothing measured\n" );
		return 0;
	}
	return Verdict();
}

//===========================================================================
// --defaults, --names, --determinism
//===========================================================================
int runDefaults( const Perturb& perturb )
{
	std::printf( "\n=== defaults: Auto, on twos, Wobble 0.5, 0.0065 lines, white with a glow, scattered\n" );
	for( bool effect : { false, true } )
	{
		ScrawlPlugin p( effect );
		const int drawings = OptionIndex( p.GetFloatParameter( PT_DRAWINGS ), 3 );
		const bool ok = OptionIndex( p.GetFloatParameter( PT_DRIVE ), 3 ) == static_cast< int >( Drive::Auto )
		                && drawings == static_cast< int >( perturb.defaultsShifted ? Drawings::Ones : Drawings::Twos )
		                && std::fabs( WobbleFromParam( p.GetFloatParameter( PT_WOBBLE ) ) - 0.5 ) < 1e-6
		                && std::fabs( LineWidthFromParam( p.GetFloatParameter( PT_LINE_WIDTH ) ) - 0.0065 ) < 1e-6
		                && std::fabs( SizeFromParam( p.GetFloatParameter( PT_SIZE ) ) - 1.0 ) < 1e-6
		                && std::fabs( LingerFromParam( p.GetFloatParameter( PT_LINGER ) ) - 1.0 ) < 1e-6
		                && OptionIndex( p.GetFloatParameter( PT_PALETTE ), 4 ) == static_cast< int >( Palette::White )
		                && OptionIndex( p.GetFloatParameter( PT_PLACEMENT ), 3 ) == static_cast< int >( Placement::Scatter )
		                && OptionIndex( p.GetFloatParameter( PT_BACKDROP ), 3 ) == static_cast< int >( Backdrop::None )
		                && p.GetFloatParameter( PT_PASSES ) == 1.0f && ( !effect || p.GetFloatParameter( PT_MIX ) == 1.0f );
		Check( ok, fmt( "%s: Auto, On Twos, Wobble 0.5, Line Width 0.0065, Size 1, Linger 1, White, Scatter, no backdrop, 1 pass%s", effect ? "Over" : "source",
		                effect ? ", Mix 1" : "" ) );
	}
	return Verdict();
}

int runNames( const Perturb& )
{
	std::printf( "\n=== names: what a host and an OSC address see\n" );
	for( bool effect : { false, true } )
	{
		ScrawlPlugin plugin( effect );
		std::set< std::string > seen;
		int clashes = 0, slashes = 0, emptyElements = 0, tooLong = 0;
		for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
		{
			std::string name = plugin.GetParamName( i );
			slashes += name.find( '/' ) != std::string::npos ? 1 : 0;
			tooLong += name.size() > 16 ? 1 : 0;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			clashes += seen.insert( key ).second ? 0 : 1;
			if( plugin.GetParamType( i ) == FF_TYPE_OPTION )
				for( unsigned int e = 0; e < plugin.GetNumParamElements( i ); ++e )
				{
					const char* element = plugin.GetParamElementName( i, e );
					emptyElements += ( element == nullptr || element[ 0 ] == '\0' ) ? 1 : 0;
				}
		}
		Check( clashes == 0 && slashes == 0 && emptyElements == 0 && tooLong == 0 && plugin.GetParamType( PT_AUDIO ) == FF_TYPE_BUFFER,
		       fmt( "%s: %u parameters, unique as Arena addresses them (%d clash), no '/' (%d), none over 16 characters (%d), no empty option (%d), Audio is the FFT buffer",
		            effect ? "Over" : "source", plugin.ParamCount(), clashes, slashes, tooLong, emptyElements ) );
	}
	return Verdict();
}

std::vector< Segment > runFor( uint32_t seed, int frames )
{
	Bench b;
	b.plugin.SetFloatParameter( PT_SEED, static_cast< float >( seed ) );
	b.plugin.SetFloatParameter( PT_PASSES, 2.0f );
	b.spectrum = []( int f, float* bins ) { demoSpectrum( f, bins ); };
	for( int f = 0; f < frames; ++f )
		b.Step();
	return b.plugin.CurrentSegments();
}

int runDeterminism( const Perturb& perturb )
{
	std::printf( "\n=== determinism: the same music and controls are the same drawing, to the bit\n" );
	auto same = []( const std::vector< Segment >& a, const std::vector< Segment >& b ) {
		if( a.size() != b.size() )
			return false;
		return a.empty() || std::memcmp( a.data(), b.data(), a.size() * sizeof( Segment ) ) == 0;
	};
	const std::vector< Segment > a = runFor( 3, 200 ), b = runFor( 3, 200 ), c = runFor( perturb.determinismSeeds ? 3 : 4, 200 );
	Check( !a.empty() && same( a, b ) && !same( a, c ), fmt( "seed 3 twice: the same %zu segments (%s); seed 4: another drawing (%s)", a.size(),
	                                                        same( a, b ) ? "yes" : "NO", !same( a, c ) ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// The picture checks (GL).
//===========================================================================
double ink( const Floats& img )
{
	double sum = 0.0;
	for( size_t i = 3; i < img.size(); i += 4 )
		sum += img[ i ];
	return sum;
}

/// Is frame n the first frame of a new drawing at `rate` drawings a second?
/// Integer arithmetic, so it is the claim and not a float that rounds.
bool boundary( int n, int rate )
{
	return n == 0 || ( n * rate ) / 60 > ( ( n - 1 ) * rate ) / 60;
}

int runTwos( const Perturb& perturb )
{
	std::printf( "\n=== twos: a new drawing 24, 12 or 8 times a second and the same picture, to the bit, in between\n" );
	struct Size
	{
		int w, h;
	};
	for( const Size& size : { Size { 640, 360 }, Size { 320, 180 } } )
		for( int d = 0; d < 3; ++d )
			for( bool effect : { false, true } )
			{
				if( effect && d != 1 )
					continue;
				const int rate = d == 0 ? 24 : ( d == 1 ? 12 : 8 );
				Rig rig( effect );
				if( !rig.Init( size.w, size.h ) )
					return 1;
				rig.plugin.SetDrawEveryFrameForTest( perturb.everyFrame );
				rig.Set( PT_DRAWINGS, static_cast< float >( d ) );
				rig.Set( PT_DRIVE, static_cast< float >( Drive::Beat ) );
				rig.Press( PT_HIT );
				Floats last;
				std::vector< int > changed, want;
				const int frames = 241;
				for( int n = 0; n < frames; ++n )
				{
					rig.Render( 1 );
					const Floats img = rig.Output();
					if( n > 0 && img != last )
						changed.push_back( n );
					if( n > 0 && boundary( n, rate ) )
						want.push_back( n );
					last = img;
				}
				Check( changed == want, fmt( "%dx%d %s, %s: the picture changed on %zu frames (%s), the drawings start on %zu (%s)", size.w, size.h,
				                             effect ? "Over" : "source", kDrawingsNames[ d ], changed.size(), frameList( changed ).c_str(), want.size(),
				                             frameList( want ).c_str() ) );
			}
	return Verdict();
}

/// The radius of the ink nearest `around` along the ray at angle `a` from
/// (cx, cy), in pixels, by the coverage-weighted centroid inside +-window.
double rayCentroid( const Floats& img, int w, int h, double cx, double cy, double a, double around, double window )
{
	double num = 0.0, den = 0.0;
	const double c = std::cos( a ), s = std::sin( a );
	for( double rho = around - window; rho <= around + window; rho += 0.125 )
	{
		const double x = cx + rho * c - 0.5, y = cy + rho * s - 0.5;
		const int x0 = static_cast< int >( std::floor( x ) ), y0 = static_cast< int >( std::floor( y ) );
		if( x0 < 0 || y0 < 0 || x0 + 1 >= w || y0 + 1 >= h )
			continue;
		const double fx = x - x0, fy = y - y0;
		auto at = [ & ]( int xi, int yi ) { return static_cast< double >( img[ ( static_cast< size_t >( yi ) * w + xi ) * 4 + 3 ] ); };
		const double v = ( 1 - fx ) * ( 1 - fy ) * at( x0, y0 ) + fx * ( 1 - fy ) * at( x0 + 1, y0 ) + ( 1 - fx ) * fy * at( x0, y0 + 1 ) + fx * fy * at( x0 + 1, y0 + 1 );
		num += v * rho;
		den += v;
	}
	return den > 0.0 ? num / den : -1.0;
}

int runBoil( const Perturb& perturb )
{
	std::printf( "\n=== boil: one ring held still, drawn 120 times on ones -- its radial spread between drawings is the hand's, out of the picture\n" );
	struct Size
	{
		int w, h;
	};
	for( const Size& size : { Size { 640, 360 }, Size { 320, 180 } } )
	{
		Rig rig;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Audio ) );//silence: only Hit draws
		rig.Set( PT_DRAWINGS, static_cast< float >( Drawings::Ones ) );
		rig.Set( PT_WOBBLE, 1.0f );
		rig.Set( PT_RINGS, 0.01f );//one line in the set
		rig.Set( PT_BURSTS, 0.0f );
		rig.Set( PT_BOLTS, 0.0f );
		rig.Set( PT_CURLS, 0.0f );
		rig.Set( PT_WAVE, 0.0f );
		rig.Set( PT_GLOW, 0.0f );
		rig.Set( PT_PLACEMENT, static_cast< float >( Placement::Centre ) );
		rig.Set( PT_LINE_WIDTH, ParamFromLineWidth( 0.01 ) );
		rig.plugin.Flags().noWiggle  = true;
		rig.plugin.Flags().frozenAge = 0.4;
		rig.plugin.Flags().fixedHand = perturb.fixedHand;
		rig.plugin.Flags().handScale = perturb.handScale;
		rig.Press( PT_HIT );
		rig.Render( 1 );
		const auto& doodles = rig.plugin.CurrentScore().Doodles();
		if( doodles.size() != 1 )
		{
			Check( false, fmt( "expected one ring, got %zu doodles", doodles.size() ) );
			continue;
		}
		const Doodle ring = doodles.front();
		//The ring as the score draws it: its radius (the stroke's extent) and
		//where its overshoot lies, to stay clear of.
		std::vector< Stroke > strokes;
		Score::Strokes( ring, 0.4, 0, rig.plugin.Settings( static_cast< double >( size.w ) / size.h ), rig.plugin.Flags(), strokes );
		const double r      = strokes.front().extent;
		const double cx     = size.w * 0.5 + ring.anchor.x * size.h, cy = size.h * 0.5 + ring.anchor.y * size.h;
		const double rPx    = r * size.h;
		const double window = 0.035 * rPx + 6.0;
		const int rays      = 360;
		std::vector< double > sum( rays, 0.0 ), sum2( rays, 0.0 );
		std::vector< int > count( rays, 0 );
		int drawings = 0;
		for( int n = 1; drawings < 120; ++n )
		{
			rig.Render( 1 );
			if( !boundary( n, 24 ) )
				continue;
			++drawings;
			const Floats img = rig.Output();
			for( int k = 0; k < rays; ++k )
			{
				const double a = 2.0 * kPi * k / rays;
				//Skip the overshoot (where two passes of the line overlap) and
				//the landing and lift, a little either side.
				double rel = std::fmod( a - ring.direction, 2.0 * kPi );
				if( rel < 0.0 )
					rel += 2.0 * kPi;
				if( rel < 1.0 || rel > 2.0 * kPi - 0.35 )
					continue;
				const double expect = rPx * ( 1.0 + 0.06 * rel / ( 2.0 * kPi ) );
				const double c      = rayCentroid( img, size.w, size.h, cx, cy, a, expect, window );
				if( c < 0.0 )
					continue;
				sum[ static_cast< size_t >( k ) ] += c;
				sum2[ static_cast< size_t >( k ) ] += c * c;
				++count[ static_cast< size_t >( k ) ];
			}
		}
		double variance = 0.0;
		int used        = 0;
		for( int k = 0; k < rays; ++k )
		{
			const int nk = count[ static_cast< size_t >( k ) ];
			if( nk < drawings )
				continue;
			const double mean = sum[ static_cast< size_t >( k ) ] / nk;
			variance += ( sum2[ static_cast< size_t >( k ) ] / nk - mean * mean ) * nk / ( nk - 1.0 );
			++used;
		}
		variance          = used > 0 ? variance / used : 0.0;
		const double A    = specAmplitude( 1.0, r ) * size.h;
		const double want = kSpreadPerA2 * A * A;
		const double ratio = variance / want;
		//120 drawings: the three sinusoids' spread alone is ~9% of the
		//estimate; the tolerance is three of those.
		Check( used > 200 && std::fabs( ratio - 1.0 ) < 0.27, fmt( "%dx%d: ring of %.1f px, %d rays x %d drawings: spread %.2f px, the law says %.2f px (ratio %.3f)", size.w, size.h, rPx,
		                                                           used, drawings, std::sqrt( variance ), std::sqrt( want ), ratio ) );
	}

	//Wobble 0: every drawing is the same picture.
	{
		Rig rig;
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Audio ) );
		rig.Set( PT_WOBBLE, 0.0f );
		rig.plugin.Flags().frozenAge = 0.4;
		rig.Press( PT_HIT );
		rig.Render( 3 );
		const Floats a = rig.Output();
		rig.Render( 30 );
		const Floats b = rig.Output();
		Check( a == b && ink( a ) > 0.0 && perturb.handScale == 1.0, fmt( "Wobble 0: thirty frames later (five drawings) the same picture to the bit (ink %.0f)", ink( a ) ) );
	}
	return Verdict();
}

int runWidth( const Perturb& perturb )
{
	std::printf( "\n=== width: a line's cross-section sums to Line Width, in pixels, at any raster\n" );
	struct Size
	{
		int w, h;
	};
	for( const Size& size : { Size { 640, 360 }, Size { 320, 180 } } )
		for( double width : { 0.006, 0.012, 0.02 } )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Audio ) );
			rig.Set( PT_WOBBLE, 0.0f );
			rig.Set( PT_GLOW, 0.0f );
			rig.Set( PT_WAVE, 1.0f );
			rig.Set( PT_LINE_WIDTH, ParamFromLineWidth( width ) );
			rig.plugin.Flags().flatWave = true;
			rig.Render( 2 );
			const Floats img = rig.Output();
			double sum = 0.0, worst = 0.0;
			const double want = ( perturb.radiusIsWidth ? 2.0 : 1.0 ) * LineWidthFromParam( ParamFromLineWidth( width ) ) * size.h;
			for( int x = 0; x < size.w; ++x )
			{
				double column = 0.0;
				for( int y = 0; y < size.h; ++y )
					column += img[ ( static_cast< size_t >( y ) * size.w + x ) * 4 + 3 ];
				sum += column;
				worst = std::max( worst, std::fabs( column - want ) );
			}
			//Half-float coverage: 2^-11 of each of a few pixels; a hundredth of a
			//pixel covers it.
			Check( worst < 0.01, fmt( "%dx%d, Line Width %.3f: every column sums to %.4f px, wants %.4f (worst off by %.5f)", size.w, size.h, width, sum / size.w, want, worst ) );
		}
	return Verdict();
}

int runLatency( const Perturb& perturb )
{
	std::printf( "\n=== latency: a kick is on the page in the first drawing made at or after its frame\n" );
	for( int rate : { 12, 24, 8 } )
		for( int kick = 60; kick < 60 + 60 / rate + 1; ++kick )
		{
			Rig rig;
			if( !rig.Init( 320, 180 ) )
				return 1;
			rig.plugin.SetDrawEveryFrameForTest( perturb.everyFrame );
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Audio ) );
			rig.Set( PT_DRAWINGS, static_cast< float >( rate == 24 ? 0 : ( rate == 12 ? 1 : 2 ) ) );
			rig.Set( PT_WAVE, 0.0f );
			rig.Set( PT_GLOW, 0.0f );
			//Silence, then one kick (and only a kick) from frame `kick` on.
			rig.spectrum = [ kick ]( int f, float* bins ) {
				for( int i = 0; i < audio::kBins; ++i )
					bins[ i ] = 0.0f;
				if( f >= kick )
				{
					const float k = static_cast< float >( std::exp( -( f - kick ) / kFps / 0.12 ) );
					bins[ 0 ]     = 0.5f * k;
					bins[ 1 ]     = 0.35f * k;
				}
			};
			int first = -1;
			for( int n = 0; n < kick + 30 && first < 0; ++n )
			{
				rig.Render( 1 );
				if( ink( rig.Output() ) > 0.0 )
					first = n;
			}
			int want = kick;
			while( !boundary( want, rate ) )
				++want;
			Check( first == want, fmt( "%d drawings/s, kick on frame %d: first ink on frame %d, the next drawing starts on %d (%.0f ms)", rate, kick, first, want,
			                           ( first - kick ) * 1000.0 / kFps ) );
		}
	return Verdict();
}

int runOver( const Perturb& )
{
	std::printf( "\n=== over: the clip (alpha and all) untouched with nothing drawn, and with Mix 0\n" );
	for( const auto& size : { std::pair< int, int > { 640, 360 }, std::pair< int, int > { 320, 180 } } )
	{
		Floats card = buildCard( size.first, size.second );
		for( int y = 0; y < size.second; ++y )
			for( int x = 0; x < size.first; ++x )
			{
				float* o      = &card[ ( static_cast< size_t >( y ) * size.first + x ) * 4 ];
				const float a = x < size.first / 3 ? 0.0f : x < 2 * size.first / 3 ? 0.5f : 1.0f;
				for( int c = 0; c < 4; ++c )
					o[ c ] *= ( c < 3 ? a : 1.0f );
				o[ 3 ] = a;
			}
		auto diff = []( const Floats& a, const Floats& b ) {
			double worst = 0.0;
			for( size_t i = 0; i < a.size(); ++i )
				worst = std::max( worst, static_cast< double >( std::fabs( a[ i ] - b[ i ] ) ) );
			return worst;
		};
		{
			Rig rig( true );
			if( !rig.Init( size.first, size.second, &card ) )
				return 1;
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Audio ) );
			rig.Set( PT_WAVE, 0.0f );
			rig.Render( 30 );
			Check( diff( rig.Output(), card ) == 0.0, fmt( "%dx%d: nothing drawn, the clip exactly (worst %.1e)", size.first, size.second, diff( rig.Output(), card ) ) );
		}
		{
			Rig rig( true );
			if( !rig.Init( size.first, size.second, &card ) )
				return 1;
			rig.Set( PT_DRIVE, static_cast< float >( Drive::Beat ) );
			rig.Press( PT_HIT );
			rig.Render( 30 );
			const double drawnDiff = diff( rig.Output(), card );
			rig.Set( PT_MIX, 0.0f );
			rig.Render( 1 );
			Check( drawnDiff > 0.1 && diff( rig.Output(), card ) == 0.0,
			       fmt( "%dx%d: doodles drawn (differ by %.2f), Mix 0 the clip exactly (worst %.1e)", size.first, size.second, drawnDiff, diff( rig.Output(), card ) ) );
		}
	}
	return Verdict();
}

int runResize( const Perturb& )
{
	std::printf( "\n=== resize: mid-show, 640x360 to 320x180 and back\n" );
	Rig rig;
	if( !rig.Init( 640, 360 ) )
		return 1;
	rig.Set( PT_DRIVE, static_cast< float >( Drive::Beat ) );
	rig.Render( 90 );
	const size_t before = rig.plugin.CurrentScore().Doodles().size();
	rig.Resize( 320, 180 );
	rig.Render( 1 );
	const size_t after = rig.plugin.CurrentScore().Doodles().size();
	const double small = ink( rig.Output() );
	rig.Resize( 640, 360 );
	rig.Render( 1 );
	const double big = ink( rig.Output() );
	Check( before > 0 && after >= before - 2 && glGetError() == GL_NO_ERROR, fmt( "%zu doodles before, %zu after: the show carried on, no GL error", before, after ) );
	Check( small > 0.0 && big > small, fmt( "both sizes draw (ink %.0f at 320x180, %.0f back at 640x360)", small, big ) );
	return Verdict();
}

//===========================================================================
// --state: what the host hands over is what it gets back (bassalt's check,
// with the blend equation added: the strokes are drawn with GL_MAX).
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: what the host hands over is what it gets back\n" );
	int problems = 0;
	std::string what;
	for( bool effect : { false, true } )
	{
		Rig rig( effect );
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Beat ) );
		rig.Press( PT_HIT );
		GLuint hostArray = 0, hostPack = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostPack );
		for( int frame = 0; frame < 12; ++frame )
		{
			const float silence[ audio::kBins ] = {};
			feed( rig.plugin, silence );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 7, 5, 300, 170 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_PIXEL_PACK_BUFFER, hostPack );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glBlendEquationSeparate( GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );

			rig.plugin.SetTime( frame / kFps );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;

			GLint viewport[ 4 ] = {}, array = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0, pack = 0, eqRGB = 0, eqAlpha = 0;
			GLfloat clear[ 4 ] = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_EQUATION_RGB, &eqRGB );
			glGetIntegerv( GL_BLEND_EQUATION_ALPHA, &eqAlpha );
			glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &pack );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 7 && viewport[ 1 ] == 5 && viewport[ 2 ] == 300 && viewport[ 3 ] == 170, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( pack == static_cast< GLint >( hostPack ), "pack-buffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( eqRGB == GL_FUNC_REVERSE_SUBTRACT && eqAlpha == GL_FUNC_ADD, "blend-equation" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBlendEquation( GL_FUNC_ADD );
		glBindVertexArray( 0 );
		glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostPack );
	}
	Check( problems == 0, fmt( "source and Over, twelve frames each (two new drawings): viewport, vertex array, program, active unit, framebuffer, "
	                           "pack buffer, blend, blend equation, scissor, clear colour and eight texture units all as the host left them (%d wrong:%s)",
	                           problems, what.empty() ? " none" : what.c_str() ) );
	return Verdict();
}

int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	std::printf( "\n=== bench: ms/frame, busy (every kind at 1, a Hit every 10 frames, the demo band, Passes 3), median of 120 frames\n" );
	for( const Size& size : sizes )
	{
		Rig rig( true );
		rig.demo = true;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		for( unsigned int id : { PT_RINGS, PT_BURSTS, PT_BOLTS, PT_CURLS, PT_WAVE } )
			rig.Set( id, 1.0f );
		rig.Set( PT_PASSES, 3.0f );
		rig.Render( 60 );
		glFinish();
		std::vector< double > drawingTimes, heldTimes;
		size_t segments = 0;
		for( int f = 0; f < 120; ++f )
		{
			if( f % 10 == 0 )
				rig.Press( PT_HIT );
			const int before = rig.plugin.DrawingsMade();
			const auto start = std::chrono::steady_clock::now();
			rig.Render( 1 );
			glFinish();
			const double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
			if( rig.plugin.DrawingsMade() != before )
			{
				drawingTimes.push_back( ms );
				segments = std::max( segments, rig.plugin.CurrentSegments().size() );
			}
			else
				heldTimes.push_back( ms );
		}
		std::sort( drawingTimes.begin(), drawingTimes.end() );
		std::sort( heldTimes.begin(), heldTimes.end() );
		std::printf( "  %-6s a new drawing: median %6.2f ms, worst %6.2f (up to %zu segments); a held one: median %5.2f ms\n", size.name,
		             drawingTimes[ drawingTimes.size() / 2 ], drawingTimes.back(), segments, heldTimes[ heldTimes.size() / 2 ] );
	}
	return 0;
}

//===========================================================================
// The registry, --offline and --negative.
//===========================================================================
const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "onset", runOnset },     { "bands", runBands },   { "music", runMusic },   { "beat", runBeat },     { "hand", runHand },
		{ "defaults", runDefaults }, { "names", runNames }, { "determinism", runDeterminism },
		{ "twos", runTwos },       { "boil", runBoil },     { "width", runWidth },   { "latency", runLatency },
		{ "over-check", runOver }, { "resize", runResize }, { "state", runState },
	};
	return list;
}

/// The checks that need no GL context. The one place that knows which they
/// are -- `--offline` (what CI runs) is everything else's complement.
bool isOffline( const std::string& flag )
{
	static const char* const offline[] = { "onset", "bands", "beat", "hand", "defaults", "names", "determinism" };
	for( const char* name : offline )
		if( flag == name )
			return true;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "onset", runOnset, "the analyser not primed: deaf after the cold start", []( Perturb& p ) { p.unprimed = true; } );
	add( "bands", runBands, "expect rings on the snare", []( Perturb& p ) { p.wrongBands = true; } );
	add( "music", runMusic, "every onset half a sixteenth late: off the music's grid", []( Perturb& p ) { p.offGrid = true; } );
	add( "beat", runBeat, "expect 120 bpm whatever the host says", []( Perturb& p ) { p.assumeBpm = 120.0; } );
	add( "hand", runHand, "two drawings with one seed: no boil", []( Perturb& p ) { p.sameDrawing = true; } );
	add( "hand", runHand, "a hand 1.3 times as unsteady as the law", []( Perturb& p ) { p.handScale = 1.3; } );
	add( "hand", runHand, "a pen that lands and lifts at full pressure", []( Perturb& p ) { p.noTaper = true; } );
	add( "defaults", runDefaults, "expect On Ones", []( Perturb& p ) { p.defaultsShifted = true; } );
	add( "determinism", runDeterminism, "expect seeds 3 and 4 to agree", []( Perturb& p ) { p.determinismSeeds = true; } );
	add( "twos", runTwos, "a new drawing every frame", []( Perturb& p ) { p.everyFrame = true; } );
	add( "boil", runBoil, "the same hand every drawing", []( Perturb& p ) { p.fixedHand = true; } );
	add( "boil", runBoil, "a hand 1.3 times as unsteady as the law", []( Perturb& p ) { p.handScale = 1.3; } );
	add( "width", runWidth, "expect the radius to be the width", []( Perturb& p ) { p.radiusIsWidth = true; } );
	add( "latency", runLatency, "a new drawing every frame: no wait for the next one", []( Perturb& p ) { p.everyFrame = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}
} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/scrawl.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 100;
	bool effect = false, demo = false, ghost = false;
	std::string wavPath;
	std::string mode, scriptPath;
	int filmFrames = -1;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "sctest -- render Scrawl offline and measure its hand\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/scrawl.png)\n"
			             "  --over            the Over effect, on the harness's card\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames of 60 fps before reading back (default 100)\n"
			             "  --audio demo      feed the synthetic band: a kick every 28 frames, a snare\n"
			             "                    on every other one, hats between (default: silence)\n"
			             "  --audio ghost     the same with a soft ghost kick between the kicks\n"
			             "  --wav PATH        a 16-bit WAV, looped, through the harness's stand-in for\n"
			             "                    Resolume's FFT (an assumption: see README)\n"
			             "  --set \"Name=V\"    set a parameter by its display name; an option by\n"
			             "                    its index or its name; an event (Hit) is pressed. Repeatable.\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in (Over) and out\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "  --shaders --out DIR   every program as compiled, written out and through the driver\n\n"
			             "  checks: --onset --bands --music --beat --hand --defaults --names --determinism\n"
			             "          --twos --boil --width --latency --over-check --resize --state --negative --bench\n"
			             "  --offline         the checks and negative controls that need no GL context\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--audio" && hasNext )
		{
			const std::string feedName = argv[ ++i ];
			if( feedName != "demo" && feedName != "ghost" && feedName != "none" )
			{
				std::fprintf( stderr, "--audio takes 'demo', 'ghost' or 'none'\n" );
				return 2;
			}
			demo  = feedName != "none";
			ghost = feedName == "ghost";
		}
		else if( argument == "--wav" && hasNext )
			wavPath = argv[ ++i ];
		else if( argument == "--over" )
			effect = true;
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "pipe";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}

	Wav music;
	if( !wavPath.empty() )
	{
		std::string error;
		if( !music.Load( wavPath, error ) )
		{
			std::fprintf( stderr, "--wav: %s\n", error.c_str() );
			return 2;
		}
	}

	if( mode == "listen" )
	{
		//What the analyser hears: each band's onsets over the frames asked for,
		//from --wav or the demo band. No GL, no plugin.
		audio::Analyser ears;
		audio::Settings s;
		std::vector< double > fired[ 3 ];
		for( int f = 0; f < frames; ++f )
		{
			float bins[ audio::kBins ] = {};
			if( !wavPath.empty() )
				music.Spectrum( f / kFps, bins );
			else if( demo )
				demoSpectrum( f, bins, kDemoPeriod, true, ghost );
			ears.Update( bins, audio::kBins, f == 0 ? 0.0f : static_cast< float >( 1.0 / kFps ), s );
			const audio::Band* b[ 3 ] = { &ears.Low(), &ears.Mid(), &ears.High() };
			for( int k = 0; k < 3; ++k )
				if( b[ k ]->fired )
					fired[ k ].push_back( f / kFps );
		}
		const char* names[ 3 ] = { "low", "mid", "high" };
		for( int k = 0; k < 3; ++k )
		{
			std::printf( "%-4s %3zu onsets:", names[ k ], fired[ k ].size() );
			for( double s0 : fired[ k ] )
				std::printf( " %.3f", s0 );
			std::printf( "\n" );
		}
		return 0;
	}

	if( mode == "list" )
	{
		ScrawlPlugin plugin( effect );
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-18s %-9s %.4f\n", parameter.index, parameter.name.c_str(), parameter.kind.c_str(), parameter.value );
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
	{
		//No context at all: this is what a runner with no accelerated GL can
		//run. The skip is loud, so a green run is not read as one that
		//checked the shaders against a driver.
		int failed = 0;
		for( const CheckEntry& check : checks() )
			if( isOffline( check.flag ) )
			{
				g_failures = 0;
				failed |= check.run( Perturb {} );
			}
		failed |= runNegative( true );
		std::printf( "\n  offline: the checks that need no GL context. The shader and pixel checks were NOT run --\n"
		             "  tools/verify.sh runs them against a real driver, at 320x180 and above.\n"
		             "\n  %s\n",
		             failed == 0 ? "PASS" : "FAIL" );
		return failed == 0 ? 0 : 1;
	}

	if( mode == "shaders" )
	{
		//Every program exactly as the plugin compiles it, for tools/glslc.sh.
		const std::string dir = outPath;
		for( const shaders::Program& program : shaders::Programs() )
		{
			std::ofstream( dir + "/" + program.name + ".vert" ) << program.vertex;
			std::ofstream( dir + "/" + program.name + ".frag" ) << program.fragment;
		}
		std::printf( "wrote %zu programs to %s\n", shaders::Programs().size(), dir.c_str() );
		CGLContextObj context = createContext();
		if( context == nullptr )
			return 1;
		int failed = 0;
		for( const shaders::Program& program : shaders::Programs() )
		{
			auto compile = [ & ]( GLenum type, const std::string& text, const char* what ) {
				const GLuint shader = glCreateShader( type );
				const char* source  = text.c_str();
				glShaderSource( shader, 1, &source, nullptr );
				glCompileShader( shader );
				GLint ok = 0;
				glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
				if( !ok )
				{
					char log[ 4096 ] = {};
					glGetShaderInfoLog( shader, sizeof( log ), nullptr, log );
					std::printf( "  FAIL  %s %s:\n%s\n", program.name, what, log );
					++failed;
				}
				return shader;
			};
			const GLuint vs     = compile( GL_VERTEX_SHADER, program.vertex, "vertex" );
			const GLuint fs     = compile( GL_FRAGMENT_SHADER, program.fragment, "fragment" );
			const GLuint linked = glCreateProgram();
			glAttachShader( linked, vs );
			glAttachShader( linked, fs );
			glLinkProgram( linked );
			GLint ok = 0;
			glGetProgramiv( linked, GL_LINK_STATUS, &ok );
			if( !ok )
			{
				char log[ 4096 ] = {};
				glGetProgramInfoLog( linked, sizeof( log ), nullptr, log );
				std::printf( "  FAIL  %s link:\n%s\n", program.name, log );
				++failed;
			}
			else
				std::printf( "  ok    %s\n", program.name );
			glDeleteProgram( linked );
			glDeleteShader( vs );
			glDeleteShader( fs );
		}
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return failed == 0 ? 0 : 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( effect, width, height, scriptPath, filmFrames, settings, demo, ghost, wavPath.empty() ? nullptr : &music );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( effect );
		rig.demo  = demo;
		rig.ghost = ghost;
		rig.wav   = wavPath.empty() ? nullptr : &music;
		if( !rig.Init( width, height ) )
			result = 1;
		else
		{
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			if( !rig.Render( std::max( frames, 1 ) ) )
				result = 1;
			if( result == 0 )
			{
				if( writePng( outPath, width, height, rig.Output() ) )
					std::printf( "wrote %s -- %dx%d, %d frames, drawing %lld, %zu doodles, %zu segments, %s\n", outPath.c_str(), width, height, rig.frame,
					             rig.plugin.DrawingIndex(), rig.plugin.CurrentScore().Doodles().size(), rig.plugin.CurrentSegments().size(),
					             rig.plugin.AudioDriving() ? "following the audio" : "following the beat" );
				else
					result = 1;
			}
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
