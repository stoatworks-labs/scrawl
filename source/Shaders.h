#pragma once

#include <initializer_list>
#include <string>
#include <vector>

/**
    The GLSL, as text.

    The shaders decide nothing: every line is made on the CPU by the hand
    (Hand.cpp) and arrives as capsules. The GPU's jobs are to rasterise them,
    blur a copy for the glow, and lay the lot over the clip.

      kStrokeVertex/Fragment  one instanced quad per capsule; coverage is the
                              distance to the capsule with a one-pixel box
                              ramp, so a line's cross-section integrates to
                              its width exactly. Drawn with GL_MAX blending:
                              the union of the capsules, so a joint is not
                              drawn twice and a crossing is not darker.
      kReduce                 the strokes, 4x4 boxes averaged into a quarter-
                              size buffer (a single bilinear tap would step
                              over a thin line)
      kBlur                   a separable Gaussian at quarter size
      kComposite              glow, then strokes, over the clip (or the
                              backdrop), premultiplied; Mix on the effect

    Each piece stays under MSVC's ~16 KB string-literal cap; tools/glslc.sh
    reassembles them in this order.
*/
namespace scrawl::shaders
{
extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kStrokeVertex;
extern const char* const kStrokeFragment;
extern const char* const kReduce;
extern const char* const kBlur;
extern const char* const kComposite;

std::string Assemble( std::initializer_list< const char* > pieces );

/// One program as the plugin compiles it.
struct Program
{
	const char* name;
	std::string vertex, fragment;
};

/// Every program the plugin compiles, in the order `Scrawl::InitGL` takes
/// them: strokes, reduce, blur, composite. `sctest --shaders DIR` writes
/// exactly these, and tools/glslc.sh reads this function to find them.
std::vector< Program > Programs();

} // namespace scrawl::shaders
