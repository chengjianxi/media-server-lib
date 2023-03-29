#include "VP8TestBase.h"
#include "vp8/VP8LayerSelector.h"
#include <limits>

class TestVP8LayerSelector : public VP8TestBase, public testing::WithParamInterface<std::pair<uint16_t, uint8_t>>
{
public:
    TestVP8LayerSelector() :
        VP8TestBase(GetParam().first, GetParam().second),
        startPicId(GetParam().first),
        startTl0PicId(GetParam().second)
    {};

    void Add(std::shared_ptr<RTPPacket> packet, uint16_t expectedPicId, uint16_t expectedTl0PicIdx)
    {
        selector.UpdateSelectedPacketForSending(packet);
        ASSERT_EQ(expectedPicId, packet->vp8PayloadDescriptor->pictureId);
        ASSERT_EQ(expectedTl0PicIdx, packet->vp8PayloadDescriptor->temporalLevelZeroIndex);
    }

    constexpr uint16_t GetExpectedPicId(int offset)
    {
        return startPicId + offset;
    }

    constexpr uint8_t GetExpectedTl0PicId(int offset)
    {
        return startTl0PicId + offset;
    }


protected:
    uint16_t startPicId = 0;
    uint8_t startTl0PicId = 0;
    VP8LayerSelector selector;
};

TEST_P(TestVP8LayerSelector, NoDropping)
{
    // Single partition
    ASSERT_NO_FATAL_FAILURE(Add(StartPacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MarkerPacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));

    // Multiple partitions
    ASSERT_NO_FATAL_FAILURE(Add(StartPacket(2000, 0),  GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(2000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(2000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(2000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(StartPacket(2000, 1), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(2000, 1), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(2000, 1), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MarkerPacket(2000, 1), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
}


TEST_P(TestVP8LayerSelector, Dropping)
{
    // Single partition
    ASSERT_NO_FATAL_FAILURE(Add(StartPacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));
    ASSERT_NO_FATAL_FAILURE(Add(MarkerPacket(1000, 0), GetExpectedPicId(1), GetExpectedTl0PicId(1)));

    // Generate packets and not add it to simulate dropping
    (void)StartPacket(2000, 0);
    (void)MiddlePacket(2000, 0);
    (void)MarkerPacket(2000, 0);

    ASSERT_EQ(GetExpectedPicId(2), currentPicId);
    ASSERT_EQ(GetExpectedTl0PicId(2), currentTl0PicId);

    // Another dropping frame without marker packet
    (void)StartPacket(2050, 0);
    (void)MiddlePacket(2050, 0);

    ASSERT_EQ(GetExpectedPicId(3), currentPicId);
    ASSERT_EQ(GetExpectedTl0PicId(3), currentTl0PicId);

    // The pic ID and layer 0 pic index is still continous
    ASSERT_NO_FATAL_FAILURE(Add(StartPacket(3000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(3000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MiddlePacket(3000, 0), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
    ASSERT_NO_FATAL_FAILURE(Add(MarkerPacket(3000, 1), GetExpectedPicId(2), GetExpectedTl0PicId(2)));
}

INSTANTIATE_TEST_SUITE_P(TestVP8LayerSelectorCases,
                         TestVP8LayerSelector,
                         testing::Values(std::pair(0, 0),
                                         std::pair(100, 50),
                                         std::pair(std::numeric_limits<uint16_t>::max(), std::numeric_limits<uint8_t>::max())));