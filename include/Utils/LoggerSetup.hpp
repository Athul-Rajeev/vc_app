#pragma once
#include <spdlog/spdlog.h>

class LoggerSetup
{
public:
    static void initializeAsyncLogger();
    static void shutdownLogger();
};