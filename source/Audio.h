#pragma once

#include <array>

/**
	The ears: Resolume's 64-bin spectrum in, three onset detectors and a few
	levels out.

	The detector is bassalt's (millpond's, rosette's, macroblock's before that:
	the one that has been driven with real music through Resolume's FFT
	buffer), split three ways. Each band keeps its own spectral flux -- the sum
	of the bins that ROSE since the last frame -- against its own running mean,
	so a kick fires the low band and a hi-hat the high one even when the kick
	is ten times louder. The low band draws rings, the middle bursts, the top
	curls; a kick that reaches the low band's recent peak draws a bolt, as
	often as Bolts allows.

	**The first frame is primed.** On the first frame there is no previous
	frame: every bin reads as having risen from silence, `dt` is zero so the
	running mean snaps to that enormous flux, and the detector is deaf for a
	second and a half -- after every clip trigger, which is exactly when an
	operator is watching for the first kick (the fleet trap, measured in
	bassalt). `Settings::prime` is off only for the harness's negative control.

	**The bin law is an assumption.** Nobody in the fleet has measured what
	frequencies Resolume's 64 bins span. If they are linear to a 22 kHz Nyquist
	then bin 0 alone is 0-345 Hz; if they are log-spaced, two bins are the kick
	and the sub. So the band edges are controls (Bass Bins, Treble From), not
	constants, and the README says so.

	**Video rate.** One spectrum arrives per rendered frame. The smallest
	interval this can resolve is a frame, and a hit lands on the frame after
	its transient -- then on the next drawing, which is at most a twelfth of a
	second later on twos. That is the plugin's latency, and `sctest --latency`
	measures it.
*/
namespace scrawl::audio
{

/// The spectrum Resolume delivers. Fixed by the host, not chosen here.
constexpr int kBins = 64;

/// The low, middle and high bands.
constexpr int kBands = 3;

/// The wave's shape is the spectrum folded into this many octave-ish groups.
constexpr int kWaveBands = 6;

struct Settings
{
	float attackSeconds  = 0.010f;
	float releaseSeconds = 0.200f;

	/// How easily an onset fires: the margin over each band's flux floor.
	float sensitivity = 0.6f;

	/// The low band is bins [0, bassBins); the high band [trebleFrom, 64).
	int bassBins   = 2;
	int trebleFrom = 16;

	/// Prime on the first frame. Off only for the harness's negative control.
	bool prime = true;
};

struct Band
{
	float level    = 0.0f;///< enveloped, sqrt-compressed mean of the band's bins
	float raw      = 0.0f;///< the same mean this frame, unenveloped
	float peak     = 0.0f;///< of `raw`, decaying slowly: the normalising reference
	float flux     = 0.0f;///< positive spectral difference this frame
	float fluxMean = 0.0f;///< running mean of the above, the adaptive floor
	float refractory = 0.0f;
	bool fired     = false;
	float strength = 0.0f;///< the last onset's raw level against the peak: a hit as hard as the
	                      ///< hardest lately is 1, whatever the envelope's attack had reached
	unsigned long long onsets = 0;
};

class Analyser
{
public:
	/// `bins` is what the host handed over; `count` may be fewer than kBins.
	/// `dt` is the frame in seconds.
	void Update( const float* bins, int count, float dt, const Settings& settings );

	const Band& Low() const
	{
		return bands[ 0 ];
	}
	const Band& Mid() const
	{
		return bands[ 1 ];
	}
	const Band& High() const
	{
		return bands[ 2 ];
	}
	const Band& Full() const
	{
		return full;
	}

	/// The whole spectrum's level against its own recent peak, 0..1.
	float Level() const;

	/// The wave's six groups, each against the whole spectrum's peak, 0..1.
	const std::array< float, kWaveBands >& WaveLevels() const
	{
		return wave;
	}

	/// True while any bin has been above the noise floor within the last two
	/// seconds: Drive's Auto listens to the music while this holds.
	bool Present() const
	{
		return sinceSound < 2.0f;
	}

	/// Whether any audio has ever arrived (for the log).
	bool EverHeard() const
	{
		return everHeard;
	}

	void Reset();

private:
	void UpdateBand( Band& band, float sum, float rawSum, float rise, int count, float dt, const Settings& settings );

	std::array< float, kBins > binLevel {};
	std::array< float, kBins > binPrevious {};
	std::array< Band, kBands > bands {};
	Band full;
	std::array< float, kWaveBands > wave {};
	float sinceSound = 1e9f;
	bool everHeard   = false;
	bool primed      = false;
};

} // namespace scrawl::audio
