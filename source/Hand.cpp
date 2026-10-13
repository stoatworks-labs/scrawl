#include "Hand.h"

#include <algorithm>
#include <cmath>

namespace scrawl::hand
{
namespace
{
constexpr double kRawWeight[ 3 ] = { 0.78, 0.52, 0.34 };

double Unit( uint32_t a, uint32_t b )
{
	return static_cast< double >( Hash( a, b ) ) * ( 1.0 / 4294967296.0 );
}
} // namespace

double Amplitude( double wobble, double extent )
{
	return Clamp( wobble, 0.0, 1.0 ) * ( 0.003 + 0.02 * std::max( 0.0, extent ) );
}

double Weight( int k )
{
	const double norm = std::sqrt( kRawWeight[ 0 ] * kRawWeight[ 0 ] + kRawWeight[ 1 ] * kRawWeight[ 1 ] + kRawWeight[ 2 ] * kRawWeight[ 2 ] );
	return kRawWeight[ k ] / norm;
}

void Resample( const std::vector< V2 >& in, double spacing, std::vector< V2 >& out, std::vector< double >& s )
{
	out.clear();
	s.clear();
	if( in.empty() )
		return;
	double total = 0.0;
	for( size_t i = 1; i < in.size(); ++i )
		total += Length( in[ i ] - in[ i - 1 ] );
	if( total <= 0.0 )
	{
		out.push_back( in.front() );
		s.push_back( 0.0 );
		return;
	}

	//Even steps, the last one exactly at the end. Corners are kept: a sample
	//lands on every input vertex the step would otherwise cut across, so a
	//bolt's zigzag stays a zigzag instead of being rounded off.
	const int steps = std::max( 1, static_cast< int >( std::ceil( total / spacing ) ) );
	const double step = total / steps;
	out.push_back( in.front() );
	s.push_back( 0.0 );
	double travelled = 0.0;  //arc length at the start of the current input segment
	double next      = step; //arc length of the next even sample
	for( size_t i = 1; i < in.size(); ++i )
	{
		const V2 a        = in[ i - 1 ];
		const V2 b        = in[ i ];
		const double len  = Length( b - a );
		if( len <= 0.0 )
			continue;
		while( next < travelled + len - 1e-12 && next < total - 1e-12 )
		{
			out.push_back( Lerp( a, b, ( next - travelled ) / len ) );
			s.push_back( next );
			next += step;
		}
		travelled += len;
		//The vertex itself, unless an even sample already sits on it.
		if( i + 1 < in.size() && std::fabs( s.back() - travelled ) > 1e-9 )
		{
			out.push_back( b );
			s.push_back( travelled );
		}
	}
	if( std::fabs( s.back() - total ) > 1e-12 )
	{
		out.push_back( in.back() );
		s.push_back( total );
	}
	else
		out.back() = in.back();
}

int Draw( const Stroke& stroke, const Options& o, std::vector< Segment >& out, std::vector< V2 >* centreline )
{
	std::vector< V2 > p;
	std::vector< double > s;
	Resample( stroke.points, kSpacing, p, s );
	if( p.size() < 2 )
		return 0;
	const double L = s.back();
	const double from = Clamp( stroke.from, 0.0, 1.0 ) * L;
	const double to   = Clamp( stroke.to, 0.0, 1.0 ) * L;
	if( to - from <= 1e-9 )
		return 0;

	//The hand for this drawing.
	const double wobble = Clamp( o.wobble, 0.0, 1.0 );
	const double A      = Amplitude( wobble, stroke.extent ) * o.amplitudeScale;
	double lambda[ 3 ], phase[ 3 ];
	for( int k = 0; k < 3; ++k )
	{
		lambda[ k ] = kWavelength[ k ] * ( 0.8 + 0.4 * Unit( o.character, static_cast< uint32_t >( 11 + k ) ) );
		phase[ k ]  = 2.0 * kPi * Unit( o.drawing, static_cast< uint32_t >( 21 + k ) );
	}
	//Registration: uniform in a disc of radius kRegistration * A, and a twist
	//that moves the gesture's far end by about a quarter of A.
	const double regR     = kRegistration * A * std::sqrt( Unit( o.drawing, 31u ) );
	const double regAngle = 2.0 * kPi * Unit( o.drawing, 32u );
	const V2 offset       = FromAngle( regAngle ) * regR;
	const double twist    = ( 2.0 * Unit( o.drawing, 33u ) - 1.0 ) * 0.25 * A / std::max( stroke.extent, 0.05 );
	const double wander   = kPressureWander * std::min( 1.0, 2.0 * wobble );
	const double pPhase   = 2.0 * kPi * Unit( o.drawing, 34u );
	const double dashPhase = Unit( o.character, 35u );

	const double landing = std::min( stroke.landing, 0.25 * L );
	const double lift    = std::min( stroke.lift, 0.35 * L );

	auto displaced = [ & ]( size_t i ) {
		//The normal from the neighbours: at a corner it is the bisector, so a
		//corner moves as a corner.
		const V2 a     = p[ i == 0 ? 0 : i - 1 ];
		const V2 b     = p[ i + 1 < p.size() ? i + 1 : i ];
		V2 t           = b - a;
		const double l = Length( t );
		t              = l > 0.0 ? t * ( 1.0 / l ) : V2 { 1.0, 0.0 };
		double d       = 0.0;
		for( int k = 0; k < 3; ++k )
				d += Weight( k ) * std::sin( 2.0 * kPi * s[ i ] / lambda[ k ] + phase[ k ] );
		const V2 q = o.pivot + Rotate( p[ i ] - o.pivot, twist ) + offset;
		return q + Perp( t ) * ( A * d );
	};
	auto pressure = [ & ]( double at ) {
		const double land = landing > 0.0 ? Step( 0.0, landing, at ) : 1.0;
		const double off  = lift > 0.0 ? Step( L, L - lift, at ) : 1.0;
		return land * off * ( 1.0 + wander * std::sin( 2.0 * kPi * at / kPressureWavelength + pPhase ) );
	};

	//The drawn part's points: the displaced samples inside [from, to], and the
	//two ends interpolated exactly where the pen started and has got to.
	std::vector< V2 > q;
	std::vector< double > qs;
	q.reserve( p.size() );
	qs.reserve( p.size() );
	for( size_t i = 0; i + 1 < p.size(); ++i )
	{
		const double s0 = s[ i ], s1 = s[ i + 1 ];
		if( s1 < from || s0 > to )
			continue;
		const V2 d0 = displaced( i ), d1 = displaced( i + 1 );
		const double span = s1 - s0;
		if( q.empty() )
		{
			const double t = span > 0.0 ? Clamp( ( from - s0 ) / span, 0.0, 1.0 ) : 0.0;
			q.push_back( Lerp( d0, d1, t ) );
			qs.push_back( s0 + span * t );
		}
		if( s1 <= to )
		{
			q.push_back( d1 );
			qs.push_back( s1 );
		}
		else
		{
			const double t = span > 0.0 ? Clamp( ( to - s0 ) / span, 0.0, 1.0 ) : 0.0;
			q.push_back( Lerp( d0, d1, t ) );
			qs.push_back( s0 + span * t );
			break;
		}
	}
	if( centreline != nullptr )
		*centreline = q;

	int added             = 0;
	const double halfWidth = 0.5 * o.pen.width * stroke.widthScale;
	const float alpha      = static_cast< float >( Clamp( o.pen.alpha * stroke.alpha, 0.0, 1.0 ) );
	for( size_t i = 0; i + 1 < q.size(); ++i )
	{
		if( stroke.dashes < 1.0 )
		{
			const double mid = 0.5 * ( qs[ i ] + qs[ i + 1 ] ) / std::max( stroke.dashPeriod, 1e-6 ) + dashPhase;
			if( mid - std::floor( mid ) >= stroke.dashes )
				continue;
		}
		const double r0 = halfWidth * pressure( qs[ i ] );
		const double r1 = halfWidth * pressure( qs[ i + 1 ] );
		if( r0 < 1e-7 && r1 < 1e-7 )
			continue;
		out.push_back( Segment { static_cast< float >( q[ i ].x ), static_cast< float >( q[ i ].y ),
		                         static_cast< float >( q[ i + 1 ].x ), static_cast< float >( q[ i + 1 ].y ),
		                         static_cast< float >( std::max( 0.0, r0 ) ), static_cast< float >( std::max( 0.0, r1 ) ),
		                         o.pen.red, o.pen.green, o.pen.blue, alpha } );
		++added;
	}
	return added;
}

} // namespace scrawl::hand
