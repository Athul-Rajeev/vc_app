#include "Utils/LoggerSetup.hpp"
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void LoggerSetup::initializeAsyncLogger()
{
    // 1. Initialize the thread pool
    spdlog::init_thread_pool(8192, 1);

    // 2. Create only the console sink
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    // 3. Create the async logger using just the console sink
    auto asyncLogger = std::make_shared<spdlog::async_logger>(
        "NetworkLogger", 
        consoleSink, 
        spdlog::thread_pool(), 
        spdlog::async_overflow_policy::overrun_oldest
    );

    // 4. Register it as the global default logger
    spdlog::set_default_logger(asyncLogger);

    // 5. Set the global log level and a custom format pattern
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
}

void LoggerSetup::shutdownLogger()
{
    // Flushes the remaining queue and cleanly stops the background thread
    spdlog::shutdown();
}