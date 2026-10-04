#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// DLL export/import for LogSystem in editor builds, so plugins can call the
// engine DLL's log functions.
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

namespace Deki
{

// Which level a line belongs to:
//   Info    - what the user did or needs to know (export finished, board memory).
//   Editor  - one line per editor milestone or state change, for Editor.log.
//             Not per item, and not when a check found nothing to do.
//   Debug   - per-item tracing (each asset, each registration). Editor.log
//             keeps it only with the editorLog.verbose preference on.
enum class LogLevel
{
    Editor,    // editor logging; hidden by default, not printed to stdout
    Internal,  // verbose engine internals; off unless DEKI_LOG_INTERNAL_ENABLED
    Debug,     // diagnostics; printed to stdout on devices when DEKI_LOG_ENABLED
    Info,
    Warning,
    Error
};

// Logging macros. Usage: DEKI_LOG_INFO("message: %d", value);

#ifdef DEKI_LOG_ENABLED
#define DEKI_LOG(level, fmt, ...) ::Deki::LogSystem::LogWithLevel(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEKI_LOG_EDITOR(fmt, ...) DEKI_LOG(::Deki::LogLevel::Editor, fmt, ##__VA_ARGS__)
#define DEKI_LOG_DEBUG(fmt, ...) DEKI_LOG(::Deki::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define DEKI_LOG_INFO(fmt, ...) DEKI_LOG(::Deki::LogLevel::Info, fmt, ##__VA_ARGS__)
#define DEKI_LOG_WARNING(fmt, ...) DEKI_LOG(::Deki::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define DEKI_LOG_ERROR(fmt, ...) DEKI_LOG(::Deki::LogLevel::Error, fmt, ##__VA_ARGS__)
#else
// Logging off. Each macro must stay ((void)0), not empty: empty turns
// `if (x) DEKI_LOG_INFO(...);` into `if (x) ;`, an empty-body warning that
// ESP-IDF's -Werror makes an error. The arguments are not evaluated.
#define DEKI_LOG(level, fmt, ...) ((void)0)
#define DEKI_LOG_EDITOR(fmt, ...) ((void)0)
#define DEKI_LOG_DEBUG(fmt, ...) ((void)0)
#define DEKI_LOG_INFO(fmt, ...) ((void)0)
#define DEKI_LOG_WARNING(fmt, ...) ((void)0)
#define DEKI_LOG_ERROR(fmt, ...) ((void)0)
#endif

// Engine internals, off unless DEKI_LOG_INTERNAL_ENABLED is defined.
#if defined(DEKI_LOG_ENABLED) && defined(DEKI_LOG_INTERNAL_ENABLED)
#define DEKI_LOG_INTERNAL(fmt, ...) DEKI_LOG(::Deki::LogLevel::Internal, fmt, ##__VA_ARGS__)
#else
#define DEKI_LOG_INTERNAL(fmt, ...) ((void)0)  // a statement, not nothing: see above
#endif

// Receives log lines, for the editor console.
using LogCallback = std::function<void(LogLevel level, const std::string& message, const char* file, int line)>;

class DEKI_LOG_API LogSystem
{
public:
    /// Sets up logging, including stdout buffering.
    static void Initialize();

    static void LogWithLevel(LogLevel level, const char* file, int line, const char* fmt, ...);

    /// Sends every log line to `callback` as well (the editor console).
    static void SetLogCallback(LogCallback callback);

    // The Object whose code is running, so a click on a console line can
    // select it.
    static void SetLogContext(uint32_t objectId);
    static void ClearLogContext();
    static uint32_t GetLogContextObjectId();

private:
    static LogCallback s_LogCallback;
};

}  // namespace Deki
