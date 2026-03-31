#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <opus.h>
#include "JitterBuffer.hpp"

// Assuming 48000Hz sample rate and 960 frame size (50 frames per second)
constexpr int FramesPerSecond = 50;
constexpr int StaleTimeoutFrames = 2 * FramesPerSecond;

struct PeerAudioState
{
    JitterBuffer<50, 4000> m_jitterBuffer;
    OpusDecoder* m_opusDecoder = nullptr;
    uint32_t m_lastPlayedSequence = 0;
    int m_timeoutCounter = 0;
    bool m_isBuffering = true;

    ~PeerAudioState()
    {
        if (m_opusDecoder != nullptr)
        {
            opus_decoder_destroy(m_opusDecoder);
            m_opusDecoder = nullptr;
        }
    }
};

class PeerMixer
{
public:
    PeerMixer();
    ~PeerMixer();

    void pushPacket(const std::string& uuid, uint32_t sequenceNumber, const uint8_t* payload, size_t payloadSize);
    void mixAudio(int16_t* outputBuffer, unsigned int nFrames, int audioChannelCount);
    void reset();

private:
    std::unordered_map<std::string, std::shared_ptr<PeerAudioState>> m_peerMap;
    std::mutex m_mapMutex;
    int m_sampleRate;
};