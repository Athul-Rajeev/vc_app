#pragma once
#include <cstring>
#include <vector>
#include <cstdint>
#include <mutex>
#include <map>
#include <chrono>
#include <RtAudio.h>
#include <opus.h>
#include <cmath>
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
    int m_audioChannelCount;
    int m_frameSize;
    int m_maxPacketSize;

    std::mutex m_dataMutex;
    
    std::map<uint32_t, std::vector<uint8_t>> m_jitterBuffer;
    std::vector<std::vector<uint8_t>> m_outgoingPackets;
    
    uint32_t m_sequenceCounter;
    uint32_t m_lastPlayedSequence;
    bool m_isBuffering;
    int m_vadHoldFrames;
};