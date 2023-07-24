#ifndef _RTMP_MESSAGE_H_
#define _RTMP_MESSAGE_H_
#include "config.h"
#include "rtmp.h"
#include "amf.h"
#include "objectparser.h"
#include "avcdescriptor.h"
#include "h265/HEVCDescriptor.h"
#include "aac/aacconfig.h"
#include "RawFrame.h"

#include <vector>
#include <variant>

template<class InputIt>
constexpr uint32_t FourCcToUint32(InputIt first)
{
	static_assert(sizeof(decltype(*first)) == 1);
	
	uint32_t result = 0;
	for (unsigned i = 0; i < sizeof(uint32_t); i++)
	{
		result = (result << 8) + uint8_t(*first++);
	}
	
	return result;
}

class RTMPMediaFrame : public RawFrame
{
public:
	enum Type {Audio=8,Video=9};
public:
	virtual ~RTMPMediaFrame();
	virtual RTMPMediaFrame* Clone() = 0;

	Type  GetType()		{ return type;		}
	QWORD GetTimestamp()	{ return timestamp;	}
	void  SetTimestamp(QWORD timestamp) { this->timestamp = timestamp; }

	virtual DWORD Parse(BYTE *data,DWORD size);
	virtual DWORD Serialize(BYTE* buffer,DWORD size);
	virtual DWORD GetSize()	{ return bufferSize+1; 	}

	virtual BYTE*	GetMediaData()			{ return buffer;		}
	virtual DWORD	GetMediaSize()			{ return mediaSize;		}
	virtual DWORD	GetMaxMediaSize()		{ return bufferSize;		}
	virtual void	SetMediaSize(DWORD mediaSize)	{ this->mediaSize = mediaSize;	}

	virtual void	Dump();

	static const char* GetTypeName(Type type)
	{
		switch (type)
		{
			case Audio:
				return "Audio";
			case Video:
				return "Video";
		}
		return "Unknown";
	}
	
	virtual RawFrameMediaType GetRawMediaType() const override
	{
		return RawFrameMediaType::CodedFrames;
	}
	
	virtual bool IsKeyFrame() const override
	{
		return false;
	}

protected:
	RTMPMediaFrame(Type type,QWORD timestamp,BYTE *data,DWORD size);
	RTMPMediaFrame(Type type,QWORD timestamp,DWORD size);

	QWORD timestamp;
	BYTE *buffer;
	DWORD bufferSize;
	DWORD mediaSize;
	DWORD pos;
	Type type;
};

class RTMPVideoFrame : public RTMPMediaFrame
{
public:
	enum VideoCodec {FLV1=2,SV=3,VP6=4,VP6A=5,SV2=6,AVC=7};
	enum FrameType  {INTRA=1,INTER=2,DISPOSABLE_INTER=3,GENERATED_KEY_FRAME=4,VIDEO_INFO=5};
	enum AVCType	{AVCHEADER = 0, AVCNALU = 1, AVCEND = 2 };
	enum PacketType {
		SequenceStart = 0,
		CodedFrames = 1,
		SequenceEnd = 2,
		CodedFramesX = 3,
		Metadata = 4,
		MPEG2TSSequenceStart = 5
	};
	enum VideoCodecEx {
		AV1 = FourCcToUint32("av01"),
		VP9 = FourCcToUint32("vp09"),
		HEVC = FourCcToUint32("hvc1")
	};
public:
	RTMPVideoFrame(QWORD timestamp,DWORD size);
	RTMPVideoFrame(QWORD timestamp, const AVCDescriptor &desc);
	virtual ~RTMPVideoFrame();
	virtual RTMPMediaFrame* Clone();

	virtual DWORD	Parse(BYTE *data,DWORD size);
	virtual DWORD	Serialize(BYTE* buffer,DWORD size);
	virtual DWORD	GetSize();

	void		SetVideoCodec(VideoCodec codec)		{ this->codec = codec;		}
	void		SetFrameType(FrameType frameType)	{ this->frameType = frameType;	}
	VideoCodec	GetVideoCodec()	const {
		if (!std::holds_alternative<VideoCodec>(codec)) 	return VideoCodec(-1);
		return std::get<VideoCodec>(codec);	
	}
	FrameType	GetFrameType()				const { return frameType;	}
	AVCType		GetAVCType() const {
		if (!std::holds_alternative<AVCType>(packetType)) 	return AVCType(-1);
		return std::get<AVCType>(packetType);	
	}
	int32_t		GetAVCTS()				const { return compositionTime; }
	
	DWORD		SetVideoFrame(BYTE* data,DWORD size);
	void		SetAVCType(AVCType type)		{ packetType = type;		}
	void		SetAVCTS(int32_t ts)			{ compositionTime = ts; }
	virtual void	Dump();
	
	bool		IsExtended() const			{ return isExtended; }
	VideoCodecEx	GetVideoCodecEx() const {
		if (!std::holds_alternative<VideoCodecEx>(codec)) 	return VideoCodecEx(-1);
		return std::get<VideoCodecEx>(codec); 
	}
	PacketType      GetPacketType() const {
		if (!std::holds_alternative<PacketType>(packetType)) 	return PacketType(-1);
		return std::get<PacketType>(packetType); 
	}
	
	
	virtual RawFrameMediaType GetRawMediaType() const override
	{
		if (!isExtended)
		{
			switch (GetAVCType())
			{
			case AVCHEADER:
				return RawFrameMediaType::Config;
			case AVCNALU:
				return RawFrameMediaType::CodedFrames;
			default:
				return RawFrameMediaType::Other;				
			}
		}
		
		switch (GetPacketType())
		{
		case SequenceStart:
			return RawFrameMediaType::Config;
		case CodedFrames:
		case CodedFramesX:
			return RawFrameMediaType::CodedFrames;
		default:
			return RawFrameMediaType::Other;
		}
	}
	
		
	virtual bool IsKeyFrame() const override
	{
		return frameType == FrameType::INTRA;
	}
	
private:
	
	enum class ParsingState
	{
		VideoTagHeader,
		VideoTagHeaderFourCc,
		VideoTagAvcExtra,
		VideoTagBody,
		VideoTagHevcCompositionTime,
		VideoTagData,
	};

	bool		isExtended = false;
	FrameType	frameType = FrameType::INTER;
	
	std::variant<VideoCodec, VideoCodecEx>	codec = AVC;
	std::variant<AVCType, PacketType> packetType = AVCHEADER;
	
	int32_t	compositionTime = 0;

	ParsingState parsingState = ParsingState::VideoTagHeader;
	
	std::unique_ptr<ObjectParser>	objectParser;
};

class RTMPAudioFrame : public RTMPMediaFrame
{
public:
	enum AudioCodec		{PCM=0,ADPCM=1,MP3=2,PCMLE=3,NELLY16khz=4,NELLY8khz=5,NELLY=6,G711A=7,G711U=8,AAC=10,SPEEX=11,MP38khz=14};
	enum SoundRate		{RATE5khz=0,RATE11khz=1,RATE22khz=2,RATE44khz=3};
	enum AACPacketType	{AACSequenceHeader = 0, AACRaw = 1};
public:
	RTMPAudioFrame(QWORD timestamp,DWORD size);
	RTMPAudioFrame(QWORD timestamp,const AACSpecificConfig &config);
	virtual ~RTMPAudioFrame();
	virtual RTMPMediaFrame* Clone();

	virtual DWORD	Parse(BYTE *data,DWORD size);
	virtual DWORD	Serialize(BYTE* buffer,DWORD size);
	virtual DWORD	GetSize();

	AudioCodec	GetAudioCodec()			{ return codec;			}
	SoundRate	GetSoundRate()			{ return rate;			}
	bool		IsSamples18Bits()		{ return sample16bits;		}
	bool		IsStereo()			{ return stereo;		}
	void		SetAudioCodec(AudioCodec codec)	{ this->codec = codec;		}
	void		SetSoundRate(SoundRate rate)	{ this->rate = rate;		}
	void		SetSamples16Bits(bool sample16bits) { this->sample16bits = sample16bits; }
	void		SetStereo(bool stereo)		{ this->stereo = stereo;	}
	DWORD		SetAudioFrame(const BYTE* data,DWORD size);

	void		SetAACPacketType(AACPacketType type)	{ extraData[0] = type;	}
	AACPacketType   GetAACPacketType()			{ return (AACPacketType) extraData[0]; }

	virtual void	Dump();
private:
	AudioCodec	codec;
	SoundRate	rate;
	bool		sample16bits;
	bool		stereo;
	DWORD		headerPos;
	BYTE		extraData[1];
};

class RTMPCommandMessage
{
public:
	RTMPCommandMessage();
	RTMPCommandMessage(const wchar_t* name,QWORD transId,AMFData* params,AMFData *extra);
	RTMPCommandMessage(const wchar_t* name,QWORD transId,AMFData* params,const std::vector<AMFData*>& extra);
	virtual ~RTMPCommandMessage();

	virtual DWORD Parse(BYTE *data,DWORD size);
	DWORD Serialize(BYTE* buffer,DWORD size);
	DWORD GetSize();

	std::wstring 	GetName() 		{ return name->GetWString(); 	}
	std::string 	GetNameUTF8() 		{ return name->GetUTF8String();	}
	double		GetTransId()		{ return transId->GetNumber(); 	}
	bool		HasName()		{ return name;			}
	bool		HasTransId()		{ return transId;		}
	bool		HasParams()  		{ return params; 		}
	AMFData*	GetParams()  		{ return params; 		}
	DWORD		GetExtraLength() 	{ return extra.size(); 		}
	AMFData*	GetExtra(DWORD i) 	{ return extra[i]; 		}
	void		Dump();
	RTMPCommandMessage* Clone() const;
	
private:
	AMFParser 	parser;
	AMFString* 	name;
	AMFNumber* 	transId;
	AMFData* 	params; 	//Could be an object or NULL
	std::vector<AMFData*> extra; 	//Variable length of extra parameters
};

class RTMPNetStatusEvent : public AMFObject
{
public:
	RTMPNetStatusEvent(const wchar_t* code,const wchar_t* level,const wchar_t* description)
	{
		AddProperty(L"level",level);
		AddProperty(L"code",code);
		AddProperty(L"description",description);
	};

	const std::wstring GetCode()		{ return (std::wstring)GetProperty(L"code");		}
	const std::wstring GetLevel()		{ return (std::wstring)GetProperty(L"level");		}
	const std::wstring GetDescription()	{ return (std::wstring)GetProperty(L"description");	}
};

class RTMPMetaData
{
public:
	RTMPMetaData(QWORD timestamp);
	~RTMPMetaData();

	QWORD GetTimestamp()	{ return timestamp;	}
	void  SetTimestamp(QWORD timestamp) { this->timestamp = timestamp; }
	DWORD Parse(BYTE *data,DWORD size);
	DWORD Serialize(BYTE* buffer,DWORD size);
	DWORD GetSize();

	DWORD		GetParamsLength();
	AMFData*	GetParams(DWORD i);
	void		AddParam(AMFData* param);
	void		Dump();
	RTMPMetaData* 	Clone();

private:
	AMFParser 	parser;
	std::vector<AMFData*> params; //Variable length of parameters
	QWORD		timestamp;
};

class RTMPMessage
{
public:
	enum Type
	{
		SetChunkSize = 1,
		AbortMessage = 2,
		Acknowledgement = 3,
		UserControlMessage = 4,
		WindowAcknowledgementSize = 5,
		SetPeerBandwidth = 6,
		Command = 20,
		CommandAMF3 = 17,	
		Data = 18,
		DataAMF3 = 15,
		SharedObject = 16,
		SharedObjectAMF3 = 19,
		Audio = 8,
		Video = 9,
		Aggregate = 22
	};
	static const char* TypeToString(Type type)
	{
		//Depending on the type
		switch(type)
		{
			case SetChunkSize:
				 return "SetChunkSize";
			case AbortMessage:
				 return "AbortMessage";
			case Acknowledgement:
				 return "Acknowledgement";
			case UserControlMessage:
				 return "UserControlMessage";
			case WindowAcknowledgementSize:
				 return "WindowAcknowledgementSize";
			case SetPeerBandwidth:
				 return "SetPeerBandwidth";
			case Command:
				 return "Command";
			case CommandAMF3:
				 return "CommandAMF3";
			case Data:
				 return "Data";
			case DataAMF3:
				 return "DataAMF3";
			case SharedObject:
				 return "SharedObject";
			case SharedObjectAMF3:
				 return "SharedObjectAMF3";
			case Audio:
				 return "Audio";
			case Video:
				 return "Video";
			case Aggregate:
				 return "Aggregate";

		}
		return "Undefined";
	}
private:
	RTMPMessage();
public:
	RTMPMessage(DWORD streamId,QWORD timestamp,Type type,DWORD length);
	RTMPMessage(DWORD streamId,QWORD timestamp,Type type,RTMPObject* msg);
	RTMPMessage(DWORD streamId,QWORD timestamp,RTMPCommandMessage* cmd);
	RTMPMessage(DWORD streamId,QWORD timestamp,RTMPMediaFrame* media);
	RTMPMessage(DWORD streamId,QWORD timestamp,RTMPMetaData* meta);
	~RTMPMessage();
	
	DWORD Parse(BYTE* buffer,DWORD size);
	DWORD Serialize(BYTE *data,DWORD size);
	bool IsParsed();
	DWORD GetLeft();
	void Dump();

	bool IsControlProtocolMessage();
	bool IsCommandMessage();
	bool IsMedia();
	bool IsMetaData();
	bool IsSharedObject();

	RTMPObject* 		GetControlProtocolMessage()	{ return ctrl; 	}
	RTMPCommandMessage* 	GetCommandMessage()		{ return cmd; 	}
	RTMPMetaData* 		GetMetaData()			{ return meta;	}
	RTMPMediaFrame*		GetMediaFrame()			{ return media;	}

	DWORD	GetStreamId() 	{ return streamId; 	}
	Type	GetType()	{ return type; 		}
	DWORD	GetLength()	{ return length; 	}
	QWORD	GetTimestamp()	{ return timestamp; 	}	

private:
	RTMPObject* 		ctrl;
	RTMPCommandMessage* 	cmd;
	RTMPMetaData*		meta;
	RTMPMediaFrame*		media;

	//Header values
	DWORD 	streamId;
	Type	type;	
	DWORD 	length;
	QWORD 	timestamp;
	//Parsing variables
	bool skip;
	bool parsing;	
	DWORD pos;
};




#endif
