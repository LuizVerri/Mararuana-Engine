// Log.cpp
// Initializes the two spdlog loggers used throughout the engine:
//   - Core logger ("MARARUANA"): internal engine subsystems.
//   - Client logger ("SANDBOX"): application/game layer code.
// Both loggers write to stdout with color output and trace-level verbosity.
// Call Log::Init() once at program startup before any logging macros are used.

#include "Mararuana/Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Mar
{
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

    void Log::Init()
    {
        // [HH:MM:SS] LOGGER_NAME: message
        spdlog::set_pattern("%^[%T] %n: %v%$");

        s_CoreLogger = spdlog::stdout_color_mt("MARARUANA");
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = spdlog::stdout_color_mt("SANDBOX");
        s_ClientLogger->set_level(spdlog::level::trace);

        s_CoreLogger->info("Core Logger initialized successfully!");
        s_ClientLogger->info("Client Logger initialized successfully!");
    }
}