/**
    The C++ half of the browser demo's cross-check: the REAL plugin class,
    ScrawlPlugin, driven without GL through scenario.mjs's frames, exactly as
    sctest's Bench drives it (Advance, then BuildDrawing when a drawing is due).
    run.mjs drives the page's port through the same frames and prints the same
    lines; compare.py diffs them.

        printer SEGMENTS.bin < scenario.txt > events.txt

    The Audio buffer is fed with the harness's own demoSpectrum, spliced out of
    tools/sctest/main.cpp by crosscheck.sh into demo_band.h, so the band the
    page ports is checked against the text the harness runs, not a third copy.

    Printed: every parameter's name and default at each new instance; a line a
    frame (due, the drawing, the drive, the doodle count, the clock, each band's
    onset and strength, the level); a line for each doodle spawned (every field
    of it); and a line for each drawing made, whose segments go to SEGMENTS.bin
    as the ten floats a segment is. Floats and doubles print as their bits.
*/

#include "Scrawl.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace scrawl;

namespace
{
constexpr double kFps = 60.0;
#include "demo_band.h"

std::string F( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, 4 );
	char b[ 16 ];
	std::snprintf( b, sizeof( b ), "f:%08x", bits );
	return b;
}

std::string D( double v )
{
	uint64_t bits;
	std::memcpy( &bits, &v, 8 );
	char b[ 24 ];
	std::snprintf( b, sizeof( b ), "d:%016llx", static_cast< unsigned long long >( bits ) );
	return b;
}
} // namespace

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		std::fprintf( stderr, "usage: printer SEGMENTS.bin < scenario.txt\n" );
		return 2;
	}
	FILE* segments = std::fopen( argv[ 1 ], "wb" );
	if( segments == nullptr )
		return 1;

	std::unique_ptr< ScrawlPlugin > plugin;
	int frame        = 0;
	uint32_t spawned = 0;
	std::string line;
	while( std::getline( std::cin, line ) )
	{
		std::istringstream in( line );
		std::string tag;
		in >> tag;
		if( tag == "new" )
		{
			int effect = 0;
			in >> effect;
			plugin = std::make_unique< ScrawlPlugin >( effect != 0 );
			plugin->SetClockScaleForTest( 1.0 );
			frame   = 0;
			spawned = 0;
			std::printf( "new %d\n", effect );
			for( unsigned int id = 0; id < plugin->ParamCount(); ++id )
			{
				if( id >= PT_ABOUT_TEXT && id < PT_SOURCE_COUNT )
					continue;
				std::printf( "P %u %s %s\n", id, plugin->GetParamName( id ), F( plugin->GetFloatParameter( id ) ).c_str() );
			}
			continue;
		}
		if( tag != "f" || !plugin )
			continue;

		double time = 0.0, aspect = 0.0;
		std::string audio;
		int band = 0;
		in >> time >> audio >> band >> aspect;
		std::string pair;
		while( in >> pair )
		{
			const size_t eq = pair.find( '=' );
			plugin->SetFloatParameter( static_cast< unsigned int >( std::stoul( pair.substr( 0, eq ) ) ),
			                           static_cast< float >( std::strtod( pair.c_str() + eq + 1, nullptr ) ) );
		}
		float bins[ audio::kBins ] = {};
		if( audio != "none" )
			demoSpectrum( band, bins, kDemoPeriod, true, audio == "ghost" );
		for( int i = 0; i < audio::kBins; ++i )
			plugin->SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( i ), bins[ i ] );
		plugin->SetTime( time );
		const bool due = plugin->Advance( aspect );
		if( due )
			plugin->BuildDrawing( aspect );

		const audio::Analyser& ears = plugin->CurrentAnalyser();
		const Score& score          = plugin->CurrentScore();
		std::printf( "F %d %d %lld %d %zu %u %s %d %s %d %s %d %s %s\n", frame, due ? 1 : 0, plugin->DrawingIndex(),
		             plugin->AudioDriving() ? 1 : 0, score.Doodles().size(), score.Spawned(), D( plugin->Clock() ).c_str(),
		             ears.Low().fired ? 1 : 0, F( ears.Low().strength ).c_str(), ears.Mid().fired ? 1 : 0,
		             F( ears.Mid().strength ).c_str(), ears.High().fired ? 1 : 0, F( ears.High().strength ).c_str(),
		             F( ears.Level() ).c_str() );
		const size_t fresh = std::min< size_t >( score.Spawned() - spawned, score.Doodles().size() );
		for( size_t i = score.Doodles().size() - fresh; i < score.Doodles().size(); ++i )
		{
			const Doodle& d = score.Doodles()[ i ];
			std::printf( "N %d %s %u %s %s %s %s %s %d %d %d %d %d\n", static_cast< int >( d.kind ), D( d.birth ).c_str(), d.seed,
			             D( d.anchor.x ).c_str(), D( d.anchor.y ).c_str(), D( d.direction ).c_str(), D( d.scale ).c_str(),
			             D( d.strength ).c_str(), d.colour, d.fullCircle ? 1 : 0, d.count, d.extra ? 1 : 0, d.variant );
		}
		spawned = score.Spawned();
		if( due )
		{
			const auto& segs = plugin->CurrentSegments();
			std::printf( "D %lld %zu\n", plugin->DrawingIndex(), segs.size() );
			if( !segs.empty() )
				std::fwrite( segs.data(), sizeof( Segment ), segs.size(), segments );
		}
		++frame;
	}
	std::fclose( segments );
	return 0;
}
