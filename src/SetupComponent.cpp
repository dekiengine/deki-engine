#include <deki/SetupComponent.h>

#ifdef DEKI_EDITOR
#include <deki/LogSystem.h>
#include <vector>
#endif

namespace Deki
{

#ifdef DEKI_EDITOR

namespace
{
struct Entry
{
    SetupComponent::EditorAutoFactory factory;
    SetupComponent::SetupPhase phase;
    bool fired;
};

// Single registry instance lives in deki-engine-core.dll. Package DLLs
// hit it via the dllimport'd RegisterEditorAutoFactory wrapper.
std::vector<Entry>& Entries()
{
    static std::vector<Entry> v;
    return v;
}
}  // namespace

void SetupComponent::RegisterEditorAutoFactory(EditorAutoFactory factory, SetupPhase phase)
{
    if (factory)
    {
        Entries().push_back({ factory, phase, false });
    }
}

void SetupComponent::ClearEditorAutoFactories()
{
    Entries().clear();
}

void SetupComponent::RunEditorAutoSetups(SetupPhase phase)
{
    // Idempotent: only fire entries that haven't been fired yet. Safe to call
    // after every package load so newly installed packages' editor setup runs
    // without re-running the ones already live.
    for (auto& e : Entries())
    {
        if (e.fired || e.phase != phase)
        {
            continue;
        }
        SetupComponent* c = e.factory();
        e.fired = true;
        if (!c)
        {
            continue;
        }
        DEKI_LOG_INTERNAL("[SetupComponent] auto-setup: %s", c->GetSetupName());
        c->Setup([](bool /*ok*/) { /* per-component error already logged */ });
        // Driver lives via static s_Driver in the component's .cpp; the
        // component instance itself is not needed past Setup().
        delete c;
    }
}

#endif  // DEKI_EDITOR

}  // namespace Deki
