#pragma once
#include <string>
#include <fstream>
#include <random>

namespace Utils
{

inline std::string generateRandomUUID()
{
    static std::random_device randomDevice;
    static std::mt19937_64 randomEngine(randomDevice());
    static std::uniform_int_distribution<uint64_t> distribution(0, 0xFFFFFFFFFFFFFFFF);

    uint64_t firstHalf = distribution(randomEngine);
    uint64_t secondHalf = distribution(randomEngine);

    char uuidBuffer[37];
    snprintf(uuidBuffer, sizeof(uuidBuffer), "%08lx-%04lx-%04lx-%04lx-%012lx",
             (firstHalf >> 32),
             (firstHalf >> 16) & 0xFFFF,
             firstHalf & 0xFFFF,
             (secondHalf >> 48) & 0xFFFF,
             secondHalf & 0xFFFFFFFFFFFF);
    return std::string(uuidBuffer);
}

inline std::string getHardwareUUID()
{
    std::string uuid;
    std::ifstream machineIdFile("/etc/machine-id");
    if (machineIdFile.is_open())
    {
        std::getline(machineIdFile, uuid);
        if (!uuid.empty())
        {
            return uuid;
        }
    }

    std::ifstream cachedUuidFile(".voicechat_uuid");
    if (cachedUuidFile.is_open())
    {
        std::getline(cachedUuidFile, uuid);
        if (!uuid.empty())
        {
            return uuid;
        }
    }

    uuid = generateRandomUUID();
    std::ofstream newCachedUuidFile(".voicechat_uuid");
    if (newCachedUuidFile.is_open())
    {
        newCachedUuidFile << uuid << std::endl;
    }

    return uuid;
}

inline void saveUsername(const std::string& username)
{
    std::ofstream outputFile(".voicechat_user");
    if (outputFile.is_open())
    {
        outputFile << username << std::endl;
    }
}

inline std::string getSavedUsername()
{
    std::string username;
    std::ifstream inputFile(".voicechat_user");
    if (inputFile.is_open())
    {
        std::getline(inputFile, username);
    }
    return username;
}

} // namespace Utils
