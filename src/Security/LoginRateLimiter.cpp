// LoginRateLimiter.cpp
#include "Security/LoginRateLimiter.hpp"

#include <algorithm>
#include <cmath>

namespace
{
// Login throttling (per identity key: source IP and/or "account:<username>").
constexpr int kMaxFailuresPerWindow = 5;
constexpr auto kFailureWindow = std::chrono::seconds{600};        // 10 minutes
constexpr auto kInitialLockout = std::chrono::seconds{1};        // doubles with each extra failure
constexpr auto kMaxLockout = std::chrono::seconds{900};          // 15 minutes (never permanent)
constexpr int kMaxDoublings = 10;                                // caps 1 << exponent overflow

// New-account budget (prevents mass account spam from one address).
constexpr int kMaxRegistrationsPerWindow = 3;
constexpr auto kRegistrationWindow = std::chrono::seconds{3600}; // 1 hour

// Housekeeping.
constexpr auto kStaleEntryExpiration = std::chrono::seconds{86400}; // untouched record lifetime (24 h)
constexpr auto kSweepInterval = std::chrono::seconds{60};
}

LoginRateLimiter::LoginRateLimiter() = default;

LoginRateLimiter::~LoginRateLimiter() = default;

int LoginRateLimiter::secondsUntilAllowed(const std::string& identityKey)
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    sweepStaleEntriesLocked();

    auto recordIterator = m_attemptRecords.find(identityKey);
    if (recordIterator == m_attemptRecords.end())
    {
        return 0;
    }

    AttemptRecord& record = recordIterator->second;
    dropExpiredLockoutsLocked(record);

    auto timeNow = std::chrono::steady_clock::now();
    if (record.lockedUntil > timeNow)
    {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(record.lockedUntil - timeNow).count();
        return std::max<int>(1, static_cast<int>(remaining));
    }
    return 0;
}

int LoginRateLimiter::recordFailure(const std::string& identityKey)
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    sweepStaleEntriesLocked();

    AttemptRecord& record = m_attemptRecords[identityKey];
    auto timeNow = std::chrono::steady_clock::now();

    // Sliding failure budget: restart the window when it has fully elapsed.
    if (std::chrono::duration_cast<std::chrono::seconds>(timeNow - record.windowStart) > kFailureWindow)
    {
        record.windowStart = timeNow;
        record.failureCount = 0;
    }
    record.failureCount++;

    if (record.failureCount <= kMaxFailuresPerWindow)
    {
        return 0;
    }

    // Progressive backoff: first over-budget failure locks 1 s, then 2 s,
    // 4 s ... capped at kMaxLockout so lockouts always expire on their own.
    int doublings = std::min(kMaxDoublings, record.failureCount - kMaxFailuresPerWindow - 1);
    auto lockout = kInitialLockout * (1 << doublings);
    lockout = std::min(lockout, kMaxLockout);

    record.lockedUntil = record.lockedUntil > timeNow ? record.lockedUntil + lockout : timeNow + lockout;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(record.lockedUntil - timeNow).count());
}

void LoginRateLimiter::recordSuccess(const std::string& identityKey)
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    m_attemptRecords.erase(identityKey);
}

bool LoginRateLimiter::isAllowedToRegister(const std::string& identityKey)
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    auto timestampsIterator = m_registrationTimestamps.find(identityKey);
    if (timestampsIterator == m_registrationTimestamps.end())
    {
        return true;
    }

    auto timeNow = std::chrono::steady_clock::now();
    auto& timestamps = timestampsIterator->second;
    timestamps.erase(
        std::remove_if(timestamps.begin(), timestamps.end(),
                       [timeNow](const std::chrono::steady_clock::time_point& timestamp)
                       {
                           return std::chrono::duration_cast<std::chrono::seconds>(timeNow - timestamp) > kRegistrationWindow;
                       }),
        timestamps.end());

    return static_cast<int>(timestamps.size()) < kMaxRegistrationsPerWindow;
}

void LoginRateLimiter::recordRegistration(const std::string& identityKey)
{
    std::lock_guard<std::mutex> lock(m_recordMutex);
    m_registrationTimestamps[identityKey].push_back(std::chrono::steady_clock::now());
}

void LoginRateLimiter::sweepStaleEntriesLocked()
{
    auto timeNow = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(timeNow - m_lastSweepTime) < kSweepInterval)
    {
        return;
    }
    m_lastSweepTime = timeNow;

    for (auto iterator = m_attemptRecords.begin(); iterator != m_attemptRecords.end();)
    {
        AttemptRecord& record = iterator->second;
        dropExpiredLockoutsLocked(record);
        bool windowExpired = std::chrono::duration_cast<std::chrono::seconds>(timeNow - record.windowStart) > kFailureWindow;
        if (windowExpired && record.lockedUntil <= timeNow)
        {
            iterator = m_attemptRecords.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    for (auto iterator = m_registrationTimestamps.begin(); iterator != m_registrationTimestamps.end();)
    {
        auto timeNowCopy = timeNow;
        auto& timestamps = iterator->second;
        timestamps.erase(
            std::remove_if(timestamps.begin(), timestamps.end(),
                           [timeNowCopy](const std::chrono::steady_clock::time_point& timestamp)
                           {
                               return std::chrono::duration_cast<std::chrono::seconds>(timeNowCopy - timestamp) > kRegistrationWindow;
                           }),
            timestamps.end());
        if (timestamps.empty())
        {
            iterator = m_registrationTimestamps.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void LoginRateLimiter::dropExpiredLockoutsLocked(AttemptRecord& record)
{
    // A fully elapsed failure window also voids any stale lockout timestamp.
    auto timeNow = std::chrono::steady_clock::now();
    bool windowExpired = std::chrono::duration_cast<std::chrono::seconds>(timeNow - record.windowStart) > kFailureWindow;
    if (windowExpired)
    {
        record.lockedUntil = std::min(record.lockedUntil, timeNow);
    }
}
