#pragma once

#include <deki/Component.h>
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
 *   DEKI_CATEGORY("System")
 *   class MyHardwareComponent : public Deki::SetupComponent {
 *   public:
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

DEKI_CATEGORY("System")
class SetupComponent : public Component
{
public:

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
    // Editor-only mechanism for SetupComponents that should run automatically,
    // without the project authoring them into a scene (parallel to embedded's
    // boot.scene / setupComponents mechanism, but driven by an in-memory
    // registry).
    //
    // A setup declares WHEN it runs, because the two answers are genuinely
    // different and the wrong one is a real cost:
    //
    //   EditorLoad  - as soon as packages finish loading, and so on every
    //                 project open and every hot reload. Right for something
    //                 cheap and local that the editor itself may want live: a
    //                 clock source, registering a client object. Wrong for
    //                 anything that talks to the network, touches hardware, or
    //                 costs the user something, because the user has not asked
    //                 for the game to do anything yet.
    //
    //   PlayStart   - when the user presses Play, which is the moment they
    //                 asked for the game to run. Right for anything with a
    //                 side effect the user would want to have consented to:
    //                 deki-gps geolocates by asking a third party where the
    //                 machine is, and doing that on project open meant opening
    //                 a project sent a request the user never asked for.
    //
    // Both are idempotent: an entry fires once, and a hot reload that clears
    // and re-registers it lets it fire again.
    enum class SetupPhase
    {
        EditorLoad,
        PlayStart,
    };

    using EditorAutoFactory = SetupComponent* (*)();

    SETUP_COMPONENT_API static void RegisterEditorAutoFactory(
        EditorAutoFactory factory, SetupPhase phase = SetupPhase::EditorLoad);
    SETUP_COMPONENT_API static void ClearEditorAutoFactories();
    SETUP_COMPONENT_API static void RunEditorAutoSetups(SetupPhase phase);
};

#ifdef DEKI_EDITOR
// Package-side helper for registering an auto SetupComponent via static
// initializer. Usage in the SetupComponent's .cpp, one or the other:
//     DEKI_REGISTER_EDITOR_AUTO_SETUP(MyEditorComponent);  // on project open
//     DEKI_REGISTER_PLAY_AUTO_SETUP(MyRuntimeComponent);   // on Play
template <typename T, SetupComponent::SetupPhase Phase>
struct AutoSetupRegistrar
{
    AutoSetupRegistrar()
    {
        SetupComponent::RegisterEditorAutoFactory(
            []() -> SetupComponent* { return new T(); }, Phase);
    }
};

/// Runs when the editor finishes loading packages: every project open, and
/// again after a hot reload. Keep it cheap and local. Anything that reaches the
/// network or the outside world belongs in DEKI_REGISTER_PLAY_AUTO_SETUP.
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName)                    \
    static ::Deki::AutoSetupRegistrar<ClassName,                      \
                                      ::Deki::SetupComponent::SetupPhase::EditorLoad> \
        s_##ClassName##_EditorAutoReg

/// Runs when the user presses Play, not before. For a setup with a side effect
/// the user would want to have asked for.
#define DEKI_REGISTER_PLAY_AUTO_SETUP(ClassName)                      \
    static ::Deki::AutoSetupRegistrar<ClassName,                      \
                                      ::Deki::SetupComponent::SetupPhase::PlayStart> \
        s_##ClassName##_PlayAutoReg
#else
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName) /* embedded: no-op */
#define DEKI_REGISTER_PLAY_AUTO_SETUP(ClassName)   /* embedded: no-op */
#endif

// Generated property metadata

}  // namespace Deki
