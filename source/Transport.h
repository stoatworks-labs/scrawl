#pragma once

#include <cmath>
#include <vector>

/**
    Resolume's transport, as FFGL 2.1 gives it: `SetBeatInfo( bpm, barPhase )`,
    turned into the eighth notes that went past during a frame.

    honeydew's and jackpot's: the phase is kept from the BPM and the host's
    elapsed time and RE-ALIGNED to the host's bar phase whenever one arrives,
    trusting it as a 0..1 position in a 4/4 bar (nothing in the fleet has
    measured how often Resolume calls it, or whether its bar phase is a usable
    downbeat). The position is counted in beats since the plugin started; a
    host phase is taken as the nearest position that agrees with it, so a bar
    boundary in the host's phase is a bar boundary here and not a jump back to
    the start of the bar. The host's first phase is where the plugin starts
    and crosses nothing; a later re-alignment that moves the position
    backwards crosses nothing, and one that leaps forwards crosses at most a
    bar's worth.

    No GL, so `sctest --offline` checks it.
*/
namespace scrawl
{
class Transport
{
public:
	static constexpr double kBeatsPerBar = 4.0;

	void SetBeatInfo( double bpm, double barPhase )
	{
		if( bpm > 0.0 && std::isfinite( bpm ) )
			this->bpm = bpm;
		if( std::isfinite( barPhase ) )
		{
			hostBarPhase = barPhase - std::floor( barPhase );
			hasHostPhase = true;
		}
	}

	/// Advance by the frame's dt (seconds). `eighths` receives the index of
	/// every eighth note crossed, in order: an even one is a beat, beat
	/// (index / 2) % 4 == 0 a downbeat.
	void Advance( double dt, std::vector< long long >& eighths )
	{
		eighths.clear();
		const double before = position;
		if( hasHostPhase )
		{
			const double bar = std::floor( position / kBeatsPerBar );
			double aligned   = ( bar + hostBarPhase ) * kBeatsPerBar;
			if( aligned < position - 0.5 * kBeatsPerBar )
				aligned += kBeatsPerBar;
			else if( aligned > position + 0.5 * kBeatsPerBar )
				aligned -= kBeatsPerBar;
			//The first phase the host sends is where we are, not a leap: it
			//crosses nothing.
			if( !hostAligned )
			{
				hostAligned = true;
				position    = aligned;
				hasHostPhase = false;
				return;
			}
			position     = aligned;
			hasHostPhase = false;
		}
		else if( dt > 0.0 )
			position += dt * bpm / 60.0;

		if( position > before )
		{
			long long first      = static_cast< long long >( std::floor( before * 2.0 ) ) + 1;
			const long long last = static_cast< long long >( std::floor( position * 2.0 ) );
			if( last - first >= 8 )
				first = last - 7;
			for( long long e = first; e <= last; ++e )
				eighths.push_back( e );
		}
	}

	double Bpm() const
	{
		return bpm;
	}
	/// Beats since the plugin started.
	double Position() const
	{
		return position;
	}
	/// Seconds since the last beat.
	double SinceBeat() const
	{
		return ( position - std::floor( position ) ) * 60.0 / bpm;
	}

private:
	double bpm          = 120.0;
	double position     = 0.0;
	double hostBarPhase = 0.0;
	bool hasHostPhase   = false;
	bool hostAligned    = false;
};

} // namespace scrawl
