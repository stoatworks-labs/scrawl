#pragma once

#include "Controls.h"
#include "Hand.h"
#include "Maths.h"

#include <array>
#include <cstdint>
#include <vector>

/**
    What gets drawn: the doodles, and the score that decides when.

    The vocabulary is the comic-book sound effect as a film once drew it over a
    band in a basement: rings of sound coming off the amps, impact spikes on
    the snare, a zigzag bolt on the big hits, little curls floating up off the
    hi-hats, and one long wobbly line that is the music itself. Each is an
    intended path that changes with its age; the hand (Hand.h) draws it.

      Rings   a set of 1-3 concentric wavy lines spreading out from where the
              sound came from: arcs either side of it, (( )), when scattered;
              arcs facing the room from the Speakers; and from the Centre full
              circles that never quite close (the pen overshoots its start, a
              little wide of it). They thin out and break into dashes as they
              go.
      Burst   spikes shooting out from an impact and shrinking to their tips;
              on a hard hit, a jagged star around it as well.
      Bolt    a zigzag with a fork, written in one drawing, flickering on
              alternate drawings in its second half, gone.
      Curl    a cursive loop-the-loop or a spiral, written on, drifting, and
              wiped off from the tail.
      Wave    not spawned: always there while there is sound, a line across
              the frame whose wiggle is the spectrum.

    Doodles are born at the frame their event arrives; they appear at the next
    drawing (Score::Build is only ever asked for a drawing's time).
*/
namespace scrawl
{
enum class Kind : int
{
	Rings = 0,
	Burst,
	Bolt,
	Curl,
	Count
};

constexpr int kKinds = static_cast< int >( Kind::Count );

extern const char* const kKindNames[];

struct Doodle
{
	Kind kind        = Kind::Rings;
	double birth     = 0.0;
	uint32_t seed    = 0;
	V2 anchor;
	double direction = 0.0;
	double scale     = 1.0;///< Size x the event's strength x the kind's amount
	double strength  = 1.0;
	int colour       = 0;
	bool fullCircle  = true;
	int count        = 1;///< rings: arcs; burst: spikes; curl: loops
	bool extra       = false;///< rings: arcs both sides; burst: the star; bolt: the fork
	int variant      = 0;///< curl: 0 loops, 1 spiral
};

/// Everything a spawn or a drawing reads from the controls.
struct ScoreSettings
{
	double aspect = 16.0 / 9.0;
	double size   = 1.0;
	double linger = 1.0;
	std::array< double, kKinds > amount { { 0.7, 0.6, 0.4, 0.5 } };
	double wave            = 0.5;
	Placement placement    = Placement::Scatter;
	V2 centre;            ///< canvas units
	uint32_t seed          = 0;
	Palette palette        = Palette::White;
	float colour[ 3 ]      = { 1.0f, 0.3f, 0.7f };
	double wobble          = 0.5;
	double lineWidth       = 0.0065;
	int passes             = 1;
};

/// The wave's input for one drawing: the six spectrum groups (or the beat's
/// stand-in) and how loud it all is.
struct WaveShape
{
	std::array< float, 6 > levels {};
	float level = 0.0f;
};

/// Switches for the harness's wrong models. Nothing in normal operation sets
/// any of them.
struct ScoreTestFlags
{
	bool fixedHand = false;///< the same hand every drawing: no boil
	bool flatWave  = false;///< the wave drawn straight and at full pressure
	bool noWiggle  = false;///< rings without their intended wiggle
	double handScale = 1.0;///< the hand's amplitude multiplied (a wrong model)
	double frozenAge = -1.0;///< at or above 0: every doodle drawn at this age, none expiring
};

class Score
{
public:
	static constexpr size_t kMaxDoodles = 96;

	/// A new doodle of `kind`, born `now`, from an event of `strength` (0..1).
	/// Nothing happens when the kind's amount is 0.
	void Spawn( Kind kind, double now, double strength, const ScoreSettings& settings );

	/// Forget doodles whose life has ended by `now`.
	void Expire( double now, const ScoreSettings& settings );

	void Clear();

	/// How long a doodle of `kind` lives, at this Linger.
	static double Life( Kind kind, double linger );

	/// Every segment of drawing `index`, made at `time` and shown for the
	/// next `exposure` seconds. Only doodles born by `time` are in it, and each
	/// is drawn as it is half-way through the exposure -- so a hit on a
	/// drawing's own frame is already on the page in that drawing.
	void Build( double time, double exposure, long long index, const ScoreSettings& settings, const WaveShape& wave,
	            std::vector< Segment >& out ) const;

	/// One doodle's intended strokes at `age` (seconds since birth) in drawing
	/// `index`. Public for the harness.
	static void Strokes( const Doodle& doodle, double age, long long index, const ScoreSettings& settings,
	                     const ScoreTestFlags& flags, std::vector< Stroke >& out );

	/// The wave's intended stroke for one drawing.
	static bool WaveStroke( const ScoreSettings& settings, const WaveShape& wave, double time,
	                        const ScoreTestFlags& flags, Stroke& out );

	/// The ink colour of a palette entry.
	static void Ink( const ScoreSettings& settings, int colour, float rgb[ 3 ] );

	const std::vector< Doodle >& Doodles() const
	{
		return doodles;
	}
	uint32_t Spawned() const
	{
		return spawned;
	}
	ScoreTestFlags& Flags()
	{
		return flags;
	}
	const ScoreTestFlags& Flags() const
	{
		return flags;
	}

private:
	std::vector< Doodle > doodles;
	uint32_t spawned   = 0;
	uint32_t ringsSide = 0;
	ScoreTestFlags flags;
};

} // namespace scrawl
