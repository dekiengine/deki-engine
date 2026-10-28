#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace Deki
{

class ITimeProvider;

/// The engine's clock, independent of the platform. All times are in
/// milliseconds.
class Time
{
public:
    /// Call once at engine startup.
    static void Initialize();

    /// Milliseconds since Initialize().
    static uint32_t GetTime();
    /// Microseconds on the platform's clock, for timing frames finer than a
    /// millisecond. Only differences between two readings mean anything.
    static uint64_t GetTimeUs();

    /// Milliseconds since `previousTime`, a value from GetTime().
    static uint32_t GetElapsed(uint32_t previousTime);

    /// Call once per frame with the frame's duration in milliseconds.
    static void Update(uint32_t deltaTime);

    /// Duration of the last frame in milliseconds.
    static uint32_t GetDeltaTime();

    /// Duration of the last frame in milliseconds, with sub-millisecond precision.
    static float GetDeltaTimeF();

    /// Update() with a float duration, for sub-millisecond precision (the
    /// editor's frame limiter).
    static void UpdateF(float deltaTimeMs);

    static float GetFPS();

    /// Call after loading a scene, so the next frame's delta does not include
    /// the loading time.
    static void ResetFrameTime();

    /// Sets the platform's sleep function. Platform init calls it before
    /// RunMainLoop starts.
    static void SetDelayFunction(std::function<void(uint32_t)> fn);

    /// Sleeps `ms` milliseconds through the delay function; does nothing when
    /// none is set.
    static void Delay(uint32_t ms);

    /// Sets the platform's time provider and takes ownership of it. Call it
    /// before Initialize().
    static void SetTimeProvider(std::unique_ptr<ITimeProvider> provider);

private:
    static std::unique_ptr<ITimeProvider> s_TimeProvider;
    static uint32_t s_CurrentTime;
    static uint32_t s_LastFrameTime;
    static uint32_t s_DeltaTime;
    static float s_DeltaTimeF;
    static uint32_t s_StartTime;
    static float s_Fps;
    static uint32_t s_FpsCounter;  // frames counted toward the FPS figure
    static uint32_t s_FpsTime;     // time accumulated toward the FPS figure
};

}  // namespace Deki
