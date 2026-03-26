#include "Audio/AudioEngine.hpp"
#include <iostream>
#include <cstring>

AudioEngine::AudioEngine()
{
    m_sampleRate = 48000;
    m_audioChannelCount = 1;
    m_frameSize = 960; 
    m_maxPacketSize = 4000;
    m_opusEncoder = nullptr;
    m_opusDecoder = nullptr;
    m_vadHoldFrames = 0;
}

AudioEngine::~AudioEngine()
{
    stopStream();

    if (m_opusEncoder != nullptr)
    {
        opus_encoder_destroy(m_opusEncoder);
    }

    if (m_opusDecoder != nullptr)
    {
        opus_decoder_destroy(m_opusDecoder);
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

    m_opusDecoder = opus_decoder_create(m_sampleRate, m_audioChannelCount, &opusError);
    if (opusError != OPUS_OK)
    {
        std::cerr << "Failed to create Opus decoder." << std::endl;
        return false;
    }

    if (m_audioSystem.getDeviceCount() < 1)
    {
        std::cerr << "No audio devices found." << std::endl;
        return false;
    }

    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = m_audioSystem.getDefaultOutputDevice();
    outputParams.nChannels = m_audioChannelCount;
    outputParams.firstChannel = 0;

    RtAudio::StreamParameters inputParams;
    inputParams.deviceId = m_audioSystem.getDefaultInputDevice();
    inputParams.nChannels = m_audioChannelCount;
    inputParams.firstChannel = 0;

    unsigned int bufferFrames = m_frameSize;

    // RtAudio v6+ returns 0 on success instead of throwing exceptions
    unsigned int streamStatus = m_audioSystem.openStream(&outputParams, &inputParams, RTAUDIO_SINT16, m_sampleRate, &bufferFrames, &routingCallback, this);
    
    if (streamStatus != 0)
    {
        std::cerr << "Failed to open audio stream: " << m_audioSystem.getErrorText() << std::endl;
        return false;
    }

    return true;
}

void AudioEngine::startStream()
{
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
    std::lock_guard<std::mutex> lockGuard(m_dataMutex);

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

        double voiceActivationThreshold = 0.015; // roughly -36 dB
        if (rootMeanSquare > voiceActivationThreshold)
        {
            m_vadHoldFrames = 25; // Hold for ~500ms (25 chunks of 20ms)
        }

        if (m_vadHoldFrames > 0)
        {
            m_vadHoldFrames--;
            
            std::vector<uint8_t> encodedData(m_maxPacketSize);
            int bytesEncoded = opus_encode(m_opusEncoder, inputBuffer, nFrames, encodedData.data(), m_maxPacketSize);

            if (bytesEncoded > 0)
            {
                encodedData.resize(bytesEncoded);
                m_outgoingPackets.push(encodedData);
            }
        }
    }

    if (outputBuffer != nullptr)
    {
        if (!m_incomingPackets.empty())
        {
            std::vector<uint8_t> packetData = m_incomingPackets.front();
            m_incomingPackets.pop();

            int decodeResult = opus_decode(m_opusDecoder, packetData.data(), packetData.size(), outputBuffer, nFrames, 0);
            
            // If the packet was corrupted and decoding failed, play silence
            if (decodeResult < 0)
            {
                std::memset(outputBuffer, 0, nFrames * m_audioChannelCount * sizeof(int16_t));
            }
        }
        else
        {
            std::memset(outputBuffer, 0, nFrames * m_audioChannelCount * sizeof(int16_t));
        }
    }

    return 0;
}

std::vector<uint8_t> AudioEngine::getOutgoingPacket()
{
    std::lock_guard<std::mutex> lockGuard(m_dataMutex);
    std::vector<uint8_t> packetData;

    if (!m_outgoingPackets.empty())
    {
        packetData = m_outgoingPackets.front();
        m_outgoingPackets.pop();
    }

    return packetData;
}

void AudioEngine::pushIncomingPacket(const std::vector<uint8_t>& opusPacket)
{
    std::lock_guard<std::mutex> lockGuard(m_dataMutex);
    m_incomingPackets.push(opusPacket);
}