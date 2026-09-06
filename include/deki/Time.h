#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace Deki
{


class ITimeProvider;

/**
 * @brief Deki Engine timing system
 *
 * Provides platform-independent timing functionality for the engine.
 * All time values are in milliseconds for consistency.
 */
class Time
{
public:
    /**
     * @brief Initialize the timing system
     * Should be called once at engine startup
     */
    static void Initialize();

    /**
     * @brief Get current time in milliseconds
     * @return Current time since initialization in milliseconds
     */
    static uint32_t GetTime();

    /**
     * @brief Get elapsed time since a previous timestamp
     * @param previous_time Previous time value from GetTime()
     * @return Elapsed time in milliseconds
     */
    static uint32_t GetElapsed(uint32_t previous_time);

    /**
     * @brief Update the timing system
     * Should be called once per frame
     * @param deltaTime Frame delta time in milliseconds
     */
    static void Update(uint32_t deltaTime);

    /**
     * @brief Get the last frame delta time
     * @return Delta time of last frame in milliseconds
     */
    static uint32_t GetDeltaTime();

    /**
     * @brief Get the last frame delta time as float (high precision)
     * @return Delta time of last frame in milliseconds with sub-ms precision
     */
    static float GetDeltaTimeF();

    /**
     * @brief Update with float delta time for high precision (used by editor FPS limiter)
     * @param delta_time_ms Frame delta time in milliseconds (float for sub-ms precision)
     */
    static void UpdateF(float delta_time_ms);

    /**
     * @brief Get frames per second
     * @return Current FPS
     */
    static float GetFPS();

    /**
     * @brief Reset frame time after loading
     * Call after loading a scene to prevent huge deltaTime from accumulated loading time
     */
    static void ResetFrameTime();

    /**
     * @brief Register the platform-specific sleep implementation.
     * Called during platform init before RunMainLoop begins.
     */
    static void SetDelayFunction(std::function<void(uint32_t)> fn);

    /**
     * @brief Sleep for ms milliseconds using the registered delay function.
     * No-op if no function has been registered.
     */
    static void Delay(uint32_t ms);

    /**
     * @brief Set the platform-specific time provider.
     * Must be called before Initialize().
     * @param provider The time provider implementation (takes ownership)
     */
    static void SetTimeProvider(std::unique_ptr<ITimeProvider> provider);

private:
    static std::unique_ptr<ITimeProvider> s_TimeProvider;
    static uint32_t s_CurrentTime;    // Current time in milliseconds
    static uint32_t s_LastFrameTime;  // Time of last frame
    static uint32_t s_DeltaTime;      // Delta time between frames (integer)
    static float s_DeltaTimeF;        // Delta time between frames (float, high precision)
    static uint32_t s_StartTime;      // Time when system was initialized
    static float fps;                 // Current frames per second
    static uint32_t s_FpsCounter;     // Frame counter for FPS calculation
    static uint32_t s_FpsTime;        // Time accumulator for FPS calculation
};

}  // namespace Deki
