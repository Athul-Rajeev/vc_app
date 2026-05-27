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
    std::string timestamp;
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
    std::vector<ChatMessage> fetchLastMessages(int channelId, int limit = 50);

    std::vector<Channel> fetchTextChannels();
    std::vector<Channel> fetchVoiceChannels();
    int addTextChannel(const std::string& name);
    int addVoiceChannel(const std::string& name);

    bool createAccount(const std::string& accountUuid, const std::string& username, const std::string& passwordHash, const std::string& passwordSalt);
    bool getAccountByUsername(const std::string& username, AccountInfo& outAccount);
    bool createSession(const std::string& sessionId, const std::string& accountUuid, const std::string& refreshTokenHash);
    bool getSessionAndAccount(const std::string& refreshTokenHash, AccountInfo& outAccount);
    void revokeSession(const std::string& refreshTokenHash);

private:
    void workerThreadLoop();

    sqlite3* m_db;
    
    std::thread m_workerThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::queue<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_isWorkerRunning;
};