#ifndef _VIDEOSTREAM_H_
#define _VIDEOSTREAM_H_

#include <pthread.h>
#include "config.h"
#include "codecs.h"
#include "rtpsession.h"
#include "RTPSmoother.h"
#include "video.h"

class VideoStream 
{
public:
	class Listener : public RTPSession::Listener
	{
	public:
		virtual void onRequestFPU() = 0;
	};
public:
	VideoStream(Listener* listener);
	~VideoStream();

	int Init(VideoInput *input, VideoOutput *output);
	int SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int quality,int fillLevel,int intraPeriod);
	int StartSending(char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap);
	int StopSending();
	int SendFPU();
	int StartReceiving(RTPMap& rtpMap);
	int StopReceiving();
	int SetMediaListener(MediaFrame::Listener *listener);
	int SetMute(bool isMuted);
	int End();

	int IsSending()	  { return sendingVideo;  }
	int IsReceiving() { return receivingVideo;}
	MediaStatistics GetStatistics();
	
protected:
	int SendVideo();
	int RecVideo();

private:
	static void* startSendingVideo(void *par);
	static void* startReceivingVideo(void *par);

	//Listners
	Listener* listener;
	MediaFrame::Listener *mediaListener;

	//Los objectos gordos
	VideoInput     	*videoInput;
	VideoOutput 	*videoOutput;
	RTPSession      rtp;
	RTPSmoother	smoother;

	//Parametros del video
	VideoCodec::Type videoCodec;		//Codec de envio
	int		videoCaptureMode;	//Modo de captura de video actual
	int 		videoGrabWidth;		//Ancho de la captura
	int 		videoGrabHeight;	//Alto de la captur
	int 		videoFPS;
	int 		videoBitrate;
	int		videoQuality;
	int		videoFillLevel;
	int		videoIntraPeriod;

	//Las threads
	pthread_t 	sendVideoThread;
	pthread_t 	recVideoThread;
	pthread_mutex_t mutex;
	pthread_cond_t	cond;

	//Controlamos si estamos mandando o no
	bool	sendingVideo;	
	bool 	receivingVideo;
	bool	inited;
	bool	sendFPU;
	bool	muted;
};

#endif
