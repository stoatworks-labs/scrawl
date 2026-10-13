#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Doodles.h"
#include "Hand.h"
#include "Transport.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <array>
#include <string>
#include <vector>

namespace scrawl
{
/**
    The plugin: the source (doodles on their own, over nothing or a backdrop)
    and, with `isEffect`, the Over effect (the same over the clip). One class,
    two registrations, jackpot's arrangement.

    Each frame it listens (the spectrum, the transport, the Hit button), lets
    the score spawn doodles at the frame's time, and then asks one question:
    has the DRAWING changed? A drawing is floor( clock x Drawings ) -- 24, 12
    or 8 a second -- and only when it changes are the strokes made again (by
    the hand, with that drawing's error), rasterised and blurred for the glow.
    Every frame in between composites the same drawing, so a held drawing is
    the same picture to the bit (`sctest --twos`), and costs a composite.
*/
class ScrawlPlugin : public CFFGLPlugin
{
public:
	explicit ScrawlPlugin( bool isEffect );
	~ScrawlPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// LOAD-BEARING: the base class's stub fails, and a failed default deletes
	/// the instance -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	unsigned int ParamCount() const
	{
		return isEffect ? PT_COUNT_ALL : PT_SOURCE_COUNT;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// The analyser's wrong model (`--negative`): no priming on the first frame.
	void SetPrimeForTest( bool on )
	{
		prime = on;
	}
	/// A wrong model of the clock (`--negative`): a new drawing every frame.
	void SetDrawEveryFrameForTest( bool on )
	{
		drawEveryFrame = on;
	}
	ScoreTestFlags& Flags()
	{
		return score.Flags();
	}
	const Score& CurrentScore() const
	{
		return score;
	}
	const audio::Analyser& CurrentAnalyser() const
	{
		return analyser;
	}
	const std::vector< Segment >& CurrentSegments() const
	{
		return segments;
	}
	long long DrawingIndex() const
	{
		return drawing;
	}
	int DrawingsMade() const
	{
		return drawingsMade;
	}
	double Clock() const
	{
		return clock;
	}
	bool AudioDriving() const
	{
		return audioDriving;
	}
	/// What the spawns and drawings read from the controls, as they stand.
	ScoreSettings Settings( double aspect ) const;
	/// The frame's events, without GL: what ProcessOpenGL does before it draws.
	/// Returns true when a new drawing is due.
	bool Advance( double aspect );
	/// Build the current drawing's segments, without GL.
	void BuildDrawing( double aspect );

private:
	void UpdateClock();
	void Listen( double dt, double aspect );
	bool EnsureTargets( int width, int height );
	void ReleaseTargets();
	void RenderDrawing( int width, int height );

	const bool isEffect;
	float params[ PT_COUNT_ALL ] = {};

	//GL.
	ffglex::FFGLShader strokeProgram, reduceProgram, blurProgram, compositeProgram;
	ffglex::FFGLScreenQuad quad;
	GLuint segmentVao = 0, segmentVbo = 0;
	size_t segmentCapacity = 0;
	struct Target
	{
		GLuint fbo = 0, texture = 0;
		int width = 0, height = 0;
	};
	Target strokeTarget, glowA, glowB;
	bool drawn = false;

	//Time. Resolume has sent both seconds and milliseconds (see boreal).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	double clock = 0.0;     ///< plugin seconds since the first frame, re-based across jumps
	double clockBase = 0.0, nowBase = 0.0;
	double lastClock = -1.0;

	//Listening.
	Transport transport;
	std::vector< long long > eighths;
	audio::Analyser analyser;
	bool prime = true;
	bool audioDriving = false;
	bool loggedAudio = false;
	int hitPresses = 0, hitsTaken = 0;
	bool hitHeld = false;
	double lastBolt = -1e9;
	double lastBeatHit = -1e9;   ///< the beat drive's stand-in for a level

	//Drawing.
	Score score;
	long long drawing = -1;
	int drawingsMade = 0;
	bool dirty = true;
	bool drawEveryFrame = false;
	int framesSeen = 0;
	WaveShape waveNow, waveDrawn;
	std::vector< Segment > segments;
	int drawnWidth = 0, drawnHeight = 0;
};

} // namespace scrawl
