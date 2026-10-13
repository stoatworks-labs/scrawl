#pragma once

/**
    The host's parameters, and what they mean in real units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo` clamps
    an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could widen
    it. The conversions live in Controls.cpp, one function per control. Option,
    boolean, event and integer parameters hold the value itself; an option
    holds its element's value, which here is always its index.

    Units: lengths are in frame heights (the canvas is 1 tall and `aspect`
    wide, origin in the middle, y up), times in seconds.
*/
namespace scrawl
{
/**
    Parameter ids. **Append only** once released: FFGL's ABI is by index, and
    hosts other than Arena may store compositions that way.

    The host draws parameters in declaration order and `SetParamGroup` folds
    *runs* of one group name, so this enum is the inspector, top to bottom.

    The Over group sits AFTER the About block, on purpose: the source declares
    ids 0 .. PT_SOURCE_COUNT-1 and the effect all of them, so both share every
    id they have in common (jackpot's arrangement).
*/
enum ParamId : unsigned int
{
	// -- Draw ----------------------------------------------------------------
	PT_DRIVE = 0,
	PT_HIT,
	PT_DRAWINGS,
	PT_WOBBLE,
	PT_LINE_WIDTH,
	PT_PASSES,
	PT_SIZE,
	PT_LINGER,
	PT_SEED,

	// -- Doodles -------------------------------------------------------------
	PT_RINGS,
	PT_BURSTS,
	PT_BOLTS,
	PT_CURLS,
	PT_WAVE,

	// -- Place ---------------------------------------------------------------
	PT_PLACEMENT,
	PT_CENTRE_X,
	PT_CENTRE_Y,

	// -- Look ----------------------------------------------------------------
	PT_PALETTE,
	PT_COLOUR_R,
	PT_COLOUR_G,
	PT_COLOUR_B,
	PT_GLOW,
	PT_BACKDROP,

	// -- Audio ---------------------------------------------------------------
	PT_AUDIO,
	PT_SENSITIVITY,
	PT_BASS_BINS,
	PT_TREBLE_FROM,

	// -- The Stoatworks About block --------------------------------------------
	// Three buttons while there is no user guide (StoatworksAbout.h's guide is
	// empty); the guide's button is a fourth id here, and the static_assert in
	// Scrawl.cpp asks for it the day the generated About block gains one.
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_SOURCE_COUNT,

	// -- Over (the effect only) ------------------------------------------------
	PT_MIX = PT_SOURCE_COUNT,

	PT_COUNT_ALL
};

/// What makes the doodles happen.
enum class Drive
{
	Auto = 0,///< the music when there is any, Resolume's beat when there is not
	Audio,
	Beat,
	Count
};

/// How many new drawings a second: animation's ones, twos and threes at 24 fps.
enum class Drawings
{
	Ones = 0,
	Twos,
	Threes,
	Count
};

enum class Placement
{
	Scatter = 0,
	Centre,
	Speakers,
	Count
};

enum class Palette
{
	White = 0,
	Ink,
	Neon,
	Colour,
	Count
};

enum class Backdrop
{
	None = 0,
	Black,
	Paper,
	Count
};

extern const char* const kDriveNames[];
extern const char* const kDrawingsNames[];
extern const char* const kPlacementNames[];
extern const char* const kPaletteNames[];
extern const char* const kBackdropNames[];

/// New drawings per second for a Drawings option.
double DrawingsPerSecond( Drawings d );

/// The hand's error, 0..1 as it stands.
double WobbleFromParam( float v );

/// The line's full width in frame heights: 0.0015 to 0.02, geometric.
double LineWidthFromParam( float v );
float ParamFromLineWidth( double frameHeights );

/// A scale on every doodle: 0.5x to 2x, geometric (0.5 is 1x).
double SizeFromParam( float v );
float ParamFromSize( double scale );

/// A scale on every doodle's lifetime: 0.5x to 2x, geometric (0.5 is 1x).
double LingerFromParam( float v );

/// How much of each kind, 0..1 (0 is none at all).
double AmountFromParam( float v );

/// The glow's strength, 0..1.
double GlowFromParam( float v );

/// The onset detector's sensitivity, 0..1.
double SensitivityFromParam( float v );

/// An option's index from its element value, rounded and clamped.
int OptionIndex( float value, int count );

} // namespace scrawl
