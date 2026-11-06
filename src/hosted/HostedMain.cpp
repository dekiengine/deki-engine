// The program's entry for a game that runs as a program on an operating
// system: the desktop simulator, a Linux handheld. It sets up the memory and
// file system the engine expects, then starts it. A board has its own entry
// instead (deki-esp32-integration's app_main).
//
// Not part of deki-engine-core: it is the deki-engine-hosted object library,
// which a build that makes an executable links (CMakeGen's
// GenerateExecutableCMakeLists). The editor and the test programs have their
// own main(), and a board's build never compiles this folder.

#include <deki/LogSystem.h>
#include <deki/Main.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/HostFileSystem.h>
#include <deki/providers/HostMemoryProvider.h>
#include <deki/providers/Memory.h>

#include <cstdio>
#include <string>

// At global scope: the runtime looks for ::main only, and inside a namespace
// the link fails with an error naming WinMain, far from the cause.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: logs survive a crash
    // No editor console here: engine logs go to dekigame.log in the working
    // directory (next to flash/ and storage/), and to stdout.
    Deki::LogSystem::SetLogCallback(
        [](Deki::LogLevel level, const std::string& msg, const char* file, int line)
        {
            (void)level;
            (void)file;
            (void)line;
            static FILE* lf = fopen("dekigame.log", "w");
            if (lf)
            {
                fprintf(lf, "%s\n", msg.c_str());
                fflush(lf);
            }
            printf("%s\n", msg.c_str());
        });
    // The providers must exist before Engine::Initialize() runs, since it
    // initializes Memory and FileSystem. Set here rather than in a static
    // initializer, so static-init order cannot bite the provider singletons.
    Deki::Memory::SetBackend(new Deki::HostMemoryProvider());
    // Engine::Initialize() then finds the assets in F:/assets/ (./flash/assets/).
    Deki::FileSystem::SetFileSystem(new Deki::HostFileSystem());
    return Deki::Main();
}
