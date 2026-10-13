#pragma once

#include "Maths.h"

#include <cstdint>
#include <vector>

/**
    The hand: how a line comes out of a person rather than a plotter.

    This is the plugin's one idea. Every doodle has an intended path -- a ring,
    a spike, a zigzag -- and every DRAWING of it is a fresh pass of a hand that
    cannot hold still. Hand-drawn effects animation is redrawn for every
    drawing, twelve a second on twos, and no two passes land in the same place;
    the line "boils". So the intended path is fixed by the doodle and the error
    is fixed by the drawing, and between two drawings everything except the
    error stays put. That is the whole look, and the rest falls out of it:

    - **The boil.** A displacement along the line's normal, a sum of three
      sinusoids in arc length whose wavelengths belong to the doodle (a
      person's hand has a character) and whose phases belong to the drawing.
      Its RMS is A/sqrt(2), and two independent drawings of the same line
      differ by A RMS -- which `sctest --hand` and `--boil` measure.
    - **Registration.** Each pass also lands as a whole slightly off: an offset
      anywhere in a disc of radius A/2 and a twist about the doodle's anchor,
      as when a sheet is not quite back on the pegs.
    - **The error scales with the gesture.** A = Wobble x (0.003 + 0.02 x
      extent) frame heights: a big circle wanders further than a small tick,
      as a reach does in any model of aimed movement.
    - **Pressure.** The pen lands (the width rises over the first few
      millimetres) and lifts (it tapers off the end), and between the two it
      presses unevenly, +-15% at full Wobble. At Wobble 0 the hand is a
      plotter: no boil, no registration error and an even width, which is what
      `sctest --width` draws with.
    - **The pen tip.** A stroke being written on stops abruptly where the pen
      is, at full pressure, and only tapers once the pen has lifted at the end.

    Lengths are in frame heights. The hand resamples whatever it is given to
    even steps of kSpacing in arc length, so the error is a function of how far
    along the line the pen is and not of how the path happened to be sampled.
*/
namespace scrawl
{
/// One piece of drawn line: a capsule from p0 to p1 whose radius goes from r0
/// to r1, in one ink. Canvas units (frame heights, origin in the middle, y up).
struct Segment
{
	float x0, y0, x1, y1;
	float r0, r1;
	float red, green, blue, alpha;
};

/// What the pen is drawing with.
struct Pen
{
	double width = 0.005;///< the full width of the line at full pressure
	float red = 1.0f, green = 1.0f, blue = 1.0f;
	float alpha = 1.0f;
};

/// One stroke: an intended centreline, and how much of it is on the page.
struct Stroke
{
	std::vector< V2 > points;
	double widthScale = 1.0;
	double from       = 0.0;///< the part drawn: [from, to] as fractions of the length
	double to         = 1.0;
	double landing    = 0.04;///< how far the pen takes to reach full pressure
	double lift       = 0.08;///< how far it takes to lift off at the end
	double dashes     = 1.0;///< 1 solid; below 1, the fraction of each dash kept
	double dashPeriod = 0.05;
	double extent     = 0.1;///< the size of the gesture, for the error's scale
	double alpha      = 1.0;
};

namespace hand
{
/// The arc-length step the hand works in (2.9 px at 720p, 8.6 px at 4K; a
/// circle of radius 0.05 is still within a twentieth of a pixel of round).
constexpr double kSpacing = 0.004;

/// The hand's error amplitude A for this Wobble and gesture size.
double Amplitude( double wobble, double extent );

/// The registration disc's radius, as a fraction of A.
constexpr double kRegistration = 0.5;

/// The base wavelengths of the boil, frame heights; each doodle scales them
/// by 0.8..1.2 (its hand's character).
constexpr double kWavelength[ 3 ] = { 0.30, 0.13, 0.06 };

/// The boil's three amplitudes, normalised so that their squares sum to 1:
/// the displacement's RMS is A/sqrt(2).
double Weight( int k );

/// The pressure's own wander at full Wobble, and its wavelength.
constexpr double kPressureWander     = 0.15;
constexpr double kPressureWavelength = 0.21;

struct Options
{
	double wobble = 0.5;
	Pen pen;
	uint32_t character = 0;///< the doodle's: the boil's wavelengths
	uint32_t drawing   = 0;///< the drawing's: the boil's phases, the registration
	V2 pivot;              ///< what the registration twists about
	double amplitudeScale = 1.0;///< test: a wrong model's hand (1 in operation)
};

/// Draw one stroke and append its segments. Returns how many it appended.
/// `centreline`, when given, receives the displaced centreline of the drawn
/// part (the harness measures the boil from it).
int Draw( const Stroke& stroke, const Options& options, std::vector< Segment >& out,
          std::vector< V2 >* centreline = nullptr );

/// Resample a polyline to even steps of `spacing` in arc length, keeping the
/// exact end points. Returns the arc length at each point in `s`.
void Resample( const std::vector< V2 >& in, double spacing, std::vector< V2 >& out, std::vector< double >& s );

} // namespace hand
} // namespace scrawl
