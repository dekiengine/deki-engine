#include <deki/LogSystem.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

// Global log counter for proper alternation across all log calls

namespace Deki
{

static int log_count = 0;

// Static callback member
LogCallback LogSystem::s_LogCallback = nullptr;

// Thread-local log context: tracks which Object is currently executing
static thread_local uint32_t s_LogContextObjectId = 0;

void LogSystem::SetLogContext(uint32_t objectId)
{
    s_LogContextObjectId = objectId;
}
void LogSystem::ClearLogContext()
{
    s_LogContextObjectId = 0;
}
uint32_t LogSystem::GetLogContextObjectId()
{
    return s_LogContextObjectId;
}

void LogSystem::SetLogCallback(LogCallback callback)
{
    s_LogCallback = callback;
}

void LogSystem::Initialize()
{
#ifdef DEKI_LOG_ENABLED
    // Line-buffered stdout: flushes at each newline for prompt output without
    // the overhead of unbuffered I/O (which is costly over serial on ESP32).
    // The size MUST be > 0 for _IOLBF/_IOFBF: MSVC's CRT validates it and raises
    // an invalid-parameter fast-fail (0xC0000409) on size 0. BUFSIZ is valid everywhere.
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
#endif
}

void LogSystem::LogWithLevel(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    // Format message first (needed for both console output and callback)
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Extract filename for display: __FILE__ is an absolute path, and this
    // used to copy it into a std::string, substr it, and concatenate the
    // callback message from parts. Three heap allocations per log line, on
    // paths that can log every frame.
    const char* display_name = file ? file : "";
    for (const char* p = display_name; *p; ++p)
        if (*p == '/' || *p == '\\')
            display_name = p + 1;

    // Call callback if registered (for editor console integration)
    if (s_LogCallback)
    {
        char full[640];
        snprintf(full, sizeof(full), "%s:%d -> %s", display_name, line, buffer);
        s_LogCallback(level, std::string(full), file, line);
    }

    // Editor level logs are only sent to callback, not printed to stdout
    if (level == LogLevel::Editor)
    {
        return;
    }

    const char* color = "";
    const char* level_text = "[INFO]";

    if (level == LogLevel::Error)
    {
        color = "\033[1;91m";  // Bright red
        level_text = "[ERROR]";
    }
    else if (level == LogLevel::Warning)
    {
        color = "\033[1;93m";  // Bright yellow
        level_text = "[WARN]";
    }
    else if (level == LogLevel::Internal)
    {
        color = "\033[0;90m";  // Dim gray
        level_text = "[INTERNAL]";
    }
    else if (level == LogLevel::Debug)
    {
        color = "\033[1;92m";  // Bright green
        level_text = "[DEBUG]";
    }
    else
    {                          // Info
        color = "\033[1;36m";  // Bright cyan
        level_text = "[INFO]";
    }
    const char* reset = "\033[0m";
    const char* arrow = "->";

    printf("%s%s %s %s:%d %s %s\n%s", color, level_text, arrow, display_name, line, arrow, buffer, reset);
}

}  // namespace Deki
