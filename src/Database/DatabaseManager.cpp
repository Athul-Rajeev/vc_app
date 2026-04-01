#include "Database/DatabaseManager.hpp"


DatabaseManager::DatabaseManager() : m_db(nullptr), m_isWorkerRunning(false)
{
}

DatabaseManager::~DatabaseManager() 
{
    if (m_isWorkerRunning)
    {
        m_isWorkerRunning = false;
        m_queueCondition.notify_all();
        if (m_workerThread.joinable())
        {
            m_workerThread.join();
        }
    }

    if (m_db) 
    {
        sqlite3_close(m_db);
    }
}

bool DatabaseManager::initialize(const std::string& dbPath) 
{
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) 
    {
        std::cerr << "Can't open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);

    const char* sqlCreateMessages = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "channel_id INTEGER, "
        "uuid TEXT, "
        "username TEXT, "
        "message TEXT, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    const char* sqlCreateTextChannels = 
        "CREATE TABLE IF NOT EXISTS text_channels ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE);";

    const char* sqlCreateVoiceChannels = 
        "CREATE TABLE IF NOT EXISTS voice_channels ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE);";

    char* errorMessage = nullptr;
    if (sqlite3_exec(m_db, sqlCreateMessages, 0, 0, &errorMessage) != SQLITE_OK) 
    {
        std::cerr << "SQL error (messages): " << errorMessage << std::endl;
        sqlite3_free(errorMessage);
        return false;
    }
    
    if (sqlite3_exec(m_db, sqlCreateTextChannels, 0, 0, &errorMessage) != SQLITE_OK) 
    {
        std::cerr << "SQL error (text_channels): " << errorMessage << std::endl;
        sqlite3_free(errorMessage);
        return false;
    }
    
    if (sqlite3_exec(m_db, sqlCreateVoiceChannels, 0, 0, &errorMessage) != SQLITE_OK) 
    {
        std::cerr << "SQL error (voice_channels): " << errorMessage << std::endl;
        sqlite3_free(errorMessage);
        return false;
    }
    
    // START THE WORKER THREAD HERE
    // This must happen before addTextChannel or addVoiceChannel are called
    m_isWorkerRunning = true;
    m_workerThread = std::thread(&DatabaseManager::workerThreadLoop, this);
    
    auto existingTextChannels = fetchTextChannels();
    if (existingTextChannels.empty()) 
    {
        addTextChannel("general");
        addTextChannel("development");
    }
    
    auto existingVoiceChannels = fetchVoiceChannels();
    if (existingVoiceChannels.empty()) 
    {
        addVoiceChannel("Voice General");
    }
    
    return true;
}

void DatabaseManager::workerThreadLoop()
{
    while (m_isWorkerRunning)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this]
            {
                return !m_taskQueue.empty() || !m_isWorkerRunning;
            });

            if (!m_isWorkerRunning && m_taskQueue.empty())
            {
                return;
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        if (task)
        {
            task();
        }
    }
}

void DatabaseManager::storeMessage(int channelId, const std::string& uuid, const std::string& username, const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, channelId, uuid, username, message]()
        {
            if (!m_db)
            {
                return;
            }

            sqlite3_stmt* stmt;
            const char* sqlInsert = "INSERT INTO messages (channel_id, uuid, username, message) VALUES (?, ?, ?, ?)";
            
            if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &stmt, 0) == SQLITE_OK) 
            {
                sqlite3_bind_int(stmt, 1, channelId);
                sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
                
                if (sqlite3_step(stmt) != SQLITE_DONE) 
                {
                    std::cerr << "Execution failed: " << sqlite3_errmsg(m_db) << std::endl;
                }
                sqlite3_finalize(stmt);
            } 
            else 
            {
                std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(m_db) << std::endl;
            }
        });
    }
    m_queueCondition.notify_one();
}

std::vector<ChatMessage> DatabaseManager::fetchLastMessages(int channelId, int limit) 
{
    std::vector<ChatMessage> history;
    if (!m_db)
    {
        return history;
    }

    sqlite3_stmt* stmt;
    const char* sqlSelect = "SELECT id, channel_id, uuid, username, message, timestamp "
                            "FROM (SELECT * FROM messages WHERE channel_id = ? ORDER BY timestamp DESC LIMIT ?) "
                            "ORDER BY timestamp ASC";

    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, 0) == SQLITE_OK) 
    {
        sqlite3_bind_int(stmt, 1, channelId);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) 
        {
            ChatMessage chatMessage;
            chatMessage.id = sqlite3_column_int(stmt, 0);
            chatMessage.channelId = sqlite3_column_int(stmt, 1);
            
            const unsigned char* rawUuid = sqlite3_column_text(stmt, 2);
            chatMessage.uuid = rawUuid ? reinterpret_cast<const char*>(rawUuid) : "";
            
            const unsigned char* rawUsername = sqlite3_column_text(stmt, 3);
            chatMessage.username = rawUsername ? reinterpret_cast<const char*>(rawUsername) : "";
            
            const unsigned char* rawMessage = sqlite3_column_text(stmt, 4);
            chatMessage.message = rawMessage ? reinterpret_cast<const char*>(rawMessage) : "";
            
            const unsigned char* rawTimestamp = sqlite3_column_text(stmt, 5);
            chatMessage.timestamp = rawTimestamp ? reinterpret_cast<const char*>(rawTimestamp) : "";
            
            history.push_back(chatMessage);
        }
        sqlite3_finalize(stmt);
    } 
    else 
    {
        std::cerr << "Failed to fetch messages: " << sqlite3_errmsg(m_db) << std::endl;
    }

    return history;
}

std::vector<Channel> DatabaseManager::fetchTextChannels() 
{
    std::vector<Channel> channels;
    if (!m_db)
    {
        return channels;
    }
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, "SELECT id, name FROM text_channels ORDER BY id ASC", -1, &stmt, 0) == SQLITE_OK) 
    {
        while (sqlite3_step(stmt) == SQLITE_ROW) 
        {
            Channel channel;
            channel.id = sqlite3_column_int(stmt, 0);
            const unsigned char* rawName = sqlite3_column_text(stmt, 1);
            channel.name = rawName ? reinterpret_cast<const char*>(rawName) : "";
            channels.push_back(channel);
        }
        sqlite3_finalize(stmt);
    }
    return channels;
}

std::vector<Channel> DatabaseManager::fetchVoiceChannels() 
{
    std::vector<Channel> channels;
    if (!m_db)
    {
        return channels;
    }
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, "SELECT id, name FROM voice_channels ORDER BY id ASC", -1, &stmt, 0) == SQLITE_OK) 
    {
        while (sqlite3_step(stmt) == SQLITE_ROW) 
        {
            Channel channel;
            channel.id = sqlite3_column_int(stmt, 0);
            const unsigned char* rawName = sqlite3_column_text(stmt, 1);
            channel.name = rawName ? reinterpret_cast<const char*>(rawName) : "";
            channels.push_back(channel);
        }
        sqlite3_finalize(stmt);
    }
    return channels;
}

int DatabaseManager::addTextChannel(const std::string& name)
{
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, name, promise]()
        {
            if (!m_db)
            {
                promise->set_value(-1);
                return;
            }
            
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(m_db, "INSERT INTO text_channels (name) VALUES (?)", -1, &stmt, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    std::cerr << "Failed to add text channel: " << sqlite3_errmsg(m_db) << std::endl;
                    promise->set_value(-1);
                }
                else
                {
                    promise->set_value(sqlite3_last_insert_rowid(m_db));
                }
                sqlite3_finalize(stmt);
            }
            else
            {
                promise->set_value(-1);
            }
        });
    }
    m_queueCondition.notify_one();
    return future.get();
}

int DatabaseManager::addVoiceChannel(const std::string& name)
{
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, name, promise]()
        {
            if (!m_db)
            {
                promise->set_value(-1);
                return;
            }
            
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(m_db, "INSERT INTO voice_channels (name) VALUES (?)", -1, &stmt, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    std::cerr << "Failed to add voice channel: " << sqlite3_errmsg(m_db) << std::endl;
                    promise->set_value(-1);
                }
                else
                {
                    promise->set_value(sqlite3_last_insert_rowid(m_db));
                }
                sqlite3_finalize(stmt);
            }
            else
            {
                promise->set_value(-1);
            }
        });
    }
    m_queueCondition.notify_one();
    return future.get();
}