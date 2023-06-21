#ifndef TIME_SYNC_INTERFACE_H
#define TIME_SYNC_INTERFACE_H

#include "media.h"
#include "rtp.h"
#include "TimeService.h"

class TimeSyncInterface
{
public:

	virtual void Update(MediaFrame::Type type, std::chrono::milliseconds now, uint64_t ts);
	
	virtual int64_t GetTimeDrift(std::chrono::milliseconds now, uint64_t ts, uint64_t& tsOut) const = 0;
	
	virtual void reset() = 0;
};

#endif
