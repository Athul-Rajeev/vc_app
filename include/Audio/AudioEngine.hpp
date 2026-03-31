#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <RtAudio.h>
#include <opus.h>
#include <cmath>
#include <iostream>

#include "LockFreeQueue.hpp"
#include "PeerMixer.hpp"

constexpr size_t MaxAudioPacketSize = 4000;
constexpr size_t OutgoingQueueCapacity = 100;

struct AudioPacket
{
    uint8_t data[MaxAudioPacketSize];
    size_t size = 0;
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void startStream();
    void stopStream();

    std::vector<uint8_t> getOutgoingPacket();
    void pushIncomingPacket(const std::string& senderUuid, const std::vector<uint8_t>& opusPacket);
    void resetBuffers();

private:
    static int routingCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData);

    int processHardwareBuffers(int16_t* outputBuffer, const int16_t* inputBuffer, unsigned int nFrames);

    RtAudio m_audioSystem;
    OpusEncoder* m_opusEncoder;

    int m_sampleRate;
    int m_audioChannelCount;
    int m_frameSize;

    PeerMixer m_peerMixer;
    LockFreeQueue<AudioPacket, OutgoingQueueCapacity> m_outgoingPackets;
    
    std::atomic<uint32_t> m_sequenceCounter;
    int m_vadHoldFrames;
};