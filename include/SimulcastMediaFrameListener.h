#ifndef SIMULCASTMEDIAFRAMELISTENER_H
#define SIMULCASTMEDIAFRAMELISTENER_H

#include "media.h"
#include "use.h"
#include "video.h"
#include "TimeService.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_set>
#include <unordered_map>

class SimulcastMediaFrameListener :
	public MediaFrame::Listener
{
public:
	SimulcastMediaFrameListener(TimeService& timeService,DWORD ssrc, DWORD numLayers);
	virtual ~SimulcastMediaFrameListener();

	void SetNumLayers(DWORD numLayers);
	void AddMediaListener(const MediaFrame::Listener::shared& listener);
	void RemoveMediaListener(const MediaFrame::Listener::shared& listener);

	virtual void onMediaFrame(const MediaFrame& frame) { onMediaFrame(0, frame); }
	virtual void onMediaFrame(DWORD ssrc, const MediaFrame& frame);

	void Stop();

private:
	void ForwardFrame(VideoFrame& frame);

	void Push(std::unique_ptr<VideoFrame> frame);
	void Enqueue(std::unique_ptr<VideoFrame> frame);
	void Flush();
private:
	TimeService& timeService;
	DWORD ssrc = 0;
	std::unordered_set<MediaFrame::Listener::shared> listeners;

	uint32_t numLayers = 0;
	uint32_t maxQueueSize = 0;

	bool initialised = false;
	DWORD selectedSsrc = 0;
	QWORD lastEnqueueTimeMs = 0;
	std::optional<QWORD> lastForwardedTimestamp;

	std::optional<uint64_t> referenceFrameTime;

	std::unordered_map<uint64_t, int64_t> initialTimestamps;
	std::unordered_map<uint64_t, size_t> layerDimensions;
	std::deque<std::unique_ptr<VideoFrame>> queue;

	// Latest timestamps for each layer
	std::unordered_map<uint32_t, uint64_t> layerTimestamps;
};

#endif /* SIMULCASTMEDIAFRAMELISTENER_H */
