#include <deki/Main.h>
#include <deki/Engine.h>
#include <deki/Storage.h>

int Deki::Main()
{
    if (!Deki::Engine::Initialize())
    {
        return 1;
    }

    Deki::Engine& deki = Deki::Engine::GetInstance();
    deki.GetSceneSystem().LoadBootScene("boot.scene", Deki::Storage::Internal);
    deki.RunMainLoop();
    deki.Shutdown();
    return 0;
}
