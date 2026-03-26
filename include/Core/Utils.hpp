#pragma once
#include <string>
#include <fstream>
#include <random>

namespace Utils {

inline std::string generateRandomUUID() {
    static std::random_device rd;
    static std::mt19937_64 e2(rd());
    static std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

    uint64_t part1 = dist(e2);
    uint64_t part2 = dist(e2);

    char buf[37];
    snprintf(buf, sizeof(buf), "%08lx-%04lx-%04lx-%04lx-%012lx",
             (part1 >> 32),
             (part1 >> 16) & 0xFFFF,
             part1 & 0xFFFF,
             (part2 >> 48) & 0xFFFF,
             part2 & 0xFFFFFFFFFFFF);
    return std::string(buf);
}

inline std::string getHardwareUUID() {
    std::string uuid;
    std::ifstream machineId("/etc/machine-id");
    if (machineId.is_open()) {
        std::getline(machineId, uuid);
        if (!uuid.empty()) {
            return uuid;
        }
    }

    std::ifstream cachedUuidFile(".voicechat_uuid");
    if (cachedUuidFile.is_open()) {
        std::getline(cachedUuidFile, uuid);
        if (!uuid.empty()) {
            return uuid;
        }
    }

    uuid = generateRandomUUID();
    std::ofstream newCachedUuidFile(".voicechat_uuid");
    if (newCachedUuidFile.is_open()) {
        newCachedUuidFile << uuid << std::endl;
    }

    return uuid;
}

inline void saveUsername(const std::string& username) {
    std::ofstream file(".voicechat_user");
    if (file.is_open()) {
        file << username << std::endl;
    }
}

inline std::string getSavedUsername() {
    std::string username;
    std::ifstream file(".voicechat_user");
    if (file.is_open()) {
        std::getline(file, username);
    }
    return username;
}

} // namespace Utils
