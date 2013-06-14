/* 
 * File:   NellyCodec.h
 * Author: Sergio
 *
 * Created on 7 de diciembre de 2011, 23:29
 */

#ifndef G722_H
#define	G722_H
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "config.h"
#include "fifo.h"
#include "codecs.h"
#include "audio.h"

class G722Encoder : public AudioEncoder
{
public:
	G722Encoder(const Properties &properties);
	virtual ~G722Encoder();
	virtual int Encode(SWORD *in,int inLen,BYTE* out,int outLen);
	virtual DWORD GetPreferredRate() { return 16000; };

private:
	AVCodec 	*codec;
	AVCodecContext	*ctx;
};

class G722Decoder : public AudioDecoder
{
public:
	G722Decoder();
	virtual ~G722Decoder();
	virtual int Decode(BYTE *in,int inLen,SWORD* out,int outLen);
	virtual DWORD GetPreferredRate() { return 16000; };

private:
	AVCodec 	*codec;
	AVCodecContext	*ctx;
	fifo<SWORD,1024>  samples;
};

#endif	/* NELLYCODEC_H */

