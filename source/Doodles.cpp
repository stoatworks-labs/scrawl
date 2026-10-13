#include "Doodles.h"

#include <algorithm>
#include <cmath>

namespace scrawl
{
const char* const kKindNames[] = { "rings", "burst", "bolt", "curl" };

namespace
{
/// Each kind's life at Linger 1, seconds.
constexpr double kLife[ kKinds ] = { 0.9, 0.42, 0.32, 1.0 };

/// Extra passes (Passes 2 and 3) go over the line again, a little thinner and
/// lighter, with their own hand error: the sketchy double line.
constexpr double kPassWidth[ 3 ] = { 1.0, 0.72, 0.55 };
constexpr float kPassAlpha[ 3 ]  = { 1.0f, 0.8f, 0.65f };

/// Neon: hot pink, cyan, yellow, lime, violet.
constexpr float kNeon[ 5 ][ 3 ] = {
	{ 1.0f, 0.25f, 0.65f }, { 0.2f, 0.9f, 1.0f }, { 1.0f, 0.92f, 0.2f }, { 0.55f, 1.0f, 0.3f }, { 0.68f, 0.45f, 1.0f },
};

/// Where the Speakers placement puts its two stacks: either side, low.
V2 Speaker( int side, double aspect )
{
	return { ( side != 0 ? 0.34 : -0.34 ) * aspect, -0.14 };
}

double EaseOut( double u )
{
	u = Clamp( u, 0.0, 1.0 );
	const double v = 1.0 - u;
	return 1.0 - v * v * v;
}

double Unit( uint32_t seed, uint32_t salt )
{
	return static_cast< double >( Hash( seed, salt ) ) * ( 1.0 / 4294967296.0 );
}

void Rings( const Doodle& d, double age, const ScoreSettings& s, const ScoreTestFlags& flags, std::vector< Stroke >& out )
{
	const double u      = age / Score::Life( Kind::Rings, s.linger );
	const double S      = d.scale;
	const double spread = EaseOut( u );
	const int sides = ( !d.fullCircle && d.extra ) ? 2 : 1;
	for( int arc = 0; arc < d.count * sides; ++arc )
	{
		const int j        = arc % d.count;
		const double facing = d.direction + ( arc >= d.count ? kPi : 0.0 );
		const double r     = S * ( 0.025 + 0.035 * j ) + S * ( 0.12 + 0.1 * d.strength ) * spread;
		const double lobes = 11.0 + 2.0 * j + static_cast< double >( d.seed % 5u );
		const double wig   = flags.noWiggle ? 0.0 : 0.045;
		const double psi   = 2.0 * kPi * Unit( d.seed, 40u + static_cast< uint32_t >( arc ) );
		Stroke st;
		double start = 0.0, span = 0.0, drift = 0.0;
		if( d.fullCircle )
		{
			//A hand-drawn circle does not close: the pen runs on past where it
			//started, and a little wide of it.
			span  = 2.0 * kPi + 0.35 + 0.3 * Unit( d.seed, 50u + static_cast< uint32_t >( j ) );
			start = d.direction + 1.3 * j;
			drift = 0.06;
		}
		else
		{
			span  = ( sides == 2 ? 1.5 : 1.9 ) + 0.35 * Unit( d.seed, 50u + static_cast< uint32_t >( arc ) );
			start = facing - 0.5 * span;
		}
		const int n = std::max( 24, static_cast< int >( std::ceil( span * r / 0.004 ) ) );
		st.points.reserve( static_cast< size_t >( n + 1 ) );
		for( int i = 0; i <= n; ++i )
		{
			const double along = span * i / n;
			const double th    = start + along;
			const double rr    = r * ( 1.0 + drift * along / ( 2.0 * kPi ) ) * ( 1.0 + wig * std::sin( lobes * th + psi ) );
			st.points.push_back( d.anchor + FromAngle( th ) * rr );
		}
		st.widthScale = std::pow( std::max( 0.0, 1.0 - u ), 0.8 );
		if( u > 0.55 )
		{
			st.dashes     = 1.0 - 0.75 * ( u - 0.55 ) / 0.45;
			st.dashPeriod = 0.05 * S;
		}
		st.to      = MinimumJerk( age / 0.08 );
		st.landing = 0.03;
		st.lift    = 0.06;
		st.extent  = r;
		out.push_back( std::move( st ) );
	}
}

void Burst( const Doodle& d, double age, const ScoreSettings& s, std::vector< Stroke >& out )
{
	const double u   = age / Score::Life( Kind::Burst, s.linger );
	const double R   = d.scale * ( 0.1 + 0.08 * d.strength );
	const double pop = MinimumJerk( age / 0.07 );
	for( int i = 0; i < d.count; ++i )
	{
		const uint32_t k = static_cast< uint32_t >( i );
		const double th  = d.direction + 2.0 * kPi * i / d.count + ( Unit( d.seed, 60u + k ) - 0.5 ) * 0.6 * 2.0 * kPi / d.count;
		const double len = ( ( i & 1 ) != 0 ? 0.6 : 1.0 ) * ( 0.85 + 0.3 * Unit( d.seed, 90u + k ) );
		const double b   = R * len * ( 0.5 + 0.5 * pop );
		const double a   = R * len * ( 0.42 + 0.58 * std::pow( Clamp( u, 0.0, 1.0 ), 1.6 ) );
		if( b - a < 1e-4 )
			continue;
		Stroke st;
		const V2 dir   = FromAngle( th );
		st.points      = { d.anchor + dir * a, d.anchor + dir * b };
		st.widthScale  = 1.15;
		st.landing     = 0.004;
		st.lift        = 0.025;
		st.extent      = R;
		out.push_back( std::move( st ) );
	}
	if( d.extra )
	{
		//The jagged star round a hard hit, closed the way a hand closes it: by
		//running on past the start.
		const int K   = 8 + static_cast< int >( d.seed % 3u );
		Stroke st;
		auto vertex = [ & ]( int i ) {
			const double rad = ( ( i & 1 ) == 0 ? 0.36 : 0.22 ) * R * ( 0.4 + 0.6 * pop );
			return d.anchor + FromAngle( d.direction + kPi * i / K ) * rad;
		};
		for( int i = 0; i <= 2 * K + 1; ++i )
			st.points.push_back( vertex( i ) );
		st.points.push_back( Lerp( vertex( 2 * K + 1 ), vertex( 2 * K + 2 ), 0.4 ) );
		st.from    = MinimumJerk( ( u - 0.45 ) / 0.55 );
		st.landing = 0.01;
		st.lift    = 0.03;
		st.extent  = 0.36 * R;
		out.push_back( std::move( st ) );
	}
}

void Bolt( const Doodle& d, double age, long long index, const ScoreSettings& s, std::vector< Stroke >& out )
{
	const double u = age / Score::Life( Kind::Bolt, s.linger );
	//Lightning flickers: in its second half it is only there every other drawing.
	if( u > 0.45 && ( ( static_cast< uint64_t >( index ) + d.seed ) & 1u ) != 0 )
		return;
	const double L   = d.scale * ( 0.3 + 0.25 * d.strength );
	const double amp = 0.05 * d.scale;
	const int k      = 6 + static_cast< int >( d.seed % 3u );
	const V2 dir     = FromAngle( d.direction );
	const V2 nrm     = Perp( dir );
	Stroke st;
	for( int i = 0; i <= k; ++i )
	{
		const double off = ( i == 0 || i == k ) ? 0.0 : ( ( i & 1 ) != 0 ? 1.0 : -1.0 ) * amp * ( 0.5 + 0.5 * Unit( d.seed, 70u + static_cast< uint32_t >( i ) ) );
		st.points.push_back( d.anchor + dir * ( L * i / k ) + nrm * off );
	}
	st.to         = MinimumJerk( age / 0.04 );
	st.widthScale = 1.3;
	st.landing    = 0.008;
	st.lift       = 0.05;
	st.extent     = L;
	if( d.extra )
	{
		const int f      = 2 + static_cast< int >( d.seed % static_cast< uint32_t >( k - 3 ) );
		const V2 fdir    = FromAngle( d.direction + ( ( d.seed & 4u ) != 0 ? 0.6 : -0.6 ) );
		const V2 fnrm    = Perp( fdir );
		Stroke fork;
		const V2 root    = st.points[ static_cast< size_t >( f ) ];
		for( int i = 0; i <= 3; ++i )
			fork.points.push_back( root + fdir * ( 0.35 * L * i / 3.0 ) + fnrm * ( i == 0 ? 0.0 : ( ( i & 1 ) != 0 ? 0.6 : -0.6 ) * amp ) );
		fork.to         = MinimumJerk( ( age - 0.02 ) / 0.04 );
		fork.widthScale = 0.9;
		fork.landing    = 0.004;
		fork.lift       = 0.04;
		fork.extent     = 0.35 * L;
		out.push_back( std::move( st ) );
		if( fork.to > 0.0 )
			out.push_back( std::move( fork ) );
		return;
	}
	out.push_back( std::move( st ) );
}

void Curl( const Doodle& d, double age, const ScoreSettings& s, std::vector< Stroke >& out )
{
	const double life   = Score::Life( Kind::Curl, s.linger );
	const double S      = d.scale;
	const V2 origin     = d.anchor + FromAngle( d.direction ) * ( 0.1 * S * age );
	const double handed = ( d.seed & 8u ) != 0 ? 1.0 : -1.0;
	Stroke st;
	const int N = 180;
	if( d.variant == 0 )
	{
		//A prolate trochoid: a pen running forward while it goes round, loops
		//because the radius is bigger than the forward step per turn.
		const double n  = static_cast< double >( d.count );
		const double Lc = 0.3 * S, b = 0.05 * S, a = Lc / ( 2.0 * kPi * n );
		for( int i = 0; i <= N; ++i )
		{
			const double phi = 2.0 * kPi * n * i / N;
			const V2 local   = { a * phi - b * std::sin( phi ), handed * b * ( 1.0 - std::cos( phi ) ) };
			st.points.push_back( origin + Rotate( local, d.direction ) );
		}
	}
	else
	{
		//A spiral, drawn from the outside in.
		const double rmax = 0.09 * S, phi0 = 2.0 * kPi * Unit( d.seed, 80u );
		for( int i = 0; i <= N; ++i )
		{
			const double v = static_cast< double >( i ) / N;
			st.points.push_back( origin + FromAngle( phi0 + handed * 2.0 * kPi * 2.25 * v ) * ( rmax * ( 1.0 - 0.88 * v ) ) );
		}
	}
	st.to         = MinimumJerk( age / ( 0.3 * s.linger ) );
	st.from       = MinimumJerk( ( age - 0.55 * life ) / ( 0.45 * life ) );
	st.widthScale = 0.85;
	st.landing    = 0.02;
	st.lift       = 0.05;
	st.extent     = 0.15 * S;
	out.push_back( std::move( st ) );
}
} // namespace

double Score::Life( Kind kind, double linger )
{
	return kLife[ static_cast< int >( kind ) ] * linger;
}

void Score::Clear()
{
	doodles.clear();
}

void Score::Spawn( Kind kind, double now, double strength, const ScoreSettings& s )
{
	const double amount = s.amount[ static_cast< size_t >( kind ) ];
	if( !( amount > 0.0 ) )
		return;
	strength = Clamp( strength, 0.0, 1.0 );
	Rng rng( Hash( s.seed, spawned, 0x5C4A3u ) );

	Doodle d;
	d.kind     = kind;
	d.birth    = now;
	d.seed     = Hash( s.seed, spawned );
	d.strength = strength;
	d.scale    = s.size * ( 0.65 + 0.5 * strength ) * ( 0.7 + 0.5 * amount );
	d.colour   = static_cast< int >( rng.Below( 5 ) );

	const double ax   = 0.4 * s.aspect, ay = 0.36;
	const V2 scatter  = { rng.Range( -ax, ax ), rng.Range( -ay, ay ) };
	const double turn = rng.Range( 0.0, 2.0 * kPi );
	const int side    = static_cast< int >( rng.Below( 2 ) );
	switch( kind )
	{
	case Kind::Rings:
		d.count     = 1 + static_cast< int >( std::lround( 2.0 * amount ) );
		d.direction = turn;
		if( s.placement == Placement::Speakers )
		{
			//The stacks take it in turns, and their arcs face the room.
			const int from = static_cast< int >( ringsSide++ & 1u );
			d.anchor       = Speaker( from, s.aspect );
			d.direction    = from != 0 ? kPi : 0.0;
			d.fullCircle   = false;
		}
		else if( s.placement == Placement::Centre )
			d.anchor = s.centre;
		else
		{
			//Scattered: sound marks either side of a point, (( )).
			d.anchor     = scatter;
			d.fullCircle = false;
			d.extra      = true;
		}
		break;
	case Kind::Burst:
		d.count     = 8 + static_cast< int >( std::lround( 6.0 * amount ) );
		d.extra     = amount >= 0.5 && strength >= 0.5;
		d.direction = turn;
		if( s.placement == Placement::Centre )
			d.anchor = s.centre + FromAngle( rng.Range( 0.0, 2.0 * kPi ) ) * ( rng.Range( 0.08, 0.2 ) * s.size );
		else if( s.placement == Placement::Speakers )
			d.anchor = Speaker( side, s.aspect ) + V2 { rng.Range( -0.06, 0.06 ), rng.Range( 0.1, 0.2 ) };
		else
			d.anchor = scatter;
		break;
	case Kind::Bolt:
		d.extra = amount >= 0.5;
		if( s.placement == Placement::Speakers )
		{
			d.anchor    = Speaker( side, s.aspect ) + V2 { 0.0, 0.1 };
			d.direction = ( side != 0 ? kPi - 0.25 : 0.25 ) + rng.Range( -0.45, 0.45 ) * ( side != 0 ? -1.0 : 1.0 );
		}
		else if( s.placement == Placement::Centre )
		{
			d.direction = turn;
			d.anchor    = s.centre + FromAngle( turn ) * ( 0.05 * s.size );
		}
		else
		{
			d.direction = turn;
			d.anchor    = scatter;
		}
		break;
	case Kind::Curl:
	default:
		d.count     = 2 + static_cast< int >( rng.Below( 2 ) );
		d.variant   = rng.Below( 3 ) == 0 ? 1 : 0;
		d.direction = 0.5 * kPi + rng.Range( -0.7, 0.7 );
		if( s.placement == Placement::Centre )
			d.anchor = s.centre + FromAngle( rng.Range( 0.0, 2.0 * kPi ) ) * ( rng.Range( 0.05, 0.18 ) * s.size );
		else if( s.placement == Placement::Speakers )
			d.anchor = Speaker( side, s.aspect ) + V2 { rng.Range( -0.05, 0.05 ), 0.12 };
		else
			d.anchor = scatter;
		break;
	}

	++spawned;
	doodles.push_back( d );
	if( doodles.size() > kMaxDoodles )
		doodles.erase( doodles.begin() );
}

void Score::Expire( double now, const ScoreSettings& s )
{
	if( flags.frozenAge >= 0.0 )
		return;
	doodles.erase( std::remove_if( doodles.begin(), doodles.end(),
	                               [ & ]( const Doodle& d ) { return now - d.birth >= Life( d.kind, s.linger ); } ),
	               doodles.end() );
}

void Score::Strokes( const Doodle& d, double age, long long index, const ScoreSettings& s, const ScoreTestFlags& flags,
                     std::vector< Stroke >& out )
{
	switch( d.kind )
	{
	case Kind::Rings: Rings( d, age, s, flags, out ); break;
	case Kind::Burst: Burst( d, age, s, out ); break;
	case Kind::Bolt: Bolt( d, age, index, s, out ); break;
	case Kind::Curl:
	default: Curl( d, age, s, out ); break;
	}
}

bool Score::WaveStroke( const ScoreSettings& s, const WaveShape& wave, double time, const ScoreTestFlags& flags, Stroke& out )
{
	if( !( s.wave > 0.0 ) )
		return false;
	const double pressure = flags.flatWave ? 1.0 : Step( 0.02, 0.12, wave.level );
	if( pressure <= 0.0 )
		return false;
	//Six sines across the frame, one per spectrum group, each drifting at its
	//own rate: the bass a slow swell, the top a fast fizz.
	constexpr double kCycles[ 6 ] = { 1.5, 2.5, 4.0, 6.5, 10.0, 15.0 };
	constexpr double kDrift[ 6 ]  = { 1.1, -1.7, 2.3, -2.9, 3.7, -4.3 };
	const double H  = flags.flatWave ? 0.0 : 0.14 * s.wave * s.size;
	const double y0 = s.placement == Placement::Centre ? s.centre.y : ( s.placement == Placement::Speakers ? -0.14 : 0.0 );
	const double x0 = -0.5 * s.aspect - 0.06, x1 = 0.5 * s.aspect + 0.06;
	const int n     = static_cast< int >( std::ceil( ( x1 - x0 ) / 0.005 ) );
	out             = Stroke {};
	out.points.reserve( static_cast< size_t >( n + 1 ) );
	for( int i = 0; i <= n; ++i )
	{
		const double x = x0 + ( x1 - x0 ) * i / n;
		double y       = 0.0;
		for( int j = 0; j < 6; ++j )
			y += wave.levels[ static_cast< size_t >( j ) ]
			     * std::sin( 2.0 * kPi * kCycles[ j ] * x / s.aspect + 2.0 * kPi * Unit( s.seed, 100u + static_cast< uint32_t >( j ) ) + kDrift[ j ] * time );
		out.points.push_back( { x, y0 + 0.5 * H * y } );
	}
	out.widthScale = pressure;
	out.landing    = 0.03;
	out.lift       = 0.03;
	out.extent     = 0.15;
	return true;
}

void Score::Ink( const ScoreSettings& s, int colour, float rgb[ 3 ] )
{
	switch( s.palette )
	{
	case Palette::Ink:
		rgb[ 0 ] = 0.05f;
		rgb[ 1 ] = 0.05f;
		rgb[ 2 ] = 0.06f;
		return;
	case Palette::Neon:
		for( int c = 0; c < 3; ++c )
			rgb[ c ] = kNeon[ ( ( colour % 5 ) + 5 ) % 5 ][ c ];
		return;
	case Palette::Colour:
		for( int c = 0; c < 3; ++c )
			rgb[ c ] = s.colour[ c ];
		return;
	case Palette::White:
	default:
		rgb[ 0 ] = rgb[ 1 ] = rgb[ 2 ] = 1.0f;
		return;
	}
}

void Score::Build( double time, double exposure, long long index, const ScoreSettings& s, const WaveShape& wave,
                   std::vector< Segment >& out ) const
{
	out.clear();
	const int passes = std::clamp( s.passes, 1, 3 );
	std::vector< Stroke > strokes;
	auto draw = [ & ]( const Stroke& st, uint32_t identity, V2 pivot, int colour ) {
		float rgb[ 3 ];
		Ink( s, colour, rgb );
		for( int p = 0; p < passes; ++p )
		{
			hand::Options o;
			o.wobble          = s.wobble;
			o.amplitudeScale  = flags.handScale;
			o.pen.width       = s.lineWidth * kPassWidth[ p ];
			o.pen.alpha       = kPassAlpha[ p ];
			o.pen.red         = rgb[ 0 ];
			o.pen.green       = rgb[ 1 ];
			o.pen.blue        = rgb[ 2 ];
			o.character       = identity;
			const uint32_t pass = Hash( identity, static_cast< uint32_t >( p ) );
			o.drawing         = flags.fixedHand ? pass : Hash( pass, static_cast< uint32_t >( index ) );
			o.pivot           = pivot;
			hand::Draw( st, o, out );
		}
	};

	for( const Doodle& d : doodles )
	{
		//A microsecond's grace: a doodle born on a drawing's own frame is in that
		//drawing, whichever way k / rate and the frame's time happened to round.
		if( d.birth > time + 1e-6 )
			continue;
		double age = std::max( 0.0, time - d.birth ) + 0.5 * exposure;
		if( flags.frozenAge >= 0.0 )
			age = flags.frozenAge;
		else if( age >= Life( d.kind, s.linger ) )
			continue;
		strokes.clear();
		Strokes( d, age, index, s, flags, strokes );
		for( size_t i = 0; i < strokes.size(); ++i )
			draw( strokes[ i ], Hash( d.seed, static_cast< uint32_t >( i ) ), d.anchor, d.colour );
	}

	Stroke line;
	if( WaveStroke( s, wave, time, flags, line ) )
		draw( line, Hash( s.seed, 0x7A7Eu ), V2 { 0.0, line.points.front().y }, static_cast< int >( s.seed % 5u ) );
}

} // namespace scrawl
