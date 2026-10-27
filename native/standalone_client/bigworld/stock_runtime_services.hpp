#ifndef STOCK_RUNTIME_SERVICES_HPP
#define STOCK_RUNTIME_SERVICES_HPP

#include "cstdmf/stdmf.hpp"

namespace wot
{

/**
 *	#1513 App frame-pacing modes over App::minFrameTime_: the Hangar special
 *	mode (0061e950/0061e880) and the arena-loading reduced mode (App+0x614).
 */
class StockFramePacing
{
public:
	// renderer/reducedFrameRate, the special frame time used when fps <= 0.
	void configure( BW::uint64 specialMinFrameTime )
	{
		specialMinFrameTime_ = specialMinFrameTime;
	}

	// The first call saves the normal frame time; nested calls only retarget.
	void setSpecial( BW::uint64 & minFrameTime, float fps, double frequency )
	{
		if (!special_)
		{
			savedMinFrameTime_ = minFrameTime;
			special_ = true;
		}
		minFrameTime = fps > 0.f ?
			BW::uint64( frequency / double( fps ) ) : specialMinFrameTime_;
	}

	// Restores the saved frame time, including an unlimited (zero) one.
	void disableSpecial( BW::uint64 & minFrameTime )
	{
		if (special_)
		{
			minFrameTime = savedMinFrameTime_;
			special_ = false;
		}
	}

	void setReduced( bool enabled ) { reduced_ = enabled; }

	// 0061e749: reduced mode sleeps at least 20 ms after each frame.
	int sleepTime( int milliseconds ) const
	{
		return (reduced_ && milliseconds < 20) ? 20 : milliseconds;
	}

	bool special() const { return special_; }
	bool reduced() const { return reduced_; }

private:
	BW::uint64 specialMinFrameTime_ = 0;
	BW::uint64 savedMinFrameTime_ = 0;
	bool special_ = false;
	bool reduced_ = false;
};

} // namespace wot

#endif // STOCK_RUNTIME_SERVICES_HPP
