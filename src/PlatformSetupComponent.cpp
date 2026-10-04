#include <deki/PlatformSetupComponent.h>
#include <deki/Engine.h>
#include <deki/Time.h>
#include <deki/Scene.h>
#include "LifecycleManager.h"
#include <deki/assets/AssetManager.h>
#include <deki/ProjectSettings.h>
#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>

namespace Deki
{

PlatformSetupComponent::PlatformSetupComponent()
    : m_CurrentIndex(0),
      m_SetupComplete(false),
      m_SetupFailed(false)
{
}

PlatformSetupComponent::~PlatformSetupComponent()
{
}

void PlatformSetupComponent::Start()
{
    DEKI_LOG_INTERNAL("Start() called with %zu setup components", setupComponents.size());

    m_CurrentIndex = 0;
    m_SetupComplete = false;
    m_SetupFailed = false;

    for (size_t i = 0; i < setupComponents.size(); i++)
    {
        SetupComponent* sc = setupComponents[i].Get();
        DEKI_LOG_INTERNAL("  [%zu] id=%u ptr=%p name=%s", i, (unsigned)setupComponents[i].id, (void*)sc,
                          sc ? sc->GetSetupName() : "NULL");
    }

    DEKI_LOG_INTERNAL("  startup_scene='%s'", ProjectSettings::GetStartupScene());

    StartNextSetup();
}

void PlatformSetupComponent::Update()
{
    if (m_WaitingToLoadScene)
    {
        m_WaitingToLoadScene = false;
        LoadStartupScene();
    }
}

void PlatformSetupComponent::StartNextSetup()
{
    // Check if we've processed all components
    if (m_CurrentIndex >= static_cast<int>(setupComponents.size()))
    {
        // All setup complete - wait 1 frame before loading scene so scene can render
        m_SetupComplete = true;
        m_WaitingToLoadScene = true;
        DEKI_LOG_INTERNAL("All setup components completed, loading startup scene next frame");
        return;
    }

    // Get resolved component from ObjectRef
    SetupComponent* setupComp = setupComponents[m_CurrentIndex].Get();
    if (!setupComp)
    {
        DEKI_LOG_INTERNAL("Component at index %d is NULL, skipping", m_CurrentIndex);
        m_CurrentIndex++;
        StartNextSetup();
        return;
    }

    DEKI_LOG_INTERNAL("Running '%s' (%d/%zu)", setupComp->GetSetupName(), m_CurrentIndex + 1,
                      (int)setupComponents.size());

    // Call Setup with callback
    setupComp->Setup([this](bool success) { OnSetupComplete(success); });
}

void PlatformSetupComponent::OnSetupComplete(bool success)
{
    if (!success)
    {
        // Get component name for error message
        SetupComponent* setupComp = setupComponents[m_CurrentIndex].Get();
        const char* name = setupComp ? setupComp->GetSetupName() : "Unknown";

        DEKI_LOG_INTERNAL("FAILED: '%s'", name);
        DEKI_LOG_ERROR("PlatformSetup: Setup failed for '%s', stopping", name);

        m_SetupFailed = true;
        // Don't continue - platform setup stops on failure
        return;
    }

    // Move to next component
    m_CurrentIndex++;
    StartNextSetup();
}

void PlatformSetupComponent::LoadStartupScene()
{
    const char* scenePath = ProjectSettings::GetStartupScene();
    if (!scenePath || scenePath[0] == '\0')
    {
        DEKI_LOG_WARNING("PlatformSetup: No startup scene in project settings");
        return;
    }

    DEKI_LOG_INTERNAL("Loading startup scene '%s'", scenePath);

    Scene* scene = Deki::AssetManager::Get()->Load<Scene>(scenePath);
    if (scene)
    {
        // Take ownership - SceneSystem manages scene lifetime via deferred deletion
        Engine& engine = Engine::GetInstance();
        engine.SetRootScene(scene, true);

        // Process lifecycle immediately so sprites have correct frame coords
        // before the next render (prevents 1-frame glitch with wrong sprites)
        LifecycleManager* lifecycle = engine.GetLifecycleManager();
        if (lifecycle)
        {
            lifecycle->ProcessInternalAwake(scene);
            lifecycle->ProcessAwake(scene);
        }

        // Reset frame time so first Update() doesn't get huge deltaTime from loading
        Time::ResetFrameTime();

        DEKI_LOG_INTERNAL("PlatformSetup: Startup scene loaded successfully");

        // The scene is up and its components have taken what they need, so
        // this is the moment the numbers mean something: everything the boot
        // path allocates is in, and nothing the game loop allocates is yet.
        Memory::LogStats("startup scene");
    }
    else
    {
        DEKI_LOG_ERROR("PlatformSetup: Failed to load startup scene '%s'", scenePath);
    }
}

}  // namespace Deki
