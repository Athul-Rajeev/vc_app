#include "Core/Application.hpp"
#include <spdlog/spdlog.h>
#include "Utils/LoggerSetup.hpp"

int main(int argc, char* argv[])
{
    bool isServerMode = false;
    std::string targetIp = "127.0.0.1";
    LoggerSetup::initializeAsyncLogger();
    spdlog::set_level(spdlog::level::debug);

    if (argc >= 2)
    {
        std::string modeArg = argv[1];
        if (modeArg == "--server")
        {
            isServerMode = true;
        }
        else if (modeArg == "--client")
        {
            if (argc >= 3)
            {
                targetIp = argv[2];
            }
            else
            {
                spdlog::error("Usage for client: VoiceChatApp --client <ServerIp>");
                return 1;
            }
        }
        else
        {
            targetIp = modeArg;
        }
    }

    Application mainApp;

    if (!mainApp.initialize(isServerMode))
    {
        spdlog::error("Failed to initialize application.");
        return 1;
    }

    mainApp.runMainLoop(targetIp);

    return 0;
}