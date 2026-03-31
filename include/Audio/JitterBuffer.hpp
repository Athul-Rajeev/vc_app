#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>

template <size_t Capacity, size_t MaxPacketSize>
class JitterBuffer
{
public:
    JitterBuffer()
    {
        for (size_t index = 0; index < Capacity; ++index)
        {
            m_sequenceTracker[index].store(0, std::memory_order_relaxed);
            m_packetSizes[index].store(0, std::memory_order_relaxed);
        }
        m_readHead.store(0, std::memory_order_relaxed);
    }

    void push(uint32_t sequence, const uint8_t* payload, size_t size)
    {
        if (size > MaxPacketSize || size == 0)
        {
            return;
        }

        uint32_t currentReadHead = m_readHead.load(std::memory_order_acquire);

        if (currentReadHead > 0 && sequence < currentReadHead)
        {
            return;
        }

        size_t slotIndex = sequence % Capacity;

        std::memcpy(m_buffer[slotIndex], payload, size);
        m_packetSizes[slotIndex].store(size, std::memory_order_release);
        m_sequenceTracker[slotIndex].store(sequence, std::memory_order_release);
    }

    bool pop(uint32_t expectedSequence, uint8_t* outPayload, size_t& outSize)
    {
        size_t slotIndex = expectedSequence % Capacity;

        uint32_t storedSequence = m_sequenceTracker[slotIndex].load(std::memory_order_acquire);

        if (storedSequence == expectedSequence)
        {
            outSize = m_packetSizes[slotIndex].load(std::memory_order_acquire);
            std::memcpy(outPayload, m_buffer[slotIndex], outSize);

            m_sequenceTracker[slotIndex].store(0, std::memory_order_release);
            m_readHead.store(expectedSequence + 1, std::memory_order_release);

            return true;
        }

        return false;
    }

    void reset()
    {
        m_readHead.store(0, std::memory_order_release);
        for (size_t index = 0; index < Capacity; ++index)
        {
            m_sequenceTracker[index].store(0, std::memory_order_release);
        }
    }

private:
    uint8_t m_buffer[Capacity][MaxPacketSize];
    std::atomic<uint32_t> m_sequenceTracker[Capacity];
    std::atomic<size_t> m_packetSizes[Capacity];
    std::atomic<uint32_t> m_readHead;
};