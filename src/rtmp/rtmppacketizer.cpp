
#include "rtmp/rtmppacketizer.h"
#include "av1/AV1.h"
#include "av1/Obu.h"

template<typename T>
std::optional<std::pair<unsigned, unsigned>> IterateParameters(const AVCDescriptor&desc, const T& func)
{
	std::optional<std::pair<unsigned, unsigned>> dim;
	
	for (int i=0;i<desc.GetNumOfSequenceParameterSets();i++)
	{
		func(desc.GetSequenceParameterSet(i), desc.GetSequenceParameterSetSize(i));
		
		H264SeqParameterSet sps;
		if (sps.Decode(desc.GetSequenceParameterSet(i)+1,desc.GetSequenceParameterSetSize(i)-1))
		{
			dim = {sps.GetWidth(), sps.GetHeight()};
		}
	}

	for (int i=0;i<desc.GetNumOfPictureParameterSets();i++)
	{
		func(desc.GetPictureParameterSet(i), desc.GetPictureParameterSetSize(i));
	}
	
	return dim;
}

template<typename T>
std::optional<std::pair<unsigned, unsigned>> IterateParameters(const HEVCDescriptor&desc, const T& func)
{
	std::optional<std::pair<unsigned, unsigned>> dim;
	
	for (int i=0;i<desc.GetNumOfVideoParameterSets();i++)
	{
		func(desc.GetVideoParameterSet(i), desc.GetVideoParameterSetSize(i));
	}

	for (int i=0;i<desc.GetNumOfSequenceParameterSets();i++)
	{
		func(desc.GetSequenceParameterSet(i), desc.GetSequenceParameterSetSize(i));
		
		H265SeqParameterSet sps;
		if (sps.Decode(desc.GetSequenceParameterSet(i)+2,desc.GetSequenceParameterSetSize(i)-2))
		{
			dim = {sps.GetWidth(), sps.GetHeight()};
		}
	}

	for (int i=0;i<desc.GetNumOfPictureParameterSets();i++)
	{
		func(desc.GetPictureParameterSet(i), desc.GetPictureParameterSetSize(i));
	}
	
	return dim;
}

VideoCodec::Type GetRtmpFrameVideoCodec(const RTMPVideoFrame& videoFrame)
{
	if (!videoFrame.IsExtended())
	{
		switch (videoFrame.GetVideoCodec())
		{
		case RTMPVideoFrame::AVC:
			return VideoCodec::H264;
		default:
			return VideoCodec::UNKNOWN;
		}
	}
	
	switch (videoFrame.GetVideoCodecEx())
	{
	case RTMPVideoFrame::HEVC: 
		return VideoCodec::H265;
	
	case RTMPVideoFrame::AV1: 
		return VideoCodec::AV1;
	
	case RTMPVideoFrame::VP9: 
		return VideoCodec::VP9;
		
	default:
		return VideoCodec::UNKNOWN;
	}
}

template<typename DescClass, VideoCodec::Type codec>
std::unique_ptr<VideoFrame> RTMPH26xPacketizer<DescClass, codec>::AddFrame(RTMPVideoFrame* videoFrame)
{
	//Debug("-RTMPAVCPacketizer::AddFrame() [size:%u,intra:%d]\n",videoFrame->GetMediaSize(), videoFrame->GetFrameType() == RTMPVideoFrame::INTRA);
	
	//Check it is processing codec
	if (GetRtmpFrameVideoCodec(*videoFrame) != codec)
		//Ignore
		return nullptr;
	
	//Check if it is descriptor
	if (videoFrame->IsConfig())
	{
		//Parse it
		if(desc.Parse(videoFrame->GetMediaData(),videoFrame->GetMediaSize()))
			//Got config
			gotConfig = true;
		else
			//Show error
			Error(" RTMPAVCPacketizer::AddFrame() | AVCDescriptor parse error\n");
		//DOne
		return nullptr;
	}
	
	//It should be a nalu then
	if (!videoFrame->IsCodedFrames())
		//DOne
		return nullptr;
	
	//Ensure that we have got config
	if (!gotConfig)
	{
		//Error
		Debug("-RTMPAVCPacketizer::AddFrame() | Gor NAL frame but not valid description yet\n");
		//DOne
		return nullptr;
	}
	
	//GEt nal header length
	DWORD nalUnitLength = desc.GetNALUnitLengthSizeMinus1() + 1;
	//Create it
	BYTE nalHeader[4];
	
	//Create frame
	auto frame = std::make_unique<VideoFrame>(codec,videoFrame->GetSize()+desc.GetSize()+256);
	
	//Set time
	frame->SetTime(videoFrame->GetTimestamp());
	//Set clock rate
	frame->SetClockRate(1000);
	//Set timestamp
	frame->SetTimestamp(videoFrame->GetTimestamp());
	
	//Get AVC data size
	auto configSize = desc.GetSize();
	//Allocate mem
	BYTE* config = (BYTE*)malloc(configSize);
	//Serialize AVC codec data
	DWORD configLen = desc.Serialize(config,configSize);
	//Set it
	frame->SetCodecConfig(config,configLen);
	//Free data
	free(config);
		
	//If is an intra
	if (videoFrame->GetFrameType()==RTMPVideoFrame::INTRA)
	{
		auto dim = IterateParameters(desc, [&](const uint8_t* data, size_t len) {
			//Set size
			setN(nalUnitLength, nalHeader, 0, len);
			
			//Append nal size header
			frame->AppendMedia(nalHeader, nalUnitLength);
			
			//Append nal
			auto ini = frame->AppendMedia(data, len);
			
			//Crete rtp packet
			frame->AddRtpPacket(ini,len,nullptr,0);
		});
		
		if (dim.has_value())
		{
			frame->SetWidth(dim->first);
			frame->SetHeight(dim->second);
		}		
		
		//Set intra flag
		frame->SetIntra(true);
	}

	//Malloc
	BYTE *data = videoFrame->GetMediaData();
	//Get size
	DWORD size = videoFrame->GetMediaSize();
	
	//Chop into NALs
	while(size>nalUnitLength)
	{
		//Get size
		DWORD nalSize = getN(nalUnitLength, data, 0);
		
		//Ensure we have enougth data
		if (nalSize+nalUnitLength>size)
		{
			//Error
			Error("-RTMPAVCPacketizer::AddFrame() Error adding size=%d nalSize=%d fameSize=%d\n",size,nalSize,videoFrame->GetMediaSize());
			//Skip
			break;
		}

		//Get NAL start
		BYTE* nal = data + nalUnitLength;

		//Skip fill data nalus for h264
		if (codec == VideoCodec::H264 && nal[0] == 12)
		{
			//Skip it
			data += nalUnitLength + nalSize;
			size -= nalUnitLength + nalSize;
			//Next
			continue;
		}

		//Append nal header
		frame->AppendMedia(data, nalUnitLength);

		//Log("-NAL %x size=%d nalSize=%d fameSize=%d\n",nal[0],size,nalSize,videoFrame->GetMediaSize());
		
		//Append nal
		auto ini = frame->AppendMedia(nal,nalSize);
		
		//Skip it
		data+=nalUnitLength+nalSize;
		size-=nalUnitLength+nalSize;
		
		//Check if NAL is bigger than RTPPAYLOADSIZE
		if (nalSize>RTPPAYLOADSIZE)
		{
			std::vector<uint8_t> fragmentationHeader;
			if (codec == VideoCodec::H264)
			{
				//Get original nal type
				BYTE nalUnitType = nal[0] & 0x1f;
				//The fragmentation unit A header, S=1
				/* +---------------+---------------+
				 * |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
				 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
				 * |F|NRI|   28    |S|E|R| Type	   |
				 * +---------------+---------------+
				*/
				fragmentationHeader = {
					(BYTE)((nal[0] & 0xE0) | 28),
					(BYTE)(0x80 | nalUnitType)
				};
				
				//Skip original header
				ini++;
				nalSize--;
			}
			else
			{
				auto naluHeader = get2(nal, 0);
				BYTE nalUnitType		= (naluHeader >> 9) & 0b111111;
				BYTE nuh_layer_id		= (naluHeader >> 3) & 0b111111;
				BYTE nuh_temporal_id_plus1	= naluHeader & 0b111;

				const uint16_t nalHeaderFU = ((uint16_t)(HEVC_RTP_NALU_Type::UNSPEC49_FU) << 9)
									| ((uint16_t)(nuh_layer_id) << 3)
									| ((uint16_t)(nuh_temporal_id_plus1)); 
				fragmentationHeader.resize(3);
				set2(fragmentationHeader.data(), 0, nalHeaderFU);
				fragmentationHeader[2] = nalUnitType | 0x80;
				
				//Skip original header
				ini+= 2;
				nalSize-=2;
			}

			//Add all
			while (nalSize)
			{
				//Get fragment size
				auto fragSize = std::min(nalSize,(DWORD)(RTPPAYLOADSIZE-fragmentationHeader.size()));
				//Check if it is last
				if (fragSize==nalSize)
					//Set end bit
					fragmentationHeader.back() |= 0x40;
				//Add rpt packet info
				frame->AddRtpPacket(ini,fragSize,fragmentationHeader.data(),fragmentationHeader.size());
				//Remove start bit
				fragmentationHeader.back() &= 0x7F;
				//Next
				ini += fragSize;
				//Remove size
				nalSize -= fragSize;
			}
		} else {
			//Add nal unit
			frame->AddRtpPacket(ini,nalSize,nullptr,0);
		}
	}
	//Done
	return frame;
}


template class RTMPH26xPacketizer<AVCDescriptor, VideoCodec::H264>;
template class RTMPH26xPacketizer<HEVCDescriptor, VideoCodec::H265>;


std::unique_ptr<VideoFrame> RTMPAv1Packetizer::AddFrame(RTMPVideoFrame* videoFrame)
{
	//Debug("-RTMPAVCPacketizer::AddFrame() [size:%u,intra:%d]\n",videoFrame->GetMediaSize(), videoFrame->GetFrameType() == RTMPVideoFrame::INTRA);
	
	//Check it is processing codec
	if (GetRtmpFrameVideoCodec(*videoFrame) != VideoCodec::AV1)
		//Ignore
		return nullptr;
	
	//Check if it is descriptor
	if (videoFrame->IsConfig())
	{
		//Parse it
		if(desc.Parse(videoFrame->GetMediaData(),videoFrame->GetMediaSize()))
			//Got config
			gotConfig = true;
		else
			//Show error
			Error(" RTMPAVCPacketizer::AddFrame() | AVCDescriptor parse error\n");
		//DOne
		return nullptr;
	}
	
	//It should be a nalu then
	if (!videoFrame->IsCodedFrames())
		//DOne
		return nullptr;
	
	//Ensure that we have got config
	if (!gotConfig)
	{
		//Error
		Debug("-RTMPAVCPacketizer::AddFrame() | Gor NAL frame but not valid description yet\n");
		//DOne
		return nullptr;
	}

	
	//Create frame
	auto frame = std::make_unique<VideoFrame>(VideoCodec::AV1,videoFrame->GetSize()+desc.GetSize()+256);
	
	//Set time
	frame->SetTime(videoFrame->GetTimestamp());
	//Set clock rate
	frame->SetClockRate(1000);
	//Set timestamp
	frame->SetTimestamp(videoFrame->GetTimestamp());
	
	//Get AVC data size
	auto configSize = desc.GetSize();
	//Allocate mem
	BYTE* config = (BYTE*)malloc(configSize);
	//Serialize AVC codec data
	DWORD configLen = desc.Serialize(config,configSize);
	//Set it
	frame->SetCodecConfig(config,configLen);
	//Free data
	free(config);
	
		
	//If is an intra
	if (videoFrame->GetFrameType()==RTMPVideoFrame::INTRA)
	{
		if (desc.sequenceHeader)
		{
			auto ini = frame->AppendMedia(desc.sequenceHeader->data(),desc.sequenceHeader->size());
			
			//Crete rtp packet
			//frame->AddRtpPacket(ini, + desc.sequenceHeader->size(),nullptr,0);
		
			auto info = ObuHelper::GetObuInfo(desc.sequenceHeader->data(), desc.sequenceHeader->size());
			
			SequenceHeaderObu sho;
			if (info && sho.Parse(info->payload, info->payloadSize))
			{
				//Set dimensions
				frame->SetWidth(sho.max_frame_width_minus_1 + 1);
				frame->SetHeight(sho.max_frame_height_minus_1 + 1);
			}
		}
		
		//Set intra flag
		frame->SetIntra(true);
	}

	//Malloc
	BYTE *data = videoFrame->GetMediaData();
	//Get size
	DWORD size = videoFrame->GetMediaSize();
	
	//Chop into NALs
	while(size>0)
	{
		auto info = ObuHelper::GetObuInfo(data, size);
		if (!info) return nullptr;
		
		if (info->obuSize > size)
		{
			return nullptr;
		}
		
		auto ini = frame->AppendMedia(data, info->obuSize);
		
		data += info->obuSize;
		size -= info->obuSize;
	}

	//Done
	return frame;
}


std::unique_ptr<AudioFrame> RTMPAACPacketizer::AddFrame(RTMPAudioFrame* audioFrame)
{
	//Debug("-RTMPAACPacketizer::AddFrame() [size:%u,aac:%d,codec:%d]\n",audioFrame->GetMediaSize(),audioFrame->GetAACPacketType(),audioFrame->GetAudioCodec());
	
	if (audioFrame->GetAudioCodec()!=RTMPAudioFrame::AAC)
	{
		Debug("-RTMPAACPacketizer::AddFrame() | Skip non-AAC codec\n");
		return nullptr;
	}
	
	//Check if it is AAC descriptor
	if (audioFrame->GetAACPacketType()==RTMPAudioFrame::AACSequenceHeader)
	{
		//Handle specific settings
		gotConfig = aacSpecificConfig.Decode(audioFrame->GetMediaData(),audioFrame->GetMediaSize());
		
		Debug("-RTMPAACPacketizer::AddFrame() | Skip AACSequenceHeader frame\n");
		return nullptr;
	}
	
	if (audioFrame->GetAACPacketType()!=RTMPAudioFrame::AACRaw)
	{
		//DOne
		Debug("-RTMPAACPacketizer::AddFrame() | Skp AACRaw frame\n");
		return nullptr;
	}

	//IF we have aac config
	if (!gotConfig)
	{
		//Error
		Debug("-RTMPAACPacketizer::AddFrame() | Got AAC frame but not valid description yet\n");
		//DOne
		return nullptr;
	}

	if (!aacSpecificConfig.GetRate())
	{
		//Error
		Debug("-RTMPAACPacketizer::AddFrame() | Got zero rate, skipping packet\n");
		//DOne
		return nullptr;
	}

	if (!aacSpecificConfig.GetFrameLength())
	{
		//Error
		Debug("-RTMPAACPacketizer::AddFrame() | Got zero frame length, skipping packet\n");
		//DOne
		return nullptr;
	}


	//Create frame
	auto frame = std::make_unique<AudioFrame>(AudioCodec::AAC);
	
	//Set time in ms
	frame->SetTime(audioFrame->GetTimestamp());
	
	//Calculate timestamp in sample rate clock
	uint64_t timestamp = audioFrame->GetTimestamp() * aacSpecificConfig.GetRate() / 1000;

	//Round up to full frame duration
	timestamp = std::round<uint64_t>((double)timestamp / aacSpecificConfig.GetFrameLength() * aacSpecificConfig.GetFrameLength());

	//Set info from AAC config
	frame->SetClockRate(aacSpecificConfig.GetRate());
	frame->SetNumChannels(aacSpecificConfig.GetChannels());
	frame->SetTimestamp(timestamp);
	frame->SetDuration(aacSpecificConfig.GetFrameLength());

	//Allocate data
	frame->AllocateCodecConfig(aacSpecificConfig.GetSize());
	//Serialize it
	aacSpecificConfig.Serialize(frame->GetCodecConfigData(),frame->GetCodecConfigSize());
	
	//Add aac frame in single rtp 
	auto ini = frame->AppendMedia(audioFrame->GetMediaData(),audioFrame->GetMediaSize());
	frame->AddRtpPacket(ini,audioFrame->GetMediaSize(),nullptr,0);

	//DOne
	return frame;
}
