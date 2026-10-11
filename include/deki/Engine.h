#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <deki/ICamera.h>  // for UnitMode in DekiEngineSettings

#include <deki/Export.h>  // DEKI_ENGINE_API
#include <deki/Color.h>
#include <deki/Component.h>
#include <deki/Object.h>
#include <deki/Scene.h>
#include <deki/SceneSystem.h>
#include <deki/Time.h>

#include <deki/reflection/Property.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>
#include <deki/FPSCounter.h>
#include <deki/providers/IDisplay.h>
#include <string>

namespace Deki
{

class IRenderSystem;
class IInputSystem;
class SceneSystem;
class LifecycleManager;

// Pixel format of a framebuffer.
enum class ColorFormat
{
    RGB565 = 0,    // 16-bit: 2 bytes per pixel
    RGB888 = 1,    // 24-bit: 3 bytes per pixel
    ARGB8888 = 2,  // 32-bit: 4 bytes per pixel with alpha
    RGB565A8 = 3   // 24-bit: 3 bytes per pixel - RGB565 (2 bytes) + A8 (1 byte)
};

/// Bytes a `width` x `height` framebuffer of `format` occupies. Use this
/// rather than counting bytes per pixel yourself: it decides how much of a
/// PSRAM framebuffer is flushed from CPU cache before DMA reads it, and an
/// undercount leaves the bottom of the screen showing stale pixels.
///
/// It answers for the whole framebuffer, not per pixel, because 1-bit
/// monochrome panels and e-paper use less than a byte per pixel. A MONO1
/// format would be one more case returning (w * h + 7) / 8.
constexpr size_t FrameBufferBytes(ColorFormat format, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }

    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    switch (format)
    {
        case ColorFormat::RGB565: return pixels * 2;
        case ColorFormat::RGB888: return pixels * 3;
        case ColorFormat::ARGB8888: return pixels * 4;
        case ColorFormat::RGB565A8: return pixels * 3;  // RGB565 plane + A8 plane
    }
    return 0;
}

// Engine settings, loaded from the project config. Global() reads them
// without an initialised Engine.
struct DEKI_ENGINE_API EngineSettings
{
    // Units the inspector shows. Display only: storage, serialization,
    // runtime math and Box2D always work in meters. In Pixels mode the
    // inspector multiplies by pixelsPerMeter.
    UnitMode inspectorUnit = UnitMode::Pixels;

    // Art density: how many pixels of art make one meter of world. Sizes
    // sprites, converts the inspector's px display, and is the grid a
    // pixel-perfect camera scales by.
    float pixelsPerMeter = 16.0f;

    // Inspector display only; stored temperatures are always Celsius.
    TemperatureUnit temperatureUnit = TemperatureUnit::Celsius;

    static EngineSettings& Global();
};

// How the engine is hosted. A device runs everything itself; the editor owns
// the providers, input, presenting and the frame loop and only asks the
// engine for lifecycle + render. Both run the same loop with different flags.
struct DEKI_ENGINE_API EngineHost
{
    bool initializeProviders = true;  // FileSystem, ProjectSettings, Memory in Initialize()
    bool pollInput = true;            // IInputSystem::Update() at the top of Update()
    bool tickPower = true;            // Power tick + sleep skip in Update()
    bool renderAndPresent = true;     // Update() renders and presents; else the host calls Render()
    bool logFrameStats = true;        // FPS line every 120 frames
    bool ownsMainLoop = true;         // RunMainLoop() drives the frames

    static EngineHost Device();
    static EngineHost Editor();
};

// deltaTime is in milliseconds.
using UpdateCallback = std::function<void(uint32_t deltaTime)>;

using GameUpdateCallback = std::function<void()>;

class Object;

/// Engine singleton: owns the scene system, lifecycle and frame loop.
///
/// Holds the active scene graph through SceneSystem, drives the component
/// lifecycle (Awake, Start, Update) through LifecycleManager, and calls
/// whichever render, input and display providers packages have installed.
/// The providers are optional, so a headless or editor-hosted engine is the
/// same class with null providers.
class DEKI_ENGINE_API Engine
{
public:
    /// How long the parts of a frame took, in milliseconds of the engine's
    /// clock: update (input, lifecycle, update callbacks), render and present.
    /// For a frame-rate readout; average over many frames, as each is whole
    /// milliseconds.
    struct FrameTimings
    {
        uint32_t update = 0;
        uint32_t render = 0;
        uint32_t present = 0;
    };

private:
    bool m_Initialized;
    bool m_IsDirty;  // needs a redraw

    SceneSystem* m_SceneSystem;

    LifecycleManager* m_LifecycleManager;

    // From a rendering package; nullptr when headless.
    IRenderSystem* m_Renderer;

    // From an input package; nullptr when there is no input.
    IInputSystem* m_InputSystem;

    // From a display package (SDL3, LovyanGFX, editor preview), which owns it.
    IDisplay* m_Display = nullptr;
    std::string m_DisplayName;

    bool m_NoFramebufferLogged = false;
    uint32_t m_FramesWithoutFramebuffer = 0;
    static constexpr uint32_t kFramesBeforeNoDisplayError = 120;  // ~2 s at 60 fps

    // Size and format the render system's framebuffer to the display. Logs and returns
    // false when there is no display, it reports no size, or the allocation fails.
    bool SizeFramebufferToDisplay();

    std::vector<UpdateCallback> m_UpdateCallbacks;

    GameUpdateCallback m_GameUpdateCallback;

    uint32_t m_TargetFps;
    bool m_Running;

    FPSCounter m_FpsCounter;
    FrameTimings m_LastFrameTimings;

    EngineSettings m_Settings;

    EngineHost m_Host;

public:
    static Engine& GetInstance();

    /// Starts the engine from the active project settings, with
    /// EngineHost::Editor() in editor builds and EngineHost::Device()
    /// otherwise. Returns false on failure.
    static bool Initialize();

    /// Starts the engine with the given host configuration.
    static bool Initialize(const EngineHost& host);

    const EngineHost& GetHost() const { return m_Host; }

    /// Shuts the engine down and frees its resources.
    void Shutdown();

    /// Runs one frame. `deltaTime` is milliseconds since the last call.
    void Update(uint32_t deltaTime);

    /// Runs Awake, Start and Update on the active scene, and handles a scene
    /// change made during them.
    void ProcessLifecycle();

    /// Renders the current scene into the render system's framebuffer.
    void Render();

    /// Sends the render system's framebuffer to the display. When the display
    /// can present partial frames, only the changed rectangles are sent
    /// (IRenderSystem::GetPresentRects + IDisplay::PresentRegions); otherwise
    /// the whole frame. Every present in the engine goes through here.
    void PresentFrame();

    /// Makes the next present cover the whole screen, including the
    /// display's own RequestFullRefresh.
    void RequestFullRefresh();

    /// Calls `callback` every frame. Returns an id for UnregisterUpdate.
    size_t RegisterUpdate(const UpdateCallback& callback);

    /// Removes the callback that RegisterUpdate returned `id` for. A package
    /// DLL that registers a callback must unregister it before it unloads, or
    /// the engine calls into unmapped code on the next frame.
    void UnregisterUpdate(size_t id);

    /// Runs the registered update callbacks once. `deltaTime` is milliseconds
    /// since the previous call. Update() does this itself; hosts that drive the
    /// lifecycle by hand (the editor's play mode) call it so package systems
    /// (tweens, ...) tick there as they do on a device.
    void RunUpdateCallbacks(uint32_t deltaTime);

    /// Makes `scene` the root scene. With `takeOwnership`, the engine deletes
    /// it when it is replaced or on shutdown.
    void SetRootScene(Scene* scene, bool takeOwnership = true);

    /// The root scene, or nullptr when none is set.
    Scene* GetRootScene() const;

    /// Creates an empty scene.
    Scene* CreateScene();

    /// The installed render system. Call it only when one is installed (see
    /// GetRenderSystem).
    IRenderSystem& GetRenderer() const { return *m_Renderer; }

    SceneSystem& GetSceneSystem() const { return *m_SceneSystem; }

    LifecycleManager* GetLifecycleManager() const { return m_LifecycleManager; }

    /// The render system, or nullptr when no rendering package is loaded.
    IRenderSystem* GetRenderSystem() const { return m_Renderer; }

    /// Called by a rendering package when it registers. The package owns `rs`.
    void SetRenderSystem(IRenderSystem* rs) { m_Renderer = rs; }

    /// The input system, or nullptr when no input package is loaded.
    IInputSystem* GetInputSystem() const { return m_InputSystem; }

    /// Called by an input package when it registers. The package owns `is`.
    void SetInputSystem(IInputSystem* is) { m_InputSystem = is; }

    /// The active display, set by a display package through SetDisplay(), or
    /// nullptr when none is loaded.
    IDisplay* GetDisplay() const { return m_Display; }

    /// Registers the active display; SDL3Display, LovyanGFXDisplay,
    /// EditorDisplay and the like call it when their package loads. The
    /// package owns the display. The engine keeps the pointer and a name for
    /// diagnostics, and sizes the framebuffer to the display, so every target
    /// renders at its own native resolution.
    void SetDisplay(IDisplay* display, const std::string& name = "");

    /// Display name set by the registering package, such as "SDL3".
    const std::string& GetDisplayName() const { return m_DisplayName; }

    /// True when a display is registered and its IsInitialized() is true.
    bool IsDisplayInitialized() const { return m_Display && m_Display->IsInitialized(); }

    bool IsInitialized() const { return m_Initialized; }

    /// True on a device, and in the editor during play mode. In runtime mode
    /// components follow the device code path: no editor GUID sync, no font
    /// preview, no on-demand baking.
    static bool IsRuntimeMode();

    /// Called by the editor's play mode when it starts and stops.
    static void SetRuntimeMode(bool runtime);

    /// Sets the callback that runs game logic each frame.
    void SetGameUpdateCallback(const GameUpdateCallback& callback);

    /// Frame rate the main loop aims for. The default is 30.
    void SetTargetFPS(uint32_t fps);

    /// Runs the main loop (timing, platform updates, rendering, callbacks)
    /// until StopMainLoop().
    void RunMainLoop();

    void StopMainLoop();

    bool IsRunning() const { return m_Running; }

    /// Asks for a redraw.
    void MarkDirty() { m_IsDirty = true; }

    /// True when a redraw is needed.
    bool IsDirty() const { return m_IsDirty; }

    /// How long the parts of the last frame took: see FrameTimings.
    FrameTimings GetLastFrameTimings() const { return m_LastFrameTimings; }

    // The render system's framebuffer, for code outside the engine.
    const uint8_t* GetFrameBuffer() const;
    int32_t GetScreenWidth() const;
    int32_t GetScreenHeight() const;
    ColorFormat GetFrameBufferFormat() const;

    EngineSettings& GetSettings() { return m_Settings; }
    const EngineSettings& GetSettings() const { return m_Settings; }

private:
    // Singleton: use GetInstance().
    Engine();
    ~Engine();

    static bool s_RuntimeMode;
};

}  // namespace Deki
