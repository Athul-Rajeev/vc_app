#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * LoginRateLimiter
 * ────────────────
 * Throttles authentication attempts (login, password change, reset) per
 * caller identity so brute-forcing a password is impractical. The identity
 * key is chosen by the caller — typically the raw source IP for network-level
 * throttling and "account:<username>" for targeted per-account protection.
 *
 * All state is in-memory and intentionally resets when the server restarts;
 * acceptable for this trusted Tailscale LAN tool.
 */
class LoginRateLimiter
{
public:
    LoginRateLimiter();
    ~LoginRateLimiter();

    /// Seconds until the key may authenticate again (0 = allowed right now).
    int secondsUntilAllowed(const std::string& identityKey);

    /// Records a rejected attempt and applies progressive lockout to keys
    /// that exceed the failure budget. Returns the lockout that now applies
    /// (in seconds; 0 while the key is still inside its budget).
    int recordFailure(const std::string& identityKey);

    /// Clears all throttling for the key after a successful authentication.
    void recordSuccess(const std::string& identityKey);

    /// True when the key is still under the new-account budget (per window).
    bool isAllowedToRegister(const std::string& identityKey);

    /// Records one newly created registration for that key.
    void recordRegistration(const std::string& identityKey);

private:
    struct AttemptRecord
    {
        int failureCount = 0;
        std::chrono::steady_clock::time_point windowStart{};
        std::chrono::steady_clock::time_point lockedUntil{};
    };

    void sweepStaleEntriesLocked();
    void dropExpiredLockoutsLocked(AttemptRecord& record);

    std::mutex m_recordMutex;
    std::unordered_map<std::string, AttemptRecord> m_attemptRecords;
    std::unordered_map<std::string, std::vector<std::chrono::steady_clock::time_point>> m_registrationTimestamps;
    std::chrono::steady_clock::time_point m_lastSweepTime{};
};
