#include "Core/Application.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: VoiceChatApp <TargetTailscaleIP>" << std::endl;
        return 1;
    }

    std::string targetIp = argv[1];
    Application mainApp;

    if (!mainApp.initialize())
    {
        std::cerr << "Failed to initialize application." << std::endl;
        return 1;
    }

    mainApp.runMainLoop(targetIp);

    return 0;
}