#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <stdexcept>

#pragma pack(push, 1)
struct WavHeader
{
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t overallSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmtChunkMarker[4] = {'f', 'm', 't', ' '};
    uint32_t lengthOfFmt = 16;
    uint16_t formatType = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 48000 * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    char dataChunkHeader[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};
#pragma pack(pop)

class WavUtils
{
public:
    static std::vector<int16_t> readWav(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Could not open input WAV");
        }
        
        WavHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));
        
        std::vector<int16_t> pcmData(header.dataSize / sizeof(int16_t));
        file.read(reinterpret_cast<char*>(pcmData.data()), header.dataSize);
        return pcmData;
    }

    static void writeWav(const std::string& filePath, const std::vector<int16_t>& pcmData, uint32_t sampleRate = 48000)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Could not open output WAV");
        }

        WavHeader header;
        header.sampleRate = sampleRate;
        header.channels = 1;
        header.bitsPerSample = 16;
        header.blockAlign = header.channels * (header.bitsPerSample / 8);
        header.byteRate = header.sampleRate * header.blockAlign;
        header.dataSize = static_cast<uint32_t>(pcmData.size() * sizeof(int16_t));
        header.overallSize = header.dataSize + sizeof(WavHeader) - 8;

        file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
        file.write(reinterpret_cast<const char*>(pcmData.data()), header.dataSize);
    }
};