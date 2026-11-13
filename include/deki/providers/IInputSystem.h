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

    // Feeds a pointer from the host in the game screen's pixels, such as the
    // editor's Play view forwarding the mouse, so game code hears it as an
    // input event and colliders get it, as with a touch on a device. Returns
    // false when this input system cannot; the host then calls DispatchInput.
    virtual bool DispatchPointer(int32_t screenX, int32_t screenY, bool down, bool move, bool up)
    {
        (void)screenX;
        (void)screenY;
        (void)down;
        (void)move;
        (void)up;
        return false;
    }

    virtual bool IsInitialized() const = 0;

    // Polls the input drivers. Engine::Update calls it once per frame.
    virtual void Update() = 0;

    // True when an input driver asked the application to quit (such as a
    // window close). Engine::RunMainLoop polls it.
    virtual bool ShouldExit() const = 0;

    // Feeds where a host's analog stick points (0 the left, 1 the right), such
    // as the editor's Play view forwarding a gamepad: x and y each from -1 to
    // 1, right and up positive. Game code reads it as DekiInput::GetStick.
    virtual void DispatchStick(int stick, float x, float y)
    {
        (void)stick;
        (void)x;
        (void)y;
    }
};

}  // namespace Deki
