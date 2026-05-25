#include "Core/Application.hpp"
#include <spdlog/spdlog.h>
#include "Utils/LoggerSetup.hpp"
#include <vector>
#include <string>

// Include Windows headers for native message boxes when the console is hidden
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    LoggerSetup::initializeAsyncLogger();
    spdlog::set_level(spdlog::level::info);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }

    for (size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--test-audio")
        {
            if (i + 2 >= args.size())
            {
                spdlog::error("Usage: VoiceChatApp --test-audio <input.wav> <output.wav>");
#ifdef _WIN32
                MessageBoxA(nullptr, "Usage: VoiceChatApp --test-audio <input.wav> <output.wav>", "Argument Error", MB_ICONERROR | MB_OK);
#endif
                return 1;
            }

            std::string inputWav = args[i + 1];
            std::string outputWav = args[i + 2];

            Application testApp;
            testApp.runAudioQualityTest(inputWav, outputWav);
            
            return 0;
        }
    }

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
                spdlog::error("Usage for client: VoiceChatApp --client <ServerIp>");
#ifdef _WIN32
                MessageBoxA(nullptr, "Usage for client: VoiceChatApp --client <ServerIp>", "Argument Error", MB_ICONERROR | MB_OK);
#endif
                return 1;
            }
        }
        else
        {
            targetIp = modeArg;
        }
    }

    Application mainApp;

    // If QML fails to load, Application::initialize will return false here
    if (!mainApp.initialize(isServerMode))
    {
        spdlog::error("Fatal Error: Failed to initialize application (likely a QML or GUI issue).");
        
#ifdef _WIN32
        // Force a visible popup so the app NEVER fails silently again
        MessageBoxA(nullptr, 
            "Failed to initialize the application.\nThis is usually caused by a QML syntax error or missing Qt module.\nCheck your log files for more details.", 
            "Fatal Initialization Error", 
            MB_ICONERROR | MB_OK);
#endif
        return 1;
    }

    mainApp.runMainLoop(targetIp);

    return 0;
}