#pragma once

#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <functional>

// DLL export/import for SetupComponent's editor-auto registry (editor builds
// only), so every package DLL shares the one registry in deki-engine-core.dll.
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

namespace Deki
{

/// Base class for components that need asynchronous setup at boot. Derive
/// from it to take part in the boot sequence; PlatformSetupComponent lists
/// them with ObjectRef<SetupComponent> and runs them in order.
///
///   DEKI_CATEGORY("System")
///   class MyHardwareComponent : public Deki::SetupComponent {
///   public:
///       void Setup(SetupCallback onComplete) override {
///           bool success = InitializeHardware();
///           onComplete(success);
///       }
///
///       const char* GetSetupName() const override { return "My Hardware"; }
///   };
DEKI_CATEGORY("System")
class SetupComponent : public Component
{
public:
    using SetupCallback = std::function<void(bool success)>;

    SetupComponent() = default;
    virtual ~SetupComponent() = default;

    /// Starts the setup. When it finishes, call `onComplete` with true on
    /// success or false on failure.
    virtual void Setup(SetupCallback onComplete) = 0;

    /// Readable name for the log.
    virtual const char* GetSetupName() const = 0;

    // ---- Editor-auto registry ----------------------------------------------
    // Editor only: SetupComponents that run automatically, without the project
    // putting them in a scene (the editor's counterpart to a device's
    // boot.scene setupComponents list).
    //
    // A setup declares when it runs:
    //
    //   EditorLoad  - as soon as packages finish loading, so on every project
    //                 open and every hot reload. For something cheap and local
    //                 the editor may want live: a clock source, registering a
    //                 client object. Not for anything that reaches the
    //                 network, touches hardware or costs the user something,
    //                 since the user has not asked the game to do anything yet.
    //
    //   PlayStart   - when the user presses Play, the moment they ask the game
    //                 to run. For anything with a side effect the user should
    //                 agree to first, such as deki-gps asking a third party
    //                 where the machine is.
    //
    // Each entry fires once; a hot reload that clears and re-registers it lets
    // it fire again.
    enum class SetupPhase
    {
        EditorLoad,
        PlayStart,
    };

    using EditorAutoFactory = SetupComponent* (*)();

    SETUP_COMPONENT_API static void RegisterEditorAutoFactory(EditorAutoFactory factory,
                                                              SetupPhase phase = SetupPhase::EditorLoad);
    SETUP_COMPONENT_API static void ClearEditorAutoFactories();
    SETUP_COMPONENT_API static void RunEditorAutoSetups(SetupPhase phase);
};

#ifdef DEKI_EDITOR
// Registers an auto SetupComponent from a static initializer. In the
// SetupComponent's .cpp, one or the other:
//     DEKI_REGISTER_EDITOR_AUTO_SETUP(MyEditorComponent);  // on project open
//     DEKI_REGISTER_PLAY_AUTO_SETUP(MyRuntimeComponent);   // on Play
template <typename T, SetupComponent::SetupPhase Phase>
struct AutoSetupRegistrar
{
    AutoSetupRegistrar()
    {
        SetupComponent::RegisterEditorAutoFactory([]() -> SetupComponent* { return new T(); }, Phase);
    }
};

/// Runs when the editor finishes loading packages: every project open, and
/// again after a hot reload. Keep it cheap and local. Anything that reaches the
/// network or the outside world belongs in DEKI_REGISTER_PLAY_AUTO_SETUP.
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName)                                                                     \
    static ::Deki::AutoSetupRegistrar<ClassName, ::Deki::SetupComponent::SetupPhase::EditorLoad>                       \
        s_##ClassName##EditorAutoReg

/// Runs when the user presses Play, not before. For a setup with a side effect
/// the user would want to have asked for.
#define DEKI_REGISTER_PLAY_AUTO_SETUP(ClassName)                                                                       \
    static ::Deki::AutoSetupRegistrar<ClassName, ::Deki::SetupComponent::SetupPhase::PlayStart>                        \
        s_##ClassName##PlayAutoReg
#else
#define DEKI_REGISTER_EDITOR_AUTO_SETUP(ClassName) /* embedded: no-op */
#define DEKI_REGISTER_PLAY_AUTO_SETUP(ClassName)   /* embedded: no-op */
#endif

}  // namespace Deki
