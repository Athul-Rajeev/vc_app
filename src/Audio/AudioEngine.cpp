#include "Audio/AudioEngine.hpp"
#include <chrono>

AudioEngine::AudioEngine()
{
    m_sampleRate = 48000;
    m_audioChannelCount = 1;
    m_frameSize = 960; 
    m_opusEncoder = nullptr;
    m_vadHoldFrames = 0;
    
    m_sequenceCounter.store(1, std::memory_order_relaxed);
}

AudioEngine::~AudioEngine()
{
    stopStream();

    if (m_opusEncoder != nullptr)
    {
        opus_encoder_destroy(m_opusEncoder);
    }
}

bool AudioEngine::initialize()
{
    int opusError = OPUS_OK;

    m_opusEncoder = opus_encoder_create(m_sampleRate, m_audioChannelCount, OPUS_APPLICATION_VOIP, &opusError);
    if (opusError != OPUS_OK)
    {
        std::cerr << "Failed to create Opus encoder." << std::endl;
        return false;
    }

    if (m_audioSystem.getDeviceCount() < 1)
    {
        std::cerr << "No audio devices found." << std::endl;
        return false;
    }

    return true;
}

void AudioEngine::startStream()
{
    if (!m_audioSystem.isStreamOpen())
    {
        RtAudio::StreamParameters outputParams;
        outputParams.deviceId = m_audioSystem.getDefaultOutputDevice();
        outputParams.nChannels = m_audioChannelCount;
        outputParams.firstChannel = 0;

        RtAudio::StreamParameters inputParams;
        inputParams.deviceId = m_audioSystem.getDefaultInputDevice();
        inputParams.nChannels = m_audioChannelCount;
        inputParams.firstChannel = 0;

        unsigned int bufferFrames = m_frameSize;

        RtAudio::StreamOptions options;
        options.flags = RTAUDIO_SCHEDULE_REALTIME;
        options.priority = 15; 

        unsigned int streamStatus = m_audioSystem.openStream(&outputParams, &inputParams, RTAUDIO_SINT16, m_sampleRate, &bufferFrames, &routingCallback, this, &options);
        
        if (streamStatus != 0)
        {
            std::cerr << "Failed to open audio stream: " << m_audioSystem.getErrorText() << std::endl;
            return;
        }
    }

    if (m_audioSystem.isStreamOpen() && !m_audioSystem.isStreamRunning())
    {
        if (m_audioSystem.startStream() != 0)
        {
            std::cerr << "Failed to start audio stream: " << m_audioSystem.getErrorText() << std::endl;
        }
    }
}

void AudioEngine::stopStream()
{
    if (m_audioSystem.isStreamRunning())
    {
        if (m_audioSystem.stopStream() != 0)
        {
            std::cerr << "Failed to stop audio stream: " << m_audioSystem.getErrorText() << std::endl;
        }
    }

    if (m_audioSystem.isStreamOpen())
    {
        m_audioSystem.closeStream();
    }
}

int AudioEngine::routingCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData)
{
    AudioEngine* engineInstance = static_cast<AudioEngine*>(userData);
    return engineInstance->processHardwareBuffers(static_cast<int16_t*>(outputBuffer), static_cast<const int16_t*>(inputBuffer), nFrames);
}

int AudioEngine::processHardwareBuffers(int16_t* outputBuffer, const int16_t* inputBuffer, unsigned int nFrames)
{
    if (inputBuffer != nullptr)
    {
        double sumOfSquares = 0.0;
        int totalSamples = nFrames * m_audioChannelCount;
        for (int sampleIndex = 0; sampleIndex < totalSamples; ++sampleIndex)
        {
            double normalizedSample = inputBuffer[sampleIndex] / 32768.0;
            sumOfSquares += normalizedSample * normalizedSample;
        }
        double rootMeanSquare = std::sqrt(sumOfSquares / totalSamples);

        double voiceActivationThreshold = 0.015;
        if (rootMeanSquare > voiceActivationThreshold)
        {
            m_vadHoldFrames = 25; 
        }

        if (m_vadHoldFrames > 0)
        {
            m_vadHoldFrames--;
            
            AudioPacket packet;
            uint32_t currentSequence = m_sequenceCounter.fetch_add(1, std::memory_order_relaxed);
            uint64_t currentTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            
            std::memcpy(packet.data, &currentSequence, sizeof(uint32_t));
            std::memcpy(packet.data + sizeof(uint32_t), &currentTimestamp, sizeof(uint64_t));

            int bytesEncoded = opus_encode(m_opusEncoder, inputBuffer, nFrames, packet.data + 12, MaxAudioPacketSize - 12);

            if (bytesEncoded > 0)
            {
                packet.size = 12 + bytesEncoded;
                m_outgoingPackets.forcePush(packet);
            }
        }
    }

    if (outputBuffer != nullptr)
    {
        std::memset(outputBuffer, 0, nFrames * m_audioChannelCount * sizeof(int16_t));
        m_peerMixer.mixAudio(outputBuffer, nFrames, m_audioChannelCount);
    }

    return 0;
}

std::vector<uint8_t> AudioEngine::getOutgoingPacket()
{
    std::vector<uint8_t> packetData;
    AudioPacket internalPacket;

    if (m_outgoingPackets.pop(internalPacket))
    {
        packetData.assign(internalPacket.data, internalPacket.data + internalPacket.size);
    }

    return packetData;
}

void AudioEngine::pushIncomingPacket(const std::string& senderUuid, const std::vector<uint8_t>& opusPacket)
{
    if (opusPacket.size() <= 12 || opusPacket.size() > MaxAudioPacketSize)
    {
        return;
    }

    uint32_t sequenceNumber;
    std::memcpy(&sequenceNumber, opusPacket.data(), sizeof(uint32_t));
    
    m_peerMixer.pushPacket(senderUuid, sequenceNumber, opusPacket.data() + 12, opusPacket.size() - 12);
}

void AudioEngine::resetBuffers()
{
    AudioPacket dummy;
    while (m_outgoingPackets.pop(dummy)) 
    {
    }
    
    m_peerMixer.reset();
    
    m_sequenceCounter.store(1, std::memory_order_release);
    m_vadHoldFrames = 0;
}