#include "Audio/PeerMixer.hpp"
#include <iostream>

PeerMixer::PeerMixer()
{
    m_sampleRate = 48000;
}

PeerMixer::~PeerMixer()
{
    reset();
}

void PeerMixer::pushPacket(const std::string& uuid, uint32_t sequenceNumber, const uint8_t* payload, size_t payloadSize)
{
    std::lock_guard<std::mutex> lock(m_mapMutex);

    auto it = m_peerMap.find(uuid);
    if (it == m_peerMap.end())
    {
        int opusError = OPUS_OK;
        OpusDecoder* newDecoder = opus_decoder_create(m_sampleRate, 1, &opusError);
        
        if (opusError != OPUS_OK)
        {
            std::cerr << "Failed to create Opus decoder for peer: " << uuid << std::endl;
            return;
        }

        auto newState = std::make_shared<PeerAudioState>();
        newState->m_opusDecoder = newDecoder;
        newState->m_lastPlayedSequence = sequenceNumber - 1;
        newState->m_isBuffering = false;

        m_peerMap[uuid] = newState;
        it = m_peerMap.find(uuid);
    }

    auto& peerState = it->second;

    if (!peerState->m_isBuffering && sequenceNumber <= peerState->m_lastPlayedSequence)
    {
        return;
    }

    peerState->m_jitterBuffer.push(sequenceNumber, payload, payloadSize);
}

void PeerMixer::mixAudio(int16_t* outputBuffer, unsigned int nFrames, int audioChannelCount)
{
    std::lock_guard<std::mutex> lock(m_mapMutex);

    std::vector<int32_t> mixBuffer(nFrames * audioChannelCount, 0);
    std::vector<int16_t> tempBuffer(nFrames * audioChannelCount, 0);

    for (auto it = m_peerMap.begin(); it != m_peerMap.end(); )
    {
        auto& peerState = it->second;
        
        uint32_t expectedSequence = peerState->m_lastPlayedSequence + 1;
        uint8_t payloadData[4000];
        size_t payloadSize = 0;

        bool hasPacket = peerState->m_jitterBuffer.pop(expectedSequence, payloadData, payloadSize);

        if (hasPacket)
        {
            int decodeResult = opus_decode(peerState->m_opusDecoder, payloadData, payloadSize, tempBuffer.data(), nFrames, 0);
            if (decodeResult >= 0)
            {
                peerState->m_timeoutCounter = 0;
                for (unsigned int i = 0; i < nFrames * audioChannelCount; ++i)
                {
                    mixBuffer[i] += tempBuffer[i];
                }
                peerState->m_lastPlayedSequence = expectedSequence;
            }
            else
            {
                // Decode failed on a real packet — log it, increment timeout,
                // but do NOT advance sequence so the gap is acknowledged
                std::cerr << "opus_decode error (peer packet): " << opus_strerror(decodeResult)
                        << " for sequence " << expectedSequence << std::endl;
                peerState->m_timeoutCounter++;
                // Do NOT update m_lastPlayedSequence here
            }
        }
        else
        {
            // PacketLossConcealment path — missing packet
            int decodeResult = opus_decode(peerState->m_opusDecoder, nullptr, 0, tempBuffer.data(), nFrames, 0);
            if (decodeResult >= 0)
            {
                peerState->m_timeoutCounter++;
                for (unsigned int i = 0; i < nFrames * audioChannelCount; ++i)
                {
                    mixBuffer[i] += tempBuffer[i];
                }
            }
            else
            {
                std::cerr << "opus_decode PacketLossConcealment error: " << opus_strerror(decodeResult) << std::endl;
                peerState->m_timeoutCounter++; // still count toward eviction
            }

            peerState->m_lastPlayedSequence = expectedSequence; // advance regardless on PacketLossConcealment path
        }

        if (peerState->m_timeoutCounter >= StaleTimeoutFrames)
        {
            it = m_peerMap.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (unsigned int i = 0; i < nFrames * audioChannelCount; ++i)
    {
        if (mixBuffer[i] > 32767)
        {
            outputBuffer[i] = 32767;
        }
        else if (mixBuffer[i] < -32768)
        {
            outputBuffer[i] = -32768;
        }
        else
        {
            outputBuffer[i] = static_cast<int16_t>(mixBuffer[i]);
        }
    }
}

void PeerMixer::reset()
{
    std::lock_guard<std::mutex> lock(m_mapMutex);
    m_peerMap.clear();
}