#include "Database/DatabaseManager.hpp"
#include <iostream>
#include <sqlite3.h>

DatabaseManager::DatabaseManager() : m_db(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (m_db) {
        sqlite3_close(m_db);
    }
}

bool DatabaseManager::initialize(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    const char* sqlCreate = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "channel_id INTEGER, "
        "uuid TEXT, "
        "username TEXT, "
        "message TEXT, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sqlCreate, 0, 0, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

void DatabaseManager::storeMessage(int channelId, const std::string& uuid, const std::string& username, const std::string& message) {
    if (!m_db) return;

    sqlite3_stmt* stmt;
    const char* sqlInsert = "INSERT INTO messages (channel_id, uuid, username, message) VALUES (?, ?, ?, ?)";
    
    if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, channelId);
        sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Execution failed: " << sqlite3_errmsg(m_db) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(m_db) << std::endl;
    }
}

std::vector<ChatMessage> DatabaseManager::fetchLastMessages(int channelId, int limit) {
    std::vector<ChatMessage> history;
    if (!m_db) return history;

    sqlite3_stmt* stmt;
    const char* sqlSelect = "SELECT id, channel_id, uuid, username, message, timestamp "
                            "FROM (SELECT * FROM messages WHERE channel_id = ? ORDER BY timestamp DESC LIMIT ?) "
                            "ORDER BY timestamp ASC";

    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, channelId);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ChatMessage msg;
            msg.id = sqlite3_column_int(stmt, 0);
            msg.channelId = sqlite3_column_int(stmt, 1);
            
            const unsigned char* u = sqlite3_column_text(stmt, 2);
            msg.uuid = u ? reinterpret_cast<const char*>(u) : "";
            
            const unsigned char* nm = sqlite3_column_text(stmt, 3);
            msg.username = nm ? reinterpret_cast<const char*>(nm) : "";
            
            const unsigned char* m = sqlite3_column_text(stmt, 4);
            msg.message = m ? reinterpret_cast<const char*>(m) : "";
            
            const unsigned char* t = sqlite3_column_text(stmt, 5);
            msg.timestamp = t ? reinterpret_cast<const char*>(t) : "";
            
            history.push_back(msg);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to fetch messages: " << sqlite3_errmsg(m_db) << std::endl;
    }

    return history;
}
