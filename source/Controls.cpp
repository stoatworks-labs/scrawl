#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace scrawl
{
const char* const kDriveNames[]     = { "Auto", "Audio", "Beat" };
const char* const kDrawingsNames[]  = { "On Ones", "On Twos", "On Threes" };
const char* const kPlacementNames[] = { "Scatter", "Centre", "Speakers" };
const char* const kPaletteNames[]   = { "White", "Ink", "Neon", "Colour" };
const char* const kBackdropNames[]  = { "None", "Black", "Paper" };

namespace
{
float Clamp01( float v )
{
	return std::clamp( v, 0.0f, 1.0f );
}

double Geometric( float v, double lo, double hi )
{
	return lo * std::pow( hi / lo, static_cast< double >( Clamp01( v ) ) );
}

float InverseGeometric( double x, double lo, double hi )
{
	return static_cast< float >( std::clamp( std::log( x / lo ) / std::log( hi / lo ), 0.0, 1.0 ) );
}
} // namespace

double DrawingsPerSecond( Drawings d )
{
	switch( d )
	{
	case Drawings::Ones: return 24.0;
	case Drawings::Threes: return 8.0;
	default: return 12.0;
	}
}

double WobbleFromParam( float v )
{
	return Clamp01( v );
}

double LineWidthFromParam( float v )
{
	return Geometric( v, 0.0015, 0.02 );
}

float ParamFromLineWidth( double frameHeights )
{
	return InverseGeometric( frameHeights, 0.0015, 0.02 );
}

double SizeFromParam( float v )
{
	return Geometric( v, 0.5, 2.0 );
}

float ParamFromSize( double scale )
{
	return InverseGeometric( scale, 0.5, 2.0 );
}

double LingerFromParam( float v )
{
	return Geometric( v, 0.5, 2.0 );
}

double AmountFromParam( float v )
{
	return Clamp01( v );
}

double GlowFromParam( float v )
{
	return Clamp01( v );
}

double SensitivityFromParam( float v )
{
	return Clamp01( v );
}

int OptionIndex( float value, int count )
{
	//Arena sends the element's value, which here is its index (jackpot's and
	//needle's measurement); the range a host READS BACK is 0..1 whatever the
	//count, which is why nothing here trusts the declared range.
	if( count <= 1 || !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

} // namespace scrawl
