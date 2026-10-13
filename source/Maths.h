#pragma once

#include <cmath>
#include <cstdint>

/**
    The small things: a 2D vector, integer hashing and a seeded generator.

    Randomness is PCG integer hashing, never `fract(sin(...))` -- the fleet's
    rule, because a driver's `sin` differs between GPUs and a C++ mirror can
    then agree with neither. Nothing random happens on the GPU here at all:
    every drawing is made on the CPU and the shaders only rasterise it.
*/
namespace scrawl
{
constexpr double kPi = 3.14159265358979323846;

struct V2
{
	double x = 0.0, y = 0.0;
};

inline V2 operator+( V2 a, V2 b )
{
	return { a.x + b.x, a.y + b.y };
}
inline V2 operator-( V2 a, V2 b )
{
	return { a.x - b.x, a.y - b.y };
}
inline V2 operator*( V2 a, double s )
{
	return { a.x * s, a.y * s };
}
inline V2 operator*( double s, V2 a )
{
	return { a.x * s, a.y * s };
}
inline double Dot( V2 a, V2 b )
{
	return a.x * b.x + a.y * b.y;
}
inline double Length( V2 a )
{
	return std::sqrt( Dot( a, a ) );
}
inline V2 Perp( V2 a )
{
	return { -a.y, a.x };
}
inline V2 FromAngle( double radians )
{
	return { std::cos( radians ), std::sin( radians ) };
}
inline V2 Lerp( V2 a, V2 b, double t )
{
	return a + ( b - a ) * t;
}
inline V2 Rotate( V2 a, double radians )
{
	const double c = std::cos( radians ), s = std::sin( radians );
	return { a.x * c - a.y * s, a.x * s + a.y * c };
}

/// PCG's output permutation as a hash (Jarzynski and Olano, "Hash Functions
/// for GPU Rendering", 2020).
inline uint32_t Hash( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

inline uint32_t Hash( uint32_t a, uint32_t b )
{
	return Hash( a ^ Hash( b + 0x9E3779B9u ) );
}

inline uint32_t Hash( uint32_t a, uint32_t b, uint32_t c )
{
	return Hash( Hash( a, b ), c );
}

/// A small seeded generator: PCG32 (O'Neill), with Lemire's unbiased
/// multiply-shift for ranges.
class Rng
{
public:
	explicit Rng( uint32_t seed )
	{
		state = 0u;
		Next();
		state += 0x853C49E6748FEA9BULL ^ static_cast< uint64_t >( Hash( seed ) ) << 32 ^ seed;
		Next();
	}

	uint32_t Next()
	{
		const uint64_t old = state;
		state              = old * 6364136223846793005ULL + 1442695040888963407ULL;
		const uint32_t xorshifted = static_cast< uint32_t >( ( ( old >> 18u ) ^ old ) >> 27u );
		const uint32_t rot        = static_cast< uint32_t >( old >> 59u );
		return ( xorshifted >> rot ) | ( xorshifted << ( ( 32u - rot ) & 31u ) );
	}

	/// Uniform in [0, 1), 32 bits of it.
	double Uniform()
	{
		return static_cast< double >( Next() ) * ( 1.0 / 4294967296.0 );
	}

	double Range( double lo, double hi )
	{
		return lo + ( hi - lo ) * Uniform();
	}

	/// Uniform in [0, n), unbiased.
	uint32_t Below( uint32_t n )
	{
		if( n == 0 )
			return 0;
		uint64_t m        = static_cast< uint64_t >( Next() ) * n;
		uint32_t low      = static_cast< uint32_t >( m );
		if( low < n )
		{
			const uint32_t threshold = static_cast< uint32_t >( -n ) % n;
			while( low < threshold )
			{
				m   = static_cast< uint64_t >( Next() ) * n;
				low = static_cast< uint32_t >( m );
			}
		}
		return static_cast< uint32_t >( m >> 32 );
	}

	/// -1 or +1.
	double Sign()
	{
		return ( Next() & 1u ) ? 1.0 : -1.0;
	}

private:
	uint64_t state = 0;
};

/// Minimum-jerk position for a reach that takes unit time (Flash and Hogan,
/// 1985): 0 at 0, 1 at 1, with zero velocity and acceleration at both ends.
/// It is how a hand moves a pen from one place to another.
inline double MinimumJerk( double t )
{
	if( t <= 0.0 )
		return 0.0;
	if( t >= 1.0 )
		return 1.0;
	return t * t * t * ( 10.0 + t * ( -15.0 + 6.0 * t ) );
}

inline double Clamp( double v, double lo, double hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

/// Hermite step from 0 at `a` to 1 at `b`; `a` may be greater than `b` (the
/// step then falls). GLSL's smoothstep is undefined that way round.
inline double Step( double a, double b, double x )
{
	if( a == b )
		return x < a ? 0.0 : 1.0;
	const double t = Clamp( ( x - a ) / ( b - a ), 0.0, 1.0 );
	return t * t * ( 3.0 - 2.0 * t );
}

} // namespace scrawl
