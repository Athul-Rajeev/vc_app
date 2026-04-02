#include "Database/DatabaseManager.hpp"
#include <spdlog/spdlog.h>

DatabaseManager::DatabaseManager() : m_db(nullptr), m_isWorkerRunning(false)
{
    spdlog::trace("DatabaseManager instantiated");
}

DatabaseManager::~DatabaseManager() 
{
    spdlog::trace("DatabaseManager shutting down");
    if (m_isWorkerRunning)
    {
        m_isWorkerRunning = false;
        m_queueCondition.notify_all();
        if (m_workerThread.joinable())
        {
            spdlog::trace("Joining database worker thread");
            m_workerThread.join();
        }
    }

    if (m_db) 
    {
        spdlog::trace("Closing SQLite database connection");
        sqlite3_close(m_db);
    }
}

bool DatabaseManager::initialize(const std::string& dbPath) 
{
    spdlog::debug("Initializing DatabaseManager with path: {}", dbPath);

    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) 
    {
        spdlog::critical("Can't open database: {}", sqlite3_errmsg(m_db));
        return false;
    }

    spdlog::trace("Setting SQLite PRAGMAs (WAL mode, NORMAL synchronous)");
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
        spdlog::critical("SQL error (messages): {}", errorMessage);
        sqlite3_free(errorMessage);
        return false;
    }
    
    if (sqlite3_exec(m_db, sqlCreateTextChannels, 0, 0, &errorMessage) != SQLITE_OK) 
    {
        spdlog::critical("SQL error (text_channels): {}", errorMessage);
        sqlite3_free(errorMessage);
        return false;
    }
    
    if (sqlite3_exec(m_db, sqlCreateVoiceChannels, 0, 0, &errorMessage) != SQLITE_OK) 
    {
        spdlog::critical("SQL error (voice_channels): {}", errorMessage);
        sqlite3_free(errorMessage);
        return false;
    }
    
    // START THE WORKER THREAD HERE
    // This must happen before addTextChannel or addVoiceChannel are called
    spdlog::trace("Starting DatabaseManager worker thread");
    m_isWorkerRunning = true;
    m_workerThread = std::thread(&DatabaseManager::workerThreadLoop, this);
    
    auto existingTextChannels = fetchTextChannels();
    if (existingTextChannels.empty()) 
    {
        spdlog::info("No text channels found, creating default ones");
        addTextChannel("general");
        addTextChannel("development");
    }
    
    auto existingVoiceChannels = fetchVoiceChannels();
    if (existingVoiceChannels.empty()) 
    {
        spdlog::info("No voice channels found, creating default ones");
        addVoiceChannel("Voice General");
    }
    
    spdlog::info("DatabaseManager initialized successfully");
    return true;
}

void DatabaseManager::workerThreadLoop()
{
    spdlog::trace("Worker thread loop entered");
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
                spdlog::trace("Worker thread loop exiting cleanly");
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
    spdlog::trace("Queueing storeMessage task for channel {} from user {}", channelId, username);
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, channelId, uuid, username, message]()
        {
            if (!m_db)
            {
                spdlog::error("Attempted to store message, but database is not initialized");
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
                    spdlog::error("Message execution failed: {}", sqlite3_errmsg(m_db));
                }
                else
                {
                    spdlog::trace("Message successfully inserted into database");
                }
                sqlite3_finalize(stmt);
            } 
            else 
            {
                spdlog::error("Failed to prepare message insert statement: {}", sqlite3_errmsg(m_db));
            }
        });
    }
    m_queueCondition.notify_one();
}

std::vector<ChatMessage> DatabaseManager::fetchLastMessages(int channelId, int limit) 
{
    spdlog::trace("Fetching up to {} last messages for channel {}", limit, channelId);
    std::vector<ChatMessage> history;
    
    if (!m_db)
    {
        spdlog::error("Attempted to fetch messages, but database is not initialized");
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
        spdlog::debug("Fetched {} messages for channel {}", history.size(), channelId);
    } 
    else 
    {
        spdlog::error("Failed to fetch messages: {}", sqlite3_errmsg(m_db));
    }

    return history;
}

std::vector<Channel> DatabaseManager::fetchTextChannels() 
{
    spdlog::trace("Fetching text channels from database");
    std::vector<Channel> channels;
    
    if (!m_db)
    {
        spdlog::error("Attempted to fetch text channels, but database is not initialized");
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
        spdlog::debug("Fetched {} text channels", channels.size());
    }
    else
    {
        spdlog::error("Failed to prepare fetch text channels statement: {}", sqlite3_errmsg(m_db));
    }
    
    return channels;
}

std::vector<Channel> DatabaseManager::fetchVoiceChannels() 
{
    spdlog::trace("Fetching voice channels from database");
    std::vector<Channel> channels;
    
    if (!m_db)
    {
        spdlog::error("Attempted to fetch voice channels, but database is not initialized");
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
        spdlog::debug("Fetched {} voice channels", channels.size());
    }
    else
    {
        spdlog::error("Failed to prepare fetch voice channels statement: {}", sqlite3_errmsg(m_db));
    }
    
    return channels;
}

int DatabaseManager::addTextChannel(const std::string& name)
{
    spdlog::debug("Queueing task to add new text channel: '{}'", name);
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, name, promise]()
        {
            if (!m_db)
            {
                spdlog::error("Attempted to add text channel '{}', but database is not initialized", name);
                promise->set_value(-1);
                return;
            }
            
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(m_db, "INSERT INTO text_channels (name) VALUES (?)", -1, &stmt, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    spdlog::error("Failed to add text channel '{}': {}", name, sqlite3_errmsg(m_db));
                    promise->set_value(-1);
                }
                else
                {
                    int insertId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
                    spdlog::info("Successfully added text channel '{}' with ID {}", name, insertId);
                    promise->set_value(insertId);
                }
                sqlite3_finalize(stmt);
            }
            else
            {
                spdlog::error("Failed to prepare add text channel statement: {}", sqlite3_errmsg(m_db));
                promise->set_value(-1);
            }
        });
    }
    m_queueCondition.notify_one();
    return future.get();
}

int DatabaseManager::addVoiceChannel(const std::string& name)
{
    spdlog::debug("Queueing task to add new voice channel: '{}'", name);
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push([this, name, promise]()
        {
            if (!m_db)
            {
                spdlog::error("Attempted to add voice channel '{}', but database is not initialized", name);
                promise->set_value(-1);
                return;
            }
            
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(m_db, "INSERT INTO voice_channels (name) VALUES (?)", -1, &stmt, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    spdlog::error("Failed to add voice channel '{}': {}", name, sqlite3_errmsg(m_db));
                    promise->set_value(-1);
                }
                else
                {
                    int insertId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
                    spdlog::info("Successfully added voice channel '{}' with ID {}", name, insertId);
                    promise->set_value(insertId);
                }
                sqlite3_finalize(stmt);
            }
            else
            {
                spdlog::error("Failed to prepare add voice channel statement: {}", sqlite3_errmsg(m_db));
                promise->set_value(-1);
            }
        });
    }
    m_queueCondition.notify_one();
    return future.get();
}