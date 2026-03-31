#pragma once

#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity>
class LockFreeQueue
{
public:
    LockFreeQueue()
    {
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

    void forcePush(const T& item)
    {
        size_t currentTail = m_tail.load(std::memory_order_relaxed);
        size_t nextTail = (currentTail + 1) % Capacity;

        m_buffer[currentTail] = item;
        m_tail.store(nextTail, std::memory_order_release);

        size_t currentHead = m_head.load(std::memory_order_acquire);
        if (nextTail == currentHead)
        {
            m_head.store((currentHead + 1) % Capacity, std::memory_order_release);
        }
    }

    bool pop(T& outItem)
    {
        size_t currentHead = m_head.load(std::memory_order_relaxed);
        if (currentHead == m_tail.load(std::memory_order_acquire))
        {
            return false; 
        }

        outItem = m_buffer[currentHead];
        m_head.store((currentHead + 1) % Capacity, std::memory_order_release);
        
        return true;
    }

    bool isEmpty() const
    {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }

private:
    T m_buffer[Capacity];
    std::atomic<size_t> m_head;
    std::atomic<size_t> m_tail;
};