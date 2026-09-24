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

// Forward declaration to avoid circular dependency
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

// Color format options for Engine
enum class ColorFormat
{
    RGB565 = 0,    // 16-bit: 2 bytes per pixel
    RGB888 = 1,    // 24-bit: 3 bytes per pixel
    ARGB8888 = 2,  // 32-bit: 4 bytes per pixel with alpha
    RGB565A8 = 3   // 24-bit: 3 bytes per pixel - RGB565 (2 bytes) + A8 (1 byte)
};

/**
 * @brief Bytes a `width` x `height` framebuffer of `format` occupies
 *
 * Here, once, because it was previously a ladder of literals inside a display
 * package -- `(format == 2) ? 4 : (format == 1) ? 3 : 2` -- which gave
 * RGB565A8 two bytes per pixel instead of three. That number decides how much
 * of a PSRAM framebuffer is flushed from CPU cache before DMA reads it, so
 * undercounting leaves the tail of the screen showing stale pixels.
 *
 * Whole-framebuffer rather than bytes-per-pixel on purpose. LovyanGFX drives
 * 1-bit monochrome panels (SSD1306 and the like) and e-paper, where a pixel
 * costs less than a byte and any per-pixel answer is either zero or a lie.
 * Adding MONO1 here is one arm returning (w * h + 7) / 8 and no caller
 * changes.
 */
constexpr size_t FrameBufferBytes(ColorFormat format, int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;

    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    switch (format)
    {
        case ColorFormat::RGB565:   return pixels * 2;
        case ColorFormat::RGB888:   return pixels * 3;
        case ColorFormat::ARGB8888: return pixels * 4;
        case ColorFormat::RGB565A8: return pixels * 3;  // RGB565 plane + A8 plane
    }
    return 0;
}

// Engine settings (loaded from project config)
// Global settings accessible without Engine initialization
struct DEKI_ENGINE_API EngineSettings
{
    // Inspector display preference (Pixels or Meters). This is purely a UI
    // concern: internal storage, serialization, runtime math and Box2D
    // integration always work in meters. The inspector multiplies by
    // pixelsPerMeter for display when inspectorUnit == Pixels.
    UnitMode inspectorUnit = UnitMode::Pixels;

    // Art density: how many pixels of art make one meter of world. Sizes
    // sprites, converts the inspector's px display, and is the scale at which
    // the design area is drawn 1:1.
    float pixelsPerMeter = 16.0f;

    // The design area: how much world the player sees, in meters, and what a
    // screen of another shape does with it. See ScreenScale.h.
    float designWidth = 20.0f;
    float designHeight = 15.0f;
    ScreenFit screenFit = ScreenFit::ShowAll;
    bool pixelPerfect = false;

    // Inspector display only; stored temperatures are always Celsius.
    TemperatureUnit temperatureUnit = TemperatureUnit::Celsius;

    // Global instance for use by Object etc without requiring engine init
    static EngineSettings& Global();
};

// How the engine is hosted. A device runs everything itself; the editor owns
// the providers, input, presenting and the frame loop and only asks the
// engine for lifecycle + render. One loop, two configurations - there used to
// be two copies of Initialize/Update/Shutdown that had drifted apart.
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

// Update callback function type
using UpdateCallback = std::function<void(uint32_t deltaTime)>;

using GameUpdateCallback = std::function<void()>;

// Include core game engine headers

// Include reflection headers

// Include utility headers

// Include interface headers


// Forward declarations
class Object;

/**
 * @brief Engine singleton: owns the scene system, lifecycle and frame loop.
 *
 * Holds the active scene graph via SceneSystem, drives the component
 * lifecycle (Awake, Start, Update) through LifecycleManager, and dispatches
 * to whichever render, input and display providers a package has installed -
 * all four are optional, so a headless or editor-hosted engine is the same
 * class with null providers.
 */
class DEKI_ENGINE_API Engine
{
private:
    bool m_Initialized;
    bool m_IsDirty;  // Track if engine needs redraw

    // Scene management system
    SceneSystem* m_SceneSystem;

    // Lifecycle management (PreRender, Start, Update)
    LifecycleManager* m_LifecycleManager;

    // Rendering system (provided by rendering package, nullptr if headless)
    IRenderSystem* m_Renderer;

    // Input system (provided by input package, nullptr if no input)
    IInputSystem* m_InputSystem;

    // Display (provided by a display integration package: SDL3, LovyanGFX, editor preview).
    // Package owns lifetime; engine-core just holds the pointer + name. Replaces the
    // old static DekiDisplay facade.
    IDisplay* m_Display = nullptr;
    std::string m_DisplayName;

    // The framebuffer's colour format, fixed by the target's build. Its size is
    // not: it is whatever the display reports once one is set.
    ColorFormat m_FrameFormat = ColorFormat::RGB565;
    bool m_NoFramebufferLogged = false;
    uint32_t m_FramesWithoutFramebuffer = 0;
    static constexpr uint32_t kFramesBeforeNoDisplayError = 120;  // ~2 s at 60 fps

    // Size the render system's framebuffer to the display. Logs and returns
    // false when there is no display, it reports no size, or the allocation fails.
    bool SizeFramebufferToDisplay();

    // Update callbacks
    std::vector<UpdateCallback> m_UpdateCallbacks;

    GameUpdateCallback m_GameUpdateCallback;

    // Main loop configuration
    uint32_t m_TargetFps;
    bool m_Running;

    // FPS counter
    FPSCounter m_FpsCounter;

    // Engine settings
    EngineSettings m_Settings;

    // How this engine instance is hosted (see EngineHost)
    EngineHost m_Host;

public:
    /**
     * @brief Get the singleton instance of Engine
     * @return Reference to the Engine instance
     */
    static Engine& GetInstance();

    /**
     * @brief Initialize the engine from the active project settings
     * @return true if initialization successful, false otherwise
     *
     * Picks the host configuration for this build: EngineHost::Editor()
     * in editor builds, EngineHost::Device() otherwise.
     */
    static bool Initialize();

    /**
     * @brief Initialize the engine for an explicit host configuration
     */
    static bool Initialize(const EngineHost& host);

    const EngineHost& GetHost() const { return m_Host; }

    /**
     * @brief Shutdown the game engine and free resources
     */
    void Shutdown();

    /**
     * @brief Update game logic and animations (called every frame)
     * @param deltaTime Time since last update in milliseconds
     */
    void Update(uint32_t deltaTime);

    /**
     * @brief Process lifecycle methods on the active scene
     * Handles Awake, Start, Update and detects scene changes during lifecycle
     */
    void ProcessLifecycle();

    /**
     * @brief Render the current scene to internal buffer
     */
    void Render();

    /**
     * @brief Present the render system's framebuffer through the display.
     *
     * Only the rectangles the render system reports as changed are pushed
     * when the display can present partial frames (IRenderSystem::
     * GetPresentRects + IDisplay::PresentRegions); otherwise the whole
     * frame. Every present in the engine goes through here.
     */
    void PresentFrame();

    /**
     * @brief Make the next present cover the whole screen (the display's own
     *        RequestFullRefresh included).
     */
    void RequestFullRefresh();

    /**
     * @brief Register an update callback to be called each frame
     * @param callback Function to call during update
     * @return ID of the registered callback (can be used to unregister)
     */
    size_t RegisterUpdate(const UpdateCallback& callback);

    /**
     * @brief Remove a callback registered with RegisterUpdate
     * @param id The value RegisterUpdate returned
     *
     * A package DLL that registers a callback MUST unregister it before it
     * unloads, or the engine calls into unmapped code on the next frame.
     */
    void UnregisterUpdate(size_t id);

    /**
     * @brief Run the registered update callbacks once
     * @param deltaTime Milliseconds since the previous call
     *
     * Update() does this itself. Hosts that drive the lifecycle by hand (the
     * editor's play mode) call it so package systems (tweens, ...) tick there
     * exactly as they do on a device.
     */
    void RunUpdateCallbacks(uint32_t deltaTime);

    /**
     * @brief Set the current root scene
     * @param scene Pointer to the scene to set as root
     * @param takeOwnership If true, engine will delete the scene when replaced or on shutdown
     */
    void SetRootScene(Scene* scene, bool takeOwnership = true);

    /**
     * @brief Get the current root scene
     * @return Pointer to the current root scene, or nullptr if none set
     */
    Scene* GetRootScene() const;

    /**
     * @brief Create a new empty scene
     * @return Pointer to the created scene
     */
    Scene* CreateScene();

    /**
     * @brief Get the renderer instance
     * @return The installed render system, or nullptr when headless
     */
    IRenderSystem& GetRenderer() const { return *m_Renderer; }

    /**
     * @brief Get the scene system instance
     * @return Reference to the SceneSystem instance
     */
    SceneSystem& GetSceneSystem() const { return *m_SceneSystem; }

    /**
     * @brief Get the lifecycle manager instance
     * @return Pointer to the LifecycleManager instance
     */
    LifecycleManager* GetLifecycleManager() const { return m_LifecycleManager; }

    /**
     * @brief Get the render system instance
     * @return Pointer to the DekiRenderSystem instance (nullptr if no rendering package)
     */
    IRenderSystem* GetRenderSystem() const { return m_Renderer; }

    /**
     * @brief Set the render system (called by rendering package during registration)
     * @param rs Pointer to the render system (package owns lifetime)
     */
    void SetRenderSystem(IRenderSystem* rs) { m_Renderer = rs; }

    /**
     * @brief Get the input system instance
     * @return Pointer to the IInputSystem instance (nullptr if no input package)
     */
    IInputSystem* GetInputSystem() const { return m_InputSystem; }

    /**
     * @brief Set the input system (called by input package during registration)
     * @param is Pointer to the input system (package owns lifetime)
     */
    void SetInputSystem(IInputSystem* is) { m_InputSystem = is; }

    /**
     * @brief Get the active display.
     * Set at runtime by a display integration package via SetDisplay().
     * nullptr if no display package is loaded.
     */
    IDisplay* GetDisplay() const { return m_Display; }

    /**
     * @brief Register the active display (called by SDL3Display, LovyanGFXDisplay,
     *        EditorDisplay, etc. at package load).
     *
     * The package owns the display instance's lifetime. Engine-core stores the
     * pointer + a name (for diagnostics) and sizes the framebuffer to the size
     * the display reports: every target renders at its own native resolution.
     */
    void SetDisplay(IDisplay* display, const std::string& name = "");

    /**
     * @brief Display name set by the registering package (e.g. "SDL3").
     */
    const std::string& GetDisplayName() const { return m_DisplayName; }

    /**
     * @brief True iff a display has been registered AND its IsInitialized() is true.
     */
    bool IsDisplayInitialized() const { return m_Display && m_Display->IsInitialized(); }

    /**
     * @brief Check if the game engine is initialized
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return m_Initialized; }

    /**
     * @brief Check if the engine is in runtime mode (play mode or device)
     *
     * In runtime mode, components follow the device code path:
     * no editor GUID sync, no font preview, no on-demand baking.
     * On device (!DEKI_EDITOR), always returns true.
     * In editor, returns true only during play mode.
     */
    static bool IsRuntimeMode();

    /**
     * @brief Set runtime mode (called by editor play mode init/shutdown)
     */
    static void SetRuntimeMode(bool runtime);

    /**
     * @brief Set the game update callback for game logic updates
     * @param callback Function to call for game logic updates
     */
    void SetGameUpdateCallback(const GameUpdateCallback& callback);

    /**
     * @brief Set the target FPS for the main loop
     * @param fps Target frames per second (default: 30)
     */
    void SetTargetFPS(uint32_t fps);

    /**
     * @brief Run the main game loop
     * This handles timing, platform updates, rendering, and callbacks
     */
    void RunMainLoop();

    /**
     * @brief Stop the main game loop
     */
    void StopMainLoop();

    /**
     * @brief Check if the main loop is running
     * @return true if running, false otherwise
     */
    bool IsRunning() const { return m_Running; }

    /**
     * @brief Mark the engine as dirty (needs redraw)
     */
    void MarkDirty() { m_IsDirty = true; }

    /**
     * @brief Check if engine needs redraw
     * @return true if dirty, false otherwise
     */
    bool IsDirty() const { return m_IsDirty; }

    // Renderer access methods for external systems
    const uint8_t* GetFrameBuffer() const;
    int32_t GetScreenWidth() const;
    int32_t GetScreenHeight() const;
    ColorFormat GetFrameBufferFormat() const;

    /**
     * @brief Get engine settings
     * @return Reference to the engine settings
     */
    EngineSettings& GetSettings() { return m_Settings; }
    const EngineSettings& GetSettings() const { return m_Settings; }

private:
    // Private constructor and destructor (singleton pattern)
    Engine();
    ~Engine();

    static bool s_runtimeMode;
};

}  // namespace Deki
