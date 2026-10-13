#include "Audio.h"

#include <algorithm>
#include <cmath>

namespace scrawl::audio
{
namespace
{
/// A one-pole coefficient for a time constant, given the frame length. 1 --
/// snap to the target -- for a zero time constant or a frame of no length.
float coefficient( float dt, float tau )
{
	if( tau <= 1e-4f || dt <= 0.0f )
		return 1.0f;
	return 1.0f - std::exp( -dt / tau );
}

/// The floor under the normalising peak. Without it, silence divides a level of
/// zero by a peak of zero and the first faint sound after it reads as full
/// scale -- so the gap between two tracks would fill with doodles.
constexpr float kPeakFloor = 0.02f;

/// The peak's decay: a bar of quiet does not re-normalise the track, a new
/// source is not a minute of nothing.
constexpr float kPeakTau = 3.0f;

/// No onset within this of the last in the same band. 80 ms is 750 bpm in
/// straight quavers, and it stops one ragged hit firing three times.
constexpr float kRefractory = 0.08f;

/// The absolute floor on flux: the adaptive one alone finds onsets in noise.
constexpr float kFluxFloor = 0.004f;

/// Below this a bin is silence (Present()).
constexpr float kNoiseFloor = 1e-5f;

/// The wave's groups: roughly an octave each if the bins are linear, which
/// they may not be (Audio.h).
constexpr int kWaveEdges[ kWaveBands + 1 ] = { 0, 1, 2, 4, 8, 16, 64 };

float clamp01( float v )
{
	return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}
} // namespace

void Analyser::Reset()
{
	binLevel.fill( 0.0f );
	binPrevious.fill( 0.0f );
	bands = {};
	full  = {};
	wave.fill( 0.0f );
	sinceSound = 1e9f;
	primed     = false;
}

void Analyser::UpdateBand( Band& band, float sum, float rawSum, float rise, int count, float dt, const Settings& settings )
{
	const float n = static_cast< float >( std::max( count, 1 ) );
	band.level    = sum / n;
	band.raw      = rawSum / n;
	band.flux     = rise / n;
	if( !primed && settings.prime )
		band.flux = 0.0f;

	band.peak = std::max( band.raw, band.peak - ( band.peak - kPeakFloor ) * coefficient( dt, kPeakTau ) );
	band.peak = std::max( band.peak, kPeakFloor );
	band.fluxMean += ( band.flux - band.fluxMean ) * coefficient( dt, 1.0f );

	//Higher sensitivity, lower bar. A compressed master has almost no flux and
	//a live kit nothing but, so the span is wide.
	const float margin = 2.5f - 2.35f * clamp01( settings.sensitivity );
	const float bar    = std::max( kFluxFloor, band.fluxMean * ( 1.0f + margin ) );

	band.fired      = false;
	band.refractory = std::max( 0.0f, band.refractory - dt );
	if( band.refractory <= 0.0f && band.flux > bar )
	{
		band.fired      = true;
		band.refractory = kRefractory;
		band.strength   = clamp01( band.raw / band.peak );
		++band.onsets;
	}
}

void Analyser::Update( const float* bins, int count, float dt, const Settings& settings )
{
	const int n        = std::clamp( count, 0, kBins );
	const float attack  = coefficient( dt, settings.attackSeconds );
	const float release = coefficient( dt, settings.releaseSeconds );
	const int lowEnd    = std::clamp( settings.bassBins, 1, kBins - 2 );
	const int highFrom  = std::clamp( settings.trebleFrom, lowEnd + 1, kBins - 1 );

	float sum[ kBands ] = {}, rawSum[ kBands ] = {}, rise[ kBands ] = {};
	int counted[ kBands ] = {};
	float total = 0.0f, totalRaw = 0.0f, totalRise = 0.0f, loudest = 0.0f;
	for( int i = 0; i < kBins; ++i )
	{
		const float value = i < n && bins != nullptr ? bins[ i ] : 0.0f;
		loudest           = std::max( loudest, value );
		//sqrt because bin magnitudes bunch hard against zero: a spectrum used
		//raw answers the kick and nothing else in the mix.
		const float raw = std::sqrt( std::max( 0.0f, value ) );

		//Flux between two RAW frames, never against the envelope: low-passing
		//a signal before asking where its corners are asks the wrong signal.
		const float up   = std::max( 0.0f, raw - binPrevious[ static_cast< size_t >( i ) ] );
		binPrevious[ static_cast< size_t >( i ) ] = raw;

		float& level = binLevel[ static_cast< size_t >( i ) ];
		level += ( raw - level ) * ( raw >= level ? attack : release );

		const int b = i < lowEnd ? 0 : ( i < highFrom ? 1 : 2 );
		sum[ b ] += level;
		rawSum[ b ] += raw;
		rise[ b ] += up;
		++counted[ b ];
		total += level;
		totalRaw += raw;
		totalRise += up;
	}

	for( int b = 0; b < kBands; ++b )
		UpdateBand( bands[ static_cast< size_t >( b ) ], sum[ b ], rawSum[ b ], rise[ b ], counted[ b ], dt, settings );
	UpdateBand( full, total, totalRaw, totalRise, kBins, dt, settings );

	for( int g = 0; g < kWaveBands; ++g )
	{
		float s = 0.0f;
		for( int i = kWaveEdges[ g ]; i < kWaveEdges[ g + 1 ]; ++i )
			s += binLevel[ static_cast< size_t >( i ) ];
		const float mean = s / static_cast< float >( kWaveEdges[ g + 1 ] - kWaveEdges[ g ] );
		wave[ static_cast< size_t >( g ) ] = clamp01( mean / std::max( kPeakFloor, full.peak ) );
	}

	if( loudest > kNoiseFloor )
	{
		sinceSound = 0.0f;
		everHeard  = true;
	}
	else
		sinceSound += dt;

	primed = true;
}

float Analyser::Level() const
{
	return clamp01( full.level / std::max( kPeakFloor, full.peak ) );
}

} // namespace scrawl::audio
