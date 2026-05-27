#pragma once
#include <string>
#include <fstream>
#include <random>
#include <cinttypes>
#include <functional>

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
    snprintf(uuidBuffer, sizeof(uuidBuffer), 
         "%08" PRIx64 "-%04" PRIx64 "-%04" PRIx64 "-%04" PRIx64 "-%012" PRIx64, 
         (firstHalf >> 32), 
         (firstHalf >> 16) & 0xFFFF, 
         firstHalf & 0xFFFF, 
         (secondHalf >> 48) & 0xFFFF, 
         secondHalf & 0xFFFFFFFFFFFF);

    return std::string(uuidBuffer);
}

inline std::string generateSalt()
{
    return generateRandomUUID();
}

inline std::string hashString(const std::string& input)
{
    std::hash<std::string> hasher;
    return std::to_string(hasher(input));
}

inline std::string hashPassword(const std::string& password, const std::string& salt)
{
    return hashString(password + salt);
}

inline void saveSessionToken(const std::string& token)
{
    std::ofstream outputFile(".voicechat_session");
    if (outputFile.is_open())
    {
        outputFile << token << std::endl;
    }
}

inline std::string getSavedSessionToken()
{
    std::string token;
    std::ifstream inputFile(".voicechat_session");
    if (inputFile.is_open())
    {
        std::getline(inputFile, token);
    }
    return token;
}

inline void clearSessionToken()
{
    std::remove(".voicechat_session");
}

} // namespace Utils