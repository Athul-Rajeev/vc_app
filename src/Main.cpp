#include "Core/Application.hpp"

int main(int argc, char* argv[])
{
    bool isServerMode = false;
    std::string targetIp = "127.0.0.1";

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
                std::cerr << "Usage for client: VoiceChatApp --client <ServerIp>\n";
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
        std::cerr << "Failed to initialize application." << std::endl;
        return 1;
    }

    mainApp.runMainLoop(targetIp);

    return 0;
}