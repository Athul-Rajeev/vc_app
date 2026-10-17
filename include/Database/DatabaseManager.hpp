#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <iostream>
#include <sqlite3.h>


struct sqlite3;

struct ChatMessage
{
    int id;
    int channelId;
    std::string uuid;
    std::string username;
    std::string message;
    long long timestamp;       // Unix epoch in milliseconds
};

struct Channel
{
    int id;
    std::string name;
};

struct AccountInfo
{
    std::string accountUuid;
    std::string username;
    std::string passwordHash;
    std::string passwordSalt;
};

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    bool initialize(const std::string& dbPath);
    void storeMessage(int channelId, const std::string& uuid, const std::string& username, const std::string& message);
    std::vector<ChatMessage> fetchLastMessages(int channelId, int limit, bool& outHasMore);
    std::vector<ChatMessage> fetchMessagesBefore(int channelId, long long beforeTimestampMs, int limit, bool& outHasMore);

    std::vector<Channel> fetchTextChannels();
    std::vector<Channel> fetchVoiceChannels();
    int addTextChannel(const std::string& name);
    int addVoiceChannel(const std::string& name);

    bool createAccount(const std::string& accountUuid, const std::string& username, const std::string& passwordHash, const std::string& passwordSalt);
    bool getAccountByUsername(const std::string& username, AccountInfo& outAccount);
    bool updateAccountPassword(const std::string& accountUuid, const std::string& passwordHash, const std::string& passwordSalt);
    bool createSession(const std::string& sessionId, const std::string& accountUuid, const std::string& refreshTokenHash);
    bool getSessionAndAccount(const std::string& refreshTokenHash, AccountInfo& outAccount);
    void revokeSession(const std::string& refreshTokenHash);
    void revokeAllSessions(const std::string& accountUuid);

    bool createPasswordReset(const std::string& accountUuid, const std::string& resetTokenHash, long long expiresAtEpochSeconds);
    bool consumePasswordReset(const std::string& resetTokenHash, AccountInfo& outAccount);

private:
    void workerThreadLoop();

    sqlite3* m_db;
    
    std::thread m_workerThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::queue<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_isWorkerRunning;
};