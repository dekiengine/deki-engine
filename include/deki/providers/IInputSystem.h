#pragma once
#include <cstdint>

namespace Deki
{

class Scene;

/// The input system as the engine sees it, so it need not link to the
/// implementation in the input package DLL.
///
/// Coordinates are in world units (float); the implementation converts raw
/// device pixels before they reach colliders.
class IInputSystem
{
public:
    virtual ~IInputSystem() = default;

    virtual void Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual void DispatchInput(Scene* scene, float x, float y, bool down, bool move, bool up) = 0;

    // Feeds a key press or release from the host, such as the editor's play
    // view forwarding keys, so game code polls keys as it would with a real
    // keyboard driver. Key codes follow InputKeys.h.
    virtual void DispatchKey(uint32_t key, bool down) = 0;

    virtual bool IsInitialized() const = 0;

    // Polls the input drivers. Engine::Update calls it once per frame.
    virtual void Update() = 0;

    // True when an input driver asked the application to quit (such as a
    // window close). Engine::RunMainLoop polls it.
    virtual bool ShouldExit() const = 0;
};

}  // namespace Deki
