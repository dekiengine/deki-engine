#pragma once

#include <deki/Behaviour.h>
#include <deki/reflection/Property.h>
#include <functional>

// DLL export/import for SetupComponent's editor-auto registry (editor builds only).
// All package DLLs share the single registry instance in deki-engine-core.dll.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define SETUP_COMPONENT_API __declspec(dllexport)
#else
#define SETUP_COMPONENT_API __declspec(dllimport)
#endif
#else
#define SETUP_COMPONENT_API
#endif
#else
#define SETUP_COMPONENT_API
#endif

/**
 * @brief Base class for components that need async setup during boot
 *
 * Similar pattern to RendererComponent - inherit from this to participate
 * in the boot sequence. BootSequenceComponent uses ObjectRef<SetupComponent>
 * to reference setup components.
 *
 * Usage:
 *   class MyHardwareComponent : public SetupComponent {
 *       DEKI_COMPONENT(MyHardwareComponent, SetupComponent, "System", "your-guid-here", "DEKI_FEATURE_MY_HARDWARE")
 *
 *       void Setup(SetupCallback onComplete) override {
 *           bool success = InitializeHardware();
 *           onComplete(success);
 *       }
 *
 *       const char* GetSetupName() const override { return "My Hardware"; }
 *   };
 */

namespace Deki
{

class SetupComponent : public Behaviour
{
public:
    DEKI_COMPONENT(SetupComponent, Behaviour, "System", "f2c936db-c42a-47f3-bf97-931cab7e44df", "")

    using SetupCallback = std::function<void(bool success)>;

    SetupComponent() = default;
    virtual ~SetupComponent() = default;

    /**
     * @brief Begin async setup. Call callback when done.
     *
     * Implementations should perform their initialization and then call
     * the callback with true on success, false on failure.
     *
     * @param onComplete Callback to invoke when setup finishes
     */
    virtual void Setup(SetupCallback onComplete) = 0;

    /**
     * @brief Get display name for logging
     * @return Human-readable name for this setup component
     */
    virtual const char* GetSetupName() const = 0;

    // ---- Editor-auto registry ----------------------------------------------
    // Editor-only mechanism for SetupComponents that should run automatically
    // when the editor finishes loading packages (parallel to embedded's
    // boot.scene / setupComponents mechanism, but driven by an in-memory
    // registry instead of a project-authored scene).

    using EditorAutoFactory = SetupComponent* (*)();

    SETUP_COMPONENT_API static void RegisterEditorAutoFactory(EditorAutoFactory factory);
    SETUP_COMPONENT_API static void ClearEditorAutoFactories();
    SETUP_COMPONENT_API static void RunEditorAutoSetups();
};

#ifdef DEKI_EDITOR
// Package-side helper for registering an editor-auto SetupComponent via static
// initializer. Usage in the SetupComponent's .cpp:
//     DEKI_REGISTER_EDITOR_AUTO_SETUP(MyEditorComponent);
template <typename T>
struct EditorAutoSetupRegistrar
{
    EditorAutoSetupRegistrar()
    {
        SetupComponent::RegisterEditorAutoFactory([]() -> SetupComponent*
                                                  { return new T(); });
    }
};
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName) \
    static ::Deki::EditorAutoSetupRegistrar<ClassName> s_##ClassName##_EditorAutoReg
#else
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName) /* embedded: no-op */
#endif

// Generated property metadata
#include "deki/generated/SetupComponent.gen.h"

}  // namespace Deki
