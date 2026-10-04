#include <deki/LogSystem.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace Deki
{

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
    // the cost of unbuffered I/O, which is high over serial on ESP32.
    // The size MUST be > 0 for _IOLBF/_IOFBF: MSVC's CRT fast-fails
    // (0xC0000409) on size 0. BUFSIZ is valid everywhere.
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

    // The file name for display, found in place: __FILE__ is an absolute
    // path, and some paths log every frame, so no heap allocation here.
    const char* displayName = file ? file : "";
    for (const char* p = displayName; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            displayName = p + 1;
        }
    }

    // Call callback if registered (for editor console integration)
    if (s_LogCallback)
    {
        char full[640];
        snprintf(full, sizeof(full), "%s:%d -> %s", displayName, line, buffer);
        s_LogCallback(level, std::string(full), file, line);
    }

    // Editor level logs are only sent to callback, not printed to stdout
    if (level == LogLevel::Editor)
    {
        return;
    }

    const char* color = "";
    const char* levelText = "[INFO]";

    if (level == LogLevel::Error)
    {
        color = "\033[1;91m";  // Bright red
        levelText = "[ERROR]";
    }
    else if (level == LogLevel::Warning)
    {
        color = "\033[1;93m";  // Bright yellow
        levelText = "[WARN]";
    }
    else if (level == LogLevel::Internal)
    {
        color = "\033[0;90m";  // Dim gray
        levelText = "[INTERNAL]";
    }
    else if (level == LogLevel::Debug)
    {
        color = "\033[1;92m";  // Bright green
        levelText = "[DEBUG]";
    }
    else
    {                          // Info
        color = "\033[1;36m";  // Bright cyan
        levelText = "[INFO]";
    }
    const char* reset = "\033[0m";
    const char* arrow = "->";

    // stderr, not stdout. The log is diagnostics; stdout is where a command's
    // result goes, and a caller must be able to parse the JSON the editor's
    // headless verbs print there.
    // The reset goes before the newline. After it, the escape would land at
    // the start of the next line wherever stdout and stderr are combined.
    fprintf(stderr, "%s%s %s %s:%d %s %s%s\n", color, levelText, arrow, displayName, line, arrow, buffer, reset);
}

}  // namespace Deki
