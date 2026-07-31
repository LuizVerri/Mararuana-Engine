#pragma once

#include <memory>
#include <spdlog/spdlog.h>

namespace Mar {

    // Log
    // Thin wrapper around two spdlog loggers: one for engine-internal messages
    // (Core) and one for application-level messages (Client).
    //
    // Call Log::Init() once at startup before using any macro.
    // After that, prefer the macros below over calling the loggers directly.
    class Log {
    public:
        static void Init();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };

} // namespace Mar


// Core logger macros
// Use these inside engine code. Output is tagged with the core logger name.
#define MAR_CORE_FATAL(...)  ::Mar::Log::GetCoreLogger()->critical(__VA_ARGS__)
#define MAR_CORE_ERROR(...)  ::Mar::Log::GetCoreLogger()->error(__VA_ARGS__)
#define MAR_CORE_WARN(...)   ::Mar::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define MAR_CORE_INFO(...)   ::Mar::Log::GetCoreLogger()->info(__VA_ARGS__)
#define MAR_CORE_TRACE(...)  ::Mar::Log::GetCoreLogger()->trace(__VA_ARGS__)


// Client logger macros
// Use these inside application code. Output is tagged with the client logger name.
#define MAR_CLIENT_FATAL(...)  ::Mar::Log::GetClientLogger()->critical(__VA_ARGS__)
#define MAR_CLIENT_ERROR(...)  ::Mar::Log::GetClientLogger()->error(__VA_ARGS__)
#define MAR_CLIENT_WARN(...)   ::Mar::Log::GetClientLogger()->warn(__VA_ARGS__)
#define MAR_CLIENT_INFO(...)   ::Mar::Log::GetClientLogger()->info(__VA_ARGS__)
#define MAR_CLIENT_TRACE(...)  ::Mar::Log::GetClientLogger()->trace(__VA_ARGS__)