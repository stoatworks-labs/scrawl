#include "Scrawl.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace ffglex;

namespace scrawl
{
namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

constexpr int kClockVotes    = 4;
constexpr double kMaxJump    = 1.0; ///< host seconds; a bigger step (or any step back) re-bases the clock
constexpr double kFrameGuess = 1.0 / 60.0;

/// The glow's blur, as a fraction of the frame's height (13 px at 1080).
constexpr double kGlowSigma = 0.012;

/// How much the glow buffer is turned up before Glow scales it: a line's blur
/// peaks well under its own coverage.
constexpr float kGlowGain = 3.0f;

/// A drawing index nudged past float error at a boundary: a frame at exactly
/// 5/60 s is drawing 1 on twos, not drawing 0 by a rounding.
constexpr double kBoundaryNudge = 1e-6;
} // namespace

//---------------------------------------------------------------------------
ScrawlPlugin::ScrawlPlugin( bool effect ) : isEffect( effect )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	//-------------------------------------------------------------------
	// Defaults: white lines with a little glow, on twos, scattered, following
	// the music when there is any and the beat when there is not.
	//-------------------------------------------------------------------
	params[ PT_DRIVE ]       = static_cast< float >( Drive::Auto );
	params[ PT_DRAWINGS ]    = static_cast< float >( Drawings::Twos );
	params[ PT_WOBBLE ]      = 0.5f;
	params[ PT_LINE_WIDTH ]  = ParamFromLineWidth( 0.0065 );
	params[ PT_PASSES ]      = 1.0f;
	params[ PT_SIZE ]        = ParamFromSize( 1.0 );
	params[ PT_LINGER ]      = 0.5f;
	params[ PT_SEED ]        = 0.0f;
	params[ PT_RINGS ]       = 0.7f;
	params[ PT_BURSTS ]      = 0.6f;
	params[ PT_BOLTS ]       = 0.4f;
	params[ PT_CURLS ]       = 0.5f;
	params[ PT_WAVE ]        = 0.5f;
	params[ PT_PLACEMENT ]   = static_cast< float >( Placement::Scatter );
	params[ PT_CENTRE_X ]    = 0.5f;
	params[ PT_CENTRE_Y ]    = 0.5f;
	params[ PT_PALETTE ]     = static_cast< float >( Palette::White );
	params[ PT_COLOUR_R ]    = 1.0f;
	params[ PT_COLOUR_G ]    = 0.3f;
	params[ PT_COLOUR_B ]    = 0.7f;
	params[ PT_GLOW ]        = 0.35f;
	params[ PT_BACKDROP ]    = static_cast< float >( Backdrop::None );
	params[ PT_SENSITIVITY ] = 0.6f;
	params[ PT_BASS_BINS ]   = 2.0f;
	params[ PT_TREBLE_FROM ] = 16.0f;
	params[ PT_MIX ]         = 1.0f;

	auto standard = [ this ]( unsigned int id, const char* name ) { SetParamInfo( id, name, FF_TYPE_STANDARD, params[ id ] ); };
	auto option   = [ this ]( unsigned int id, const char* name, const char* const* names, int count ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( count ), params[ id ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, static_cast< unsigned int >( i ), names[ i ], static_cast< float >( i ) );
	};
	auto integer = [ this ]( unsigned int id, const char* name, float lo, float hi ) {
		//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an integer
		//is declared with its real default and range.
		SetParamInfo( id, name, FF_TYPE_INTEGER, params[ id ] );
		SetParamRange( id, lo, hi );
	};
	auto colour = [ this ]( unsigned int id, const char* red, const char* green, const char* blue ) {
		SetParamInfo( id, red, FF_TYPE_RED, params[ id ] );
		SetParamInfo( id + 1, green, FF_TYPE_GREEN, params[ id + 1 ] );
		SetParamInfo( id + 2, blue, FF_TYPE_BLUE, params[ id + 2 ] );
	};

	option( PT_DRIVE, "Drive", kDriveNames, static_cast< int >( Drive::Count ) );
	SetParamInfo( PT_HIT, "Hit", FF_TYPE_EVENT, false );
	option( PT_DRAWINGS, "Drawings", kDrawingsNames, static_cast< int >( Drawings::Count ) );
	standard( PT_WOBBLE, "Wobble" );
	standard( PT_LINE_WIDTH, "Line Width" );
	integer( PT_PASSES, "Passes", 1.0f, 3.0f );
	standard( PT_SIZE, "Size" );
	standard( PT_LINGER, "Linger" );
	integer( PT_SEED, "Seed", 0.0f, 9999.0f );

	standard( PT_RINGS, "Rings" );
	standard( PT_BURSTS, "Bursts" );
	standard( PT_BOLTS, "Bolts" );
	standard( PT_CURLS, "Curls" );
	standard( PT_WAVE, "Wave" );

	option( PT_PLACEMENT, "Placement", kPlacementNames, static_cast< int >( Placement::Count ) );
	standard( PT_CENTRE_X, "Centre X" );
	standard( PT_CENTRE_Y, "Centre Y" );

	option( PT_PALETTE, "Palette", kPaletteNames, static_cast< int >( Palette::Count ) );
	colour( PT_COLOUR_R, "Colour", "Colour_Green", "Colour_Blue" );
	standard( PT_GLOW, "Glow" );
	option( PT_BACKDROP, "Backdrop", kBackdropNames, static_cast< int >( Backdrop::Count ) );

	SetBufferParamInfo( PT_AUDIO, "Audio", audio::kBins, FF_USAGE_FFT );
	standard( PT_SENSITIVITY, "Sensitivity" );
	integer( PT_BASS_BINS, "Bass Bins", 1.0f, 8.0f );
	integer( PT_TREBLE_FROM, "Treble From", 4.0f, 48.0f );

	for( unsigned int id = PT_DRIVE; id <= PT_SEED; ++id )
		SetParamGroup( id, "Draw" );
	for( unsigned int id = PT_RINGS; id <= PT_WAVE; ++id )
		SetParamGroup( id, "Doodles" );
	for( unsigned int id = PT_PLACEMENT; id <= PT_CENTRE_Y; ++id )
		SetParamGroup( id, "Place" );
	for( unsigned int id = PT_PALETTE; id <= PT_BACKDROP; ++id )
		SetParamGroup( id, "Look" );
	for( unsigned int id = PT_AUDIO; id <= PT_TREBLE_FROM; ++id )
		SetParamGroup( id, "Audio" );

	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int id = PT_ABOUT_TEXT; id < PT_SOURCE_COUNT; ++id )
		SetParamGroup( id, "About" );
	static_assert( PT_ABOUT_TEXT + stoatworks::about::kParamCount == PT_SOURCE_COUNT,
	               "the About block's size changed: add or remove a PT_ABOUT_BUTTON id" );

	if( isEffect )
	{
		standard( PT_MIX, "Mix" );
		SetParamGroup( PT_MIX, "Over" );
	}
}

ScrawlPlugin::~ScrawlPlugin() = default;

//---------------------------------------------------------------------------
FFResult ScrawlPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	//Every program from the one list (Shaders.cpp), so what the harness checks
	//through glslc is what is compiled here.
	FFGLShader* const targets[] = { &strokeProgram, &reduceProgram, &blurProgram, &compositeProgram };
	const std::vector< shaders::Program > list = shaders::Programs();
	bool ok = list.size() == 4;
	for( size_t i = 0; ok && i < list.size(); ++i )
		if( !targets[ i ]->Compile( list[ i ].vertex, list[ i ].fragment ) )
		{
			diag::error( std::string( "the " ) + list[ i ].name + " shader failed to compile" );
			ok = false;
		}
	if( !ok || !quad.Initialise() )
	{
		FFGLLog::LogToHost( "Scrawl: a shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//The capsules: no per-vertex data at all (the corner is gl_VertexID), three
	//per-instance attributes.
	GLint vao = 0, vbo = 0;
	glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &vao );
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &vbo );
	glGenVertexArrays( 1, &segmentVao );
	glGenBuffers( 1, &segmentVbo );
	glBindVertexArray( segmentVao );
	glBindBuffer( GL_ARRAY_BUFFER, segmentVbo );
	segmentCapacity = 4096;
	glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( segmentCapacity * sizeof( Segment ) ), nullptr, GL_DYNAMIC_DRAW );
	static_assert( sizeof( Segment ) == 10 * sizeof( float ), "Segment is uploaded as ten packed floats" );
	const GLsizei stride = static_cast< GLsizei >( sizeof( Segment ) );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< void* >( static_cast< uintptr_t >( 0 ) ) );
	glVertexAttribDivisor( 0, 1 );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< void* >( static_cast< uintptr_t >( 4 * sizeof( float ) ) ) );
	glVertexAttribDivisor( 1, 1 );
	glEnableVertexAttribArray( 2 );
	glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< void* >( static_cast< uintptr_t >( 6 * sizeof( float ) ) ) );
	glVertexAttribDivisor( 2, 1 );
	glBindVertexArray( static_cast< GLuint >( vao ) );
	glBindBuffer( GL_ARRAY_BUFFER, static_cast< GLuint >( vbo ) );

	drawn = false;
	diag::info( isEffect ? "initialised (Over)" : "initialised (source)" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ScrawlPlugin::DeInitGL()
{
	strokeProgram.FreeGLResources();
	reduceProgram.FreeGLResources();
	blurProgram.FreeGLResources();
	compositeProgram.FreeGLResources();
	quad.Release();
	ReleaseTargets();
	if( segmentVbo )
		glDeleteBuffers( 1, &segmentVbo );
	if( segmentVao )
		glDeleteVertexArrays( 1, &segmentVao );
	segmentVbo = segmentVao = 0;
	segmentCapacity = 0;
	drawn           = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
// Time
//---------------------------------------------------------------------------
void ScrawlPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via boreal and
	//jackpot).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				lastNow    = -1.0;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

FFResult ScrawlPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void ScrawlPlugin::SetBeatInfo( float bpm, float barPhase )
{
	CFFGLPlugin::SetBeatInfo( bpm, barPhase );
	transport.SetBeatInfo( bpm, barPhase );
}

//---------------------------------------------------------------------------
// The controls as the score reads them
//---------------------------------------------------------------------------
ScoreSettings ScrawlPlugin::Settings( double aspect ) const
{
	ScoreSettings s;
	s.aspect = aspect;
	s.size   = SizeFromParam( params[ PT_SIZE ] );
	s.linger = LingerFromParam( params[ PT_LINGER ] );
	s.amount = { { AmountFromParam( params[ PT_RINGS ] ), AmountFromParam( params[ PT_BURSTS ] ), AmountFromParam( params[ PT_BOLTS ] ),
	               AmountFromParam( params[ PT_CURLS ] ) } };
	s.wave      = AmountFromParam( params[ PT_WAVE ] );
	s.placement = static_cast< Placement >( OptionIndex( params[ PT_PLACEMENT ], static_cast< int >( Placement::Count ) ) );
	s.centre    = { ( Clamp( params[ PT_CENTRE_X ], 0.0, 1.0 ) - 0.5 ) * aspect, Clamp( params[ PT_CENTRE_Y ], 0.0, 1.0 ) - 0.5 };
	s.seed      = static_cast< uint32_t >( std::clamp( std::lround( params[ PT_SEED ] ), 0L, 9999L ) );
	s.palette   = static_cast< Palette >( OptionIndex( params[ PT_PALETTE ], static_cast< int >( Palette::Count ) ) );
	for( int c = 0; c < 3; ++c )
		s.colour[ c ] = std::clamp( params[ PT_COLOUR_R + c ], 0.0f, 1.0f );
	s.wobble    = WobbleFromParam( params[ PT_WOBBLE ] );
	s.lineWidth = LineWidthFromParam( params[ PT_LINE_WIDTH ] );
	s.passes    = std::clamp( static_cast< int >( std::lround( params[ PT_PASSES ] ) ), 1, 3 );
	return s;
}

//---------------------------------------------------------------------------
// Listening: the spectrum, the transport and the Hit button, into spawns
//---------------------------------------------------------------------------
void ScrawlPlugin::Listen( double dt, double aspect )
{
	const ScoreSettings s = Settings( aspect );

	float bins[ audio::kBins ] = {};
	int count                  = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
	{
		count = std::min( audio::kBins, static_cast< int >( info->elements.size() ) );
		for( int i = 0; i < count; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	audio::Settings as;
	as.sensitivity = static_cast< float >( SensitivityFromParam( params[ PT_SENSITIVITY ] ) );
	as.bassBins    = std::clamp( static_cast< int >( std::lround( params[ PT_BASS_BINS ] ) ), 1, 8 );
	as.trebleFrom  = std::clamp( static_cast< int >( std::lround( params[ PT_TREBLE_FROM ] ) ), 4, 48 );
	as.prime       = prime;
	analyser.Update( bins, count, static_cast< float >( dt ), as );
	if( !loggedAudio && analyser.EverHeard() )
	{
		diag::info( "audio has arrived in the FFT buffer" );
		loggedAudio = true;
	}

	transport.Advance( dt, eighths );

	const Drive drive = static_cast< Drive >( OptionIndex( params[ PT_DRIVE ], static_cast< int >( Drive::Count ) ) );
	audioDriving      = drive == Drive::Audio || ( drive == Drive::Auto && analyser.Present() );
	const int curlsTwice = s.amount[ static_cast< size_t >( Kind::Curl ) ] >= 0.67 ? 1 : 0;

	if( audioDriving )
	{
		if( analyser.Low().fired )
			score.Spawn( Kind::Rings, clock, analyser.Low().strength, s );
		if( analyser.Mid().fired )
			score.Spawn( Kind::Burst, clock, analyser.Mid().strength, s );
		if( analyser.High().fired )
			for( int i = 0; i <= curlsTwice; ++i )
				score.Spawn( Kind::Curl, clock, analyser.High().strength, s );
		//A bolt is a kick that reaches the low band's own recent peak, and no
		//more often than Bolts allows: one every 2 s at 0, every 0.4 s at 1.
		//Not a full-spectrum onset -- the square root that lets a hi-hat be
		//heard at all makes 33 bins of hat outweigh two bins of kick, and the
		//bolts landed on the hats.
		const double boltAmount = s.amount[ static_cast< size_t >( Kind::Bolt ) ];
		const double bar        = 1.0 - 0.5 * boltAmount;
		const double gap        = 0.4 + 1.6 * ( 1.0 - boltAmount );
		if( analyser.Low().fired && analyser.Low().strength >= bar && clock - lastBolt >= gap )
		{
			score.Spawn( Kind::Bolt, clock, analyser.Low().strength, s );
			lastBolt = clock;
		}
		for( size_t j = 0; j < waveNow.levels.size(); ++j )
			waveNow.levels[ j ] = analyser.WaveLevels()[ j ];
		waveNow.level = analyser.Level();
	}
	else
	{
		//The beat: rings on every beat, a burst on two and four, a bolt on the
		//one, curls on the "and"s.
		for( long long e : eighths )
		{
			if( e % 2 == 0 )
			{
				const long long inBar = ( e / 2 ) % 4;
				score.Spawn( Kind::Rings, clock, inBar == 0 ? 0.9 : 0.6, s );
				if( inBar == 1 || inBar == 3 )
					score.Spawn( Kind::Burst, clock, 0.75, s );
				if( inBar == 0 )
					score.Spawn( Kind::Bolt, clock, 0.85, s );
			}
			else
				for( int i = 0; i <= curlsTwice; ++i )
					score.Spawn( Kind::Curl, clock, 0.5, s );
		}
		//The wave swells on each beat and settles between.
		const float env = static_cast< float >( std::exp( -transport.SinceBeat() / 0.18 ) );
		waveNow.levels  = { { env, 0.75f * env, 0.35f + 0.35f * env, 0.25f + 0.2f * env, 0.18f, 0.12f } };
		waveNow.level   = 0.4f + 0.6f * env;
	}

	while( hitsTaken < hitPresses )
	{
		++hitsTaken;
		for( int k = 0; k < kKinds; ++k )
			score.Spawn( static_cast< Kind >( k ), clock, 1.0, s );
	}
	score.Expire( clock, s );
}

bool ScrawlPlugin::Advance( double aspect )
{
	UpdateClock();
	//Plugin time from the first frame, re-based across a jump (a scrub, a
	//restart, a stall): a jump draws nothing new and spawns nothing old.
	if( lastNow < 0.0 || now < lastNow || now - lastNow > kMaxJump )
	{
		clockBase = lastClock < 0.0 ? 0.0 : lastClock + kFrameGuess;
		nowBase   = now;
	}
	clock            = clockBase + ( now - nowBase );
	const double dt  = lastClock < 0.0 ? 0.0 : std::max( 0.0, clock - lastClock );
	lastNow          = now;
	lastClock        = clock;

	Listen( dt, aspect );

	const double rate = DrawingsPerSecond( static_cast< Drawings >( OptionIndex( params[ PT_DRAWINGS ], static_cast< int >( Drawings::Count ) ) ) );
	const long long k = drawEveryFrame ? framesSeen : static_cast< long long >( std::floor( clock * rate + kBoundaryNudge ) );
	++framesSeen;
	const bool changed = k != drawing;
	if( changed )
	{
		drawing   = k;
		waveDrawn = waveNow;
	}
	return changed || dirty;
}

void ScrawlPlugin::BuildDrawing( double aspect )
{
	const double rate = DrawingsPerSecond( static_cast< Drawings >( OptionIndex( params[ PT_DRAWINGS ], static_cast< int >( Drawings::Count ) ) ) );
	const double time = drawEveryFrame ? clock : static_cast< double >( drawing ) / rate;
	score.Build( time, drawEveryFrame ? 0.0 : 1.0 / rate, drawing, Settings( aspect ), waveDrawn, segments );
	dirty = false;
	++drawingsMade;
}

//---------------------------------------------------------------------------
// GL
//---------------------------------------------------------------------------
namespace
{
bool Allocate( GLuint& fbo, GLuint& texture, int& w, int& h, int width, int height, bool linear )
{
	if( fbo != 0 && w == width && h == height )
		return true;
	if( texture )
		glDeleteTextures( 1, &texture );
	if( fbo )
		glDeleteFramebuffers( 1, &fbo );
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	//Never leave a new buffer undefined: it is whatever memory the driver had.
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	w = width;
	h = height;
	if( !complete )
		diag::error( "a framebuffer is incomplete" );
	return complete;
}
} // namespace

bool ScrawlPlugin::EnsureTargets( int width, int height )
{
	const int qw = ( width + 3 ) / 4, qh = ( height + 3 ) / 4;
	bool ok = Allocate( strokeTarget.fbo, strokeTarget.texture, strokeTarget.width, strokeTarget.height, width, height, false );
	ok      = Allocate( glowA.fbo, glowA.texture, glowA.width, glowA.height, qw, qh, true ) && ok;
	ok      = Allocate( glowB.fbo, glowB.texture, glowB.width, glowB.height, qw, qh, true ) && ok;
	return ok;
}

void ScrawlPlugin::ReleaseTargets()
{
	for( Target* t : { &strokeTarget, &glowA, &glowB } )
	{
		if( t->texture )
			glDeleteTextures( 1, &t->texture );
		if( t->fbo )
			glDeleteFramebuffers( 1, &t->fbo );
		*t = Target {};
	}
}

void ScrawlPlugin::RenderDrawing( int width, int height )
{
	const double aspect = static_cast< double >( width ) / height;

	//The capsules, as the union of their coverage.
	glBindBuffer( GL_ARRAY_BUFFER, segmentVbo );
	if( segments.size() > segmentCapacity )
	{
		segmentCapacity = segments.size() + segments.size() / 2;
		glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( segmentCapacity * sizeof( Segment ) ), nullptr, GL_DYNAMIC_DRAW );
	}
	if( !segments.empty() )
		glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast< GLsizeiptr >( segments.size() * sizeof( Segment ) ), segments.data() );

	glBindFramebuffer( GL_FRAMEBUFFER, strokeTarget.fbo );
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( !segments.empty() )
	{
		glUseProgram( strokeProgram.GetGLID() );
		glUniform2f( strokeProgram.FindUniform( "Canvas" ), static_cast< float >( aspect ), 1.0f );
		glUniform1f( strokeProgram.FindUniform( "PixelSize" ), 1.0f / static_cast< float >( height ) );
		glEnable( GL_BLEND );
		glBlendEquation( GL_MAX );
		glBindVertexArray( segmentVao );
		glDrawArraysInstanced( GL_TRIANGLE_STRIP, 0, 4, static_cast< GLsizei >( segments.size() ) );
		glBindVertexArray( 0 );
		glBlendEquation( GL_FUNC_ADD );
		glDisable( GL_BLEND );
	}

	//The glow: a quarter-size copy, blurred across and then down.
	if( GlowFromParam( params[ PT_GLOW ] ) > 0.0 )
	{
		const int qw = glowA.width, qh = glowA.height;
		glBindFramebuffer( GL_FRAMEBUFFER, glowA.fbo );
		glViewport( 0, 0, qw, qh );
		glUseProgram( reduceProgram.GetGLID() );
		glUniform1i( reduceProgram.FindUniform( "Source" ), 0 );
		glUniform2i( reduceProgram.FindUniform( "SourceSize" ), width, height );
		bindUnit( 0, strokeTarget.texture );
		quad.Draw();

		const float sigma = static_cast< float >( Clamp( kGlowSigma * qh, 0.5, 8.0 ) );
		glUseProgram( blurProgram.GetGLID() );
		glUniform1i( blurProgram.FindUniform( "Source" ), 0 );
		glUniform2i( blurProgram.FindUniform( "SourceSize" ), qw, qh );
		glUniform1f( blurProgram.FindUniform( "Sigma" ), sigma );
		glBindFramebuffer( GL_FRAMEBUFFER, glowB.fbo );
		glUniform2f( blurProgram.FindUniform( "Direction" ), 1.0f, 0.0f );
		bindUnit( 0, glowA.texture );
		quad.Draw();
		glBindFramebuffer( GL_FRAMEBUFFER, glowA.fbo );
		glUniform2f( blurProgram.FindUniform( "Direction" ), 0.0f, 1.0f );
		bindUnit( 0, glowB.texture );
		quad.Draw();
		bindUnit( 0, 0 );
	}
	glUseProgram( 0 );
	drawn       = true;
	drawnWidth  = width;
	drawnHeight = height;
}

FFResult ScrawlPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || compositeProgram.GetGLID() == 0 || strokeProgram.GetGLID() == 0 )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = input ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height          = input ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	const double aspect = static_cast< double >( width ) / height;
	glDisable( GL_BLEND );

	const bool due = Advance( aspect );

	//Every buffer allocated before anything is bound to draw: allocating
	//binds, and a binding that is cleared on the way out unbinds the input.
	if( !EnsureTargets( width, height ) )
		return FF_FAIL;
	if( due || !drawn || drawnWidth != width || drawnHeight != height )
	{
		BuildDrawing( aspect );
		RenderDrawing( width, height );
	}

	//The composite, onto the host's framebuffer.
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	glUseProgram( compositeProgram.GetGLID() );
	float clipMax[ 2 ] = { 1.0f, 1.0f };
	if( input != nullptr && input->HardwareWidth > 0 && input->HardwareHeight > 0 )
	{
		clipMax[ 0 ] = static_cast< float >( input->Width ) / static_cast< float >( input->HardwareWidth );
		clipMax[ 1 ] = static_cast< float >( input->Height ) / static_cast< float >( input->HardwareHeight );
	}
	glUniform1i( compositeProgram.FindUniform( "Clip" ), 0 );
	glUniform1i( compositeProgram.FindUniform( "Strokes" ), 1 );
	glUniform1i( compositeProgram.FindUniform( "Glow" ), 2 );
	glUniform2f( compositeProgram.FindUniform( "ClipMax" ), clipMax[ 0 ], clipMax[ 1 ] );
	glUniform2f( compositeProgram.FindUniform( "GlowMax" ), static_cast< float >( width ) / static_cast< float >( 4 * glowA.width ),
	             static_cast< float >( height ) / static_cast< float >( 4 * glowA.height ) );
	glUniform1i( compositeProgram.FindUniform( "HasClip" ), input != nullptr ? 1 : 0 );
	glUniform1i( compositeProgram.FindUniform( "BackdropMode" ), OptionIndex( params[ PT_BACKDROP ], static_cast< int >( Backdrop::Count ) ) );
	glUniform1f( compositeProgram.FindUniform( "GlowGain" ), kGlowGain * static_cast< float >( GlowFromParam( params[ PT_GLOW ] ) ) );
	glUniform1f( compositeProgram.FindUniform( "Strength" ), isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	bindUnit( 0, input != nullptr ? input->Handle : strokeTarget.texture );
	bindUnit( 1, strokeTarget.texture );
	bindUnit( 2, glowA.texture );
	quad.Draw();
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
// Parameters
//---------------------------------------------------------------------------
FFResult ScrawlPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= ParamCount() )
		return FF_FAIL;
	if( index >= PT_ABOUT_TEXT && index < PT_SOURCE_COUNT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Hit acts on the rising edge, once.
	if( index == PT_HIT )
	{
		const bool down = value >= 0.5f;
		if( down && !hitHeld )
			++hitPresses;
		hitHeld = down;
	}
	const float previous = params[ index ];
	params[ index ]      = value;
	//Anything that changes how the drawing looks makes it again, at the same
	//drawing (so with the same hand); Hit and Mix change nothing drawn.
	if( value != previous && index != PT_HIT && index != PT_MIX )
		dirty = true;
	return FF_SUCCESS;
}

float ScrawlPlugin::GetFloatParameter( unsigned int index )
{
	return index < ParamCount() ? params[ index ] : 0.0f;
}

FFResult ScrawlPlugin::SetTextParameter( unsigned int index, const char* )
{
	//Display-only, and it MUST still succeed: instantiateGL pushes every
	//declared default back through the setters and deletes the instance if
	//one fails.
	return index == PT_ABOUT_TEXT ? FF_SUCCESS : FF_FAIL;
}

char* ScrawlPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string aboutLine = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutLine.c_str() );
	}
	static char empty[ 1 ] = { '\0' };
	return empty;
}

} // namespace scrawl
