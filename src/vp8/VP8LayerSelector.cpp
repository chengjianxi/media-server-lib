 #include "VP8LayerSelector.h"
#include "vp8.h"



VP8LayerSelector::VP8LayerSelector()
{
	waitingForIntra = true;
	temporalLayerId = 0;
	nextTemporalLayerId = LayerInfo::MaxLayerId;
}


void VP8LayerSelector::SelectTemporalLayer(BYTE id)
{
	//Check if its the same
	if (id==nextTemporalLayerId)
		//Do nothing
		return;
	//Log
	UltraDebug("-SelectTemporalLayer [layerId:%d,prev:%d,current:%d]\n",id,nextTemporalLayerId,temporalLayerId);
	//Set next
	nextTemporalLayerId = id;
}

void VP8LayerSelector::SelectSpatialLayer(BYTE id)
{
	//Not supported
}

bool VP8LayerSelector::Select(const RTPPacket::shared& packet,bool &mark)
{
	//Get header and description from packet
	auto& desc   = packet->vp8PayloadDescriptor;
	auto& header = packet->vp8PayloadHeader;

	//Check that you are having a descriptor
	if (!desc)
		//Error
		return 0;

	//UltraDebug("-intra:%d\t tl:%u\t picId:%u\t tl0picIdx:%u\t sync:%u\t waitingForIntra:%d\n",header && header->isKeyFrame,desc->temporalLayerIndex,desc->pictureId,desc->temporalLevelZeroIndex,desc->layerSync,waitingForIntra);

	//If we have to wait for first intra
	if (waitingForIntra)
	{
		//If this is not intra
		if (!header || !header->isKeyFrame)
			//Discard
			return 0;
		//Stop waiting
		waitingForIntra = 0;
	}

	//Store current temporal id
	BYTE currentTemporalLayerId = temporalLayerId;

	//Check if we need to upscale temporally
	if (nextTemporalLayerId>temporalLayerId)
	{
		//Check if we can upscale and it is the start of the layer and it is a layer higher than current
		if (desc->layerSync && desc->startOfPartition && desc->temporalLayerIndex>currentTemporalLayerId && desc->temporalLayerIndex<=nextTemporalLayerId)
		{
			UltraDebug("-VP8LayerSelector::Select() | Upscaling temporalLayerId [id:%d,current:%d,target:%d]\n",desc->temporalLayerIndex,currentTemporalLayerId,nextTemporalLayerId);
			//Update current layer
			temporalLayerId = desc->temporalLayerIndex;
			currentTemporalLayerId = temporalLayerId;
		}
	//Check if we need to downscale
	} else if (nextTemporalLayerId<temporalLayerId) {
		//We can only downscale on the end of a frame
		if (packet->GetMark())
		{
			UltraDebug("-VP8LayerSelector::Select() | Downscaling temporalLayerId [id:%d,current:%d,target:%d]\n",temporalLayerId,currentTemporalLayerId,nextTemporalLayerId);
			//Update to target layer for next packets
			temporalLayerId = nextTemporalLayerId;
		}
	}

	//If it is not valid for the current layer
	if (currentTemporalLayerId<desc->temporalLayerIndex)
	{
		//UltraDebug("-VP8LayerSelector::Select() | dropping packet based on temporalLayerId [current:%d,desc:%d,mark:%d]\n",currentTemporalLayerId,desc->temporalLayerIndex,packet->GetMark());
		//Drop it
		return false;
	}

	//RTP mark is unchanged
	mark = packet->GetMark();

	//UltraDebug("-VP8LayerSelector::Select() | Accepting packet [extSegNum:%u,mark:%d,desc->tid:%d,current:%d]\n",packet->GetExtSeqNum(),mark,desc->temporalLayerIndex,currentTemporalLayerId);

	//Select
	return true;
}


void VP8LayerSelector::UpdateSelectedPacketForSending(RTPPacket::shared packet)
{
	//Rewrite pict id
	bool rewitePictureIds = false;
	uint16_t pictureId = 0;
	uint8_t temporalLevelZeroIndex = 0;

	if (!packet->vp8PayloadDescriptor)
	{
		UltraDebug("VP8 packet missing payload descriptor");
		return;
	}

	//Get VP8 desc
	auto desc = *packet->vp8PayloadDescriptor;

	//Check if we have a new pictId
	if (desc.pictureIdPresent)
	{
		if (desc.pictureId != lastSrcPicId || !picId)
		{
			if (!picId)
			{
				picId = desc.pictureId;
			}
			else
			{
				//Increase picture id
				(*picId)++;
			}
		}

		pictureId = *picId;

		//We may need to rewrite vp8 picture ids
		rewitePictureIds = pictureId != desc.pictureId;

		//Update ids
		lastSrcPicId = desc.pictureId;
	}

	//Check if we have a new base layer
	if (desc.temporalLevelZeroIndexPresent)
	{
		if (desc.temporalLayerIndexPresent && desc.temporalLayerIndex != 0)
		{
			// If it is not a base layer, apply the pic id offset
			temporalLevelZeroIndex = uint8_t(int(desc.temporalLevelZeroIndex) + tl0PicIdxOffset);
		}
		else
		{
			if (desc.temporalLevelZeroIndex!=lastSrcTl0PicIdx || !tl0PicIdx)
			{
				if (!tl0PicIdx)
				{
					tl0PicIdx = desc.temporalLevelZeroIndex;
				}
				else
				{
					//Increase tl0 index
					(*tl0PicIdx)++;
				}
			}

			tl0PicIdxOffset = int(*tl0PicIdx) - int(desc.temporalLevelZeroIndex);

			temporalLevelZeroIndex = *tl0PicIdx;

			//Update ids
			lastSrcTl0PicIdx = desc.temporalLevelZeroIndex;
		}

		//We may need to rewrite vp8 picture ids
		rewitePictureIds |= temporalLevelZeroIndex != desc.temporalLevelZeroIndex;
	}

	packet->rewitePictureIds = rewitePictureIds;

	//Rewrite picture id
	packet->vp8PayloadDescriptor->pictureId = pictureId;
	//Rewrite tl0 index
	packet->vp8PayloadDescriptor->temporalLevelZeroIndex = temporalLevelZeroIndex;

	//Error("-ext seq:%lu pictureIdPresent:%d rewrite:%d pictId:%d lastPictId:%d origPictId:%d intra:%d mark:%d \n",extSeqNum, desc.pictureIdPresent, rewitePictureIds, pictureId, lastSrcPicId, packet->vp8PayloadDescriptor ? packet->vp8PayloadDescriptor->pictureId : -1, packet->IsKeyFrame(), packet->GetMark());
}

std::vector<LayerInfo> VP8LayerSelector::GetLayerIds(const RTPPacket::shared& packet)
{
	std::vector<LayerInfo> infos;

	//Check if it already have the descriptor
	if (!packet->vp8PayloadDescriptor)
	{
		//Create new one
		auto& desc = packet->vp8PayloadDescriptor.emplace();

		//Try to parse
		int len = desc.Parse(packet->GetMediaData(),packet->GetMediaLength());

		//If error
		if (!len)
		{
			//Clear desc
			packet->vp8PayloadDescriptor.reset();
			//NOne
			return infos;
		}

		//Parse header if first packet
		if (desc.startOfPartition && desc.partitionIndex==0)
		{
			//Create header
			auto &header = packet->vp8PayloadHeader.emplace();

			//parse it
			if (header.Parse(packet->GetMediaData()+len,packet->GetMediaLength()-len))
			{
				//Set key frame
				packet->SetKeyFrame(header.isKeyFrame);
			} else {
				//Clear desc
				packet->vp8PayloadDescriptor.reset();
				packet->vp8PayloadHeader.reset();
			}
		}
	}

	//If we got the descriptor
	if (packet->vp8PayloadDescriptor)
		//Set temporal layer info
		infos.emplace_back(packet->vp8PayloadDescriptor->temporalLayerIndex,0);
	//UltraDebug("-VP8LayerSelector::GetLayerIds() | [tid:%u,sid:%u]\n",info.temporalLayerId,info.spatialLayerId);
	return infos;
}
