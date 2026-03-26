#pragma once
#include <string>
#include <vector>

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

private:
    sqlite3* m_db;
};
