#include "Scrawl.h"

/**
    The effect: the same doodles over the clip.

    See SourcePlugin.cpp for why this file is listed in its own target.
*/
namespace
{
class ScrawlEffect : public scrawl::ScrawlPlugin
{
public:
	ScrawlEffect() :
		ScrawlPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ScrawlEffect >, // Create method
	"SK02",                        // Plugin unique ID of maximum length 4
	"SW Scrawl Over",              // Plugin name
	2,                             // API major version number
	1,                             // API minor version number
	0,                             // Plugin major version number
	1,                             // Plugin minor version number
	FF_EFFECT,                     // Plugin type
	"Hand-drawn doodles to the music over the clip: rings of sound, impact spikes, zigzag bolts, curls and a "
	"wobbly line, redrawn on twos by a hand that cannot hold still. Follows the audio, or the beat when there "
	"is none.",
	"Scrawl FFGL effect"           // About
);

extern "C" const char* ScrawlEffectBuildStamp()
{
	return "scrawl " SCRAWL_VERSION " effect, built " __DATE__ " " __TIME__;
}
