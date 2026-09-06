#pragma once
#include <cstdint>

namespace Deki
{


class Scene;

/**
 * @brief Abstract interface for the input dispatch system
 *
 * Engine uses this interface to interact with the input system
 * without linking to the concrete DekiInputSystem implementation
 * (which lives in the input package DLL).
 *
 * Coordinates are in world units (float). Raw device pixel events are
 * converted by the implementation before reaching colliders.
 */
class IInputSystem
{
public:
    virtual ~IInputSystem() = default;

    virtual void Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual void DispatchInput(Scene* scene, float x, float y,
                               bool down, bool move, bool up) = 0;

    // Inject a keyboard key state change from the host (e.g. the editor's
    // play view forwarding keys, so game code can poll keys the same way it
    // does with a real keyboard driver). Key codes follow InputKeys.h.
    virtual void DispatchKey(uint32_t key, bool down) = 0;

    virtual bool IsInitialized() const = 0;

    // Poll active input drivers (called once per frame by Engine::Update).
    virtual void Update() = 0;

    // Returns true when an input driver has signaled an application quit
    // (e.g. window close). Polled by Engine::RunMainLoop.
    virtual bool ShouldExit() const = 0;
};

}  // namespace Deki
