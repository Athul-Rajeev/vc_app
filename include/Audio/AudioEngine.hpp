#pragma once
#include <iostream>
#include <cstring>
#include <vector>
#include <cstdint>
#include <mutex>
#include <queue>
#include <cmath>
#include <RtAudio.h>
#include <opus.h>

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void startStream();
    void stopStream();

    std::vector<uint8_t> getOutgoingPacket();
    void pushIncomingPacket(const std::vector<uint8_t>& opusPacket);

private:
    static int routingCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData);

    int processHardwareBuffers(int16_t* outputBuffer, const int16_t* inputBuffer, unsigned int nFrames);

    RtAudio m_audioSystem;
    OpusEncoder* m_opusEncoder;
    OpusDecoder* m_opusDecoder;

    int m_sampleRate;
    int m_channels;
    int m_frameSize;
    int m_maxPacketSize;

    std::mutex m_dataMutex;
    std::queue<std::vector<uint8_t>> m_incomingPackets;
    std::queue<std::vector<uint8_t>> m_outgoingPackets;
    
    int m_vadHoldFrames;
};