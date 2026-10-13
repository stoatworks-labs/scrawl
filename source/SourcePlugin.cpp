#include "Scrawl.h"

/**
    The source: doodles on their own -- over nothing, so the layer goes over
    whatever is under it, or on black, or on paper.

    Listed directly in the ScrawlSource target, not in scrawl_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins into
    both bundles. The core is an OBJECT library because this registers itself
    from a file-scope constructor nothing references (see CMakeLists.txt).

    `SW Scrawl` is nine characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class ScrawlSource : public scrawl::ScrawlPlugin
{
public:
	ScrawlSource() :
		ScrawlPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ScrawlSource >, // Create method
	"SK01",                        // Plugin unique ID of maximum length 4
	"SW Scrawl",                   // Plugin name
	2,                             // API major version number
	1,                             // API minor version number
	0,                             // Plugin major version number
	1,                             // Plugin minor version number
	FF_SOURCE,                     // Plugin type
	"Hand-drawn doodles to the music: rings of sound, impact spikes, zigzag bolts, curls and a wobbly line, "
	"redrawn on twos by a hand that cannot hold still, so every line boils. Follows the audio, or the beat "
	"when there is none.",
	"Scrawl FFGL source"           // About
);

extern "C" const char* ScrawlSourceBuildStamp()
{
	return "scrawl " SCRAWL_VERSION " source, built " __DATE__ " " __TIME__;
}
