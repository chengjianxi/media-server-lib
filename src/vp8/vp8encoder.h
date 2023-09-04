/* 
 * File:   vp8encoder.h
 * Author: Sergio
 *
 * Created on 13 de noviembre de 2012, 15:57
 */

#ifndef VP8ENCODER_H
#define	VP8ENCODER_H
#include "config.h"
#include "video.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"

class VP8Encoder : public VideoEncoder
{
public:
	VP8Encoder(const Properties& properties);
	virtual ~VP8Encoder();
	virtual VideoFrame* EncodeFrame(const VideoBuffer::const_shared& videoBuffer);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width,int height);
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod);
private:
	int OpenCodec();
private:
	vpx_codec_ctx_t		encoder;
	vpx_codec_enc_cfg_t	config;
	vpx_image_t*		pic;
	VideoFrame		frame;
	bool forceKeyFrame;
	int width;
	int height;
	DWORD numPixels;
	int bitrate;
	int fps;
	int format;
	int opened;
	int intraPeriod;
	int pts;
	int num;
	int threads;
	int cpuused;
	int maxKeyFrameBitratePct;
	int deadline;
	int errorResilientPartitions;
	int dropFrameThreshold;
	vpx_rc_mode endUsage;
	int minQuantizer;
	int maxQuantizer;
	int undershootPct;
	int overshootPct;
	int bufferSize;
	int bufferInitialSize;
	int bufferOptimalSize;
	int staticThreshold;
	int noiseReductionSensitivity;

};

#endif	/* VP8ENCODER_H */

