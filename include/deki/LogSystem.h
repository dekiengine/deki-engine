#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// DLL export/import for LogSystem (editor builds only)
// This ensures plugins can call log functions from the engine DLL
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define DEKI_LOG_API __declspec(dllexport)
#else
#define DEKI_LOG_API __declspec(dllimport)
#endif
#else
#define DEKI_LOG_API
#endif
#else
#define DEKI_LOG_API
#endif

// Log levels for the Deki logging system

namespace Deki
{

enum class LogLevel
{
    Editor,    // Internal editor logging (hidden by default, not printed to stdout)
    Internal,  // Verbose engine internals (off by default, opt-in via DEKI_LOG_INTERNAL_ENABLED)
    Debug,     // Debug diagnostics (prints to stdout on devices when DEKI_LOG_ENABLED)
    Info,
    Warning,
    Error
};

// Centralized logging macros for GameEngine
// Usage: GAME_ENGINE_LOG("message: %d", value);

#ifdef DEKI_LOG_ENABLED
// Unified logging system with specific level macros
#define DEKI_LOG(level, fmt, ...) ::Deki::LogSystem::LogWithLevel(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Convenience macros for specific log levels
#define DEKI_LOG_EDITOR(fmt, ...) DEKI_LOG(::Deki::LogLevel::Editor, fmt, ##__VA_ARGS__)
#define DEKI_LOG_DEBUG(fmt, ...) DEKI_LOG(::Deki::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define DEKI_LOG_INFO(fmt, ...) DEKI_LOG(::Deki::LogLevel::Info, fmt, ##__VA_ARGS__)
#define DEKI_LOG_WARNING(fmt, ...) DEKI_LOG(::Deki::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define DEKI_LOG_ERROR(fmt, ...) DEKI_LOG(::Deki::LogLevel::Error, fmt, ##__VA_ARGS__)
#else
// All logging disabled in release
#define DEKI_LOG(level, fmt, ...)
#define DEKI_LOG_EDITOR(fmt, ...)
#define DEKI_LOG_DEBUG(fmt, ...)
#define DEKI_LOG_INFO(fmt, ...)
#define DEKI_LOG_WARNING(fmt, ...)
#define DEKI_LOG_ERROR(fmt, ...)
#endif

// Internal engine logging — off by default, opt-in via DEKI_LOG_INTERNAL_ENABLED
#if defined(DEKI_LOG_ENABLED) && defined(DEKI_LOG_INTERNAL_ENABLED)
#define DEKI_LOG_INTERNAL(fmt, ...) DEKI_LOG(::Deki::LogLevel::Internal, fmt, ##__VA_ARGS__)
#else
#define DEKI_LOG_INTERNAL(fmt, ...)
#endif

// Log callback type for editor integration
using LogCallback = std::function<void(LogLevel level, const std::string& message,
                                       const char* file, int line)>;

class DEKI_LOG_API LogSystem
{
public:
    /**
     * @brief Initialize the Deki logging system
     * Sets up stdout buffering and other logging configuration
     */
    static void Initialize();

    static void LogWithLevel(LogLevel level, const char* file, int line, const char* fmt, ...);

    /**
     * @brief Set a callback to receive log messages (for editor console integration)
     */
    static void SetLogCallback(LogCallback callback);

    // Log context: tracks which Object is currently executing (for console click-to-select)
    static void SetLogContext(uint32_t objectId);
    static void ClearLogContext();
    static uint32_t GetLogContextObjectId();

private:
    static LogCallback s_LogCallback;
};

}  // namespace Deki
