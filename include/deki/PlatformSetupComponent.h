#pragma once

#include <deki/Behaviour.h>
#include <deki/SetupComponent.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include <vector>
#include <string>

/**
 * @brief Orchestrates sequential platform initialization of SetupComponents
 *
 * Add this component to a boot scene along with SetupComponents (like SDCardComponent).
 * It will call Setup() on each component in order, waiting for each to complete
 * before proceeding to the next. After all setup completes successfully,
 * it loads the startup scene.
 *
 * If any setup fails, the platform setup stops and the startup scene is not loaded.
 *
 * Usage in boot scene:
 *   BootRoot
 *   ├── PlatformSetupComponent
 *   │   └── setupComponents: [→SDCardComponent]
 *   └── SDCardObj
 *       └── SDCardComponent
 */

namespace Deki
{

class PlatformSetupComponent : public Behaviour
{
public:
    DEKI_COMPONENT(PlatformSetupComponent, Behaviour, "System", "e1b3c909-aaa1-49f4-8d62-04145f79f2a0", "")
    DEKI_DESCRIPTION("Runs each Setup Component in order at boot, then loads the startup scene.")

    /** @brief SetupComponents to initialize in order (use ObjectRef picker in editor) */
    // Boot scenes written before the camelCase rename spell this key
    // setup_components; without the alias they loaded an empty list and the
    // device came up with no display.
    DEKI_EXPORT
    DEKI_RENAMED_FROM("setup_components")
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

// Generated property metadata
#include "deki/generated/PlatformSetupComponent.gen.h"

}  // namespace Deki
