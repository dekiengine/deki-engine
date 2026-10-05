#pragma once

#include <deki/Component.h>
#include <deki/SetupComponent.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include <vector>
#include <string>

namespace Deki
{

/// Runs the boot scene's SetupComponents (such as SDCardComponent) one at a
/// time, in order, waiting for each to finish, then loads the startup scene.
/// If a setup fails it stops there and the startup scene is not loaded.
///
/// In a boot scene:
///   BootRoot
///   ├── PlatformSetupComponent
///   │   └── setupComponents: [→SDCardComponent]
///   └── SDCardObj
///       └── SDCardComponent
DEKI_CATEGORY("System")
DEKI_DESCRIPTION("Runs each Setup Component in order at boot, then loads the startup scene.")
class PlatformSetupComponent : public Component
{
public:
    // The SetupComponents to run, in order.
    DEKI_EXPORT
    std::vector<ObjectRef<SetupComponent>> setupComponents;

    PlatformSetupComponent();
    virtual ~PlatformSetupComponent();

    void Start() override;
    void Update() override;

private:
    int m_CurrentIndex = 0;
    bool m_SetupComplete = false;
    bool m_SetupFailed = false;
    bool m_WaitingToLoadScene = false;

    void StartNextSetup();
    void OnSetupComplete(bool success);
    void LoadStartupScene();
};

}  // namespace Deki
