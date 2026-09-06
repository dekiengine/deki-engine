#pragma once

#include <cstdint>

/**
 * @brief Abstract interface for platform-specific time provider
 *
 * Platforms implement this to provide millisecond-resolution timing.
 * Implementations:
 * - SDL3: SDL_GetTicks()
 * - ESP32: esp_timer_get_time() / 1000
 */

namespace Deki
{

class ITimeProvider
{
public:
    virtual ~ITimeProvider() = default;

    /**
     * @brief Get current time in milliseconds
     * @return Monotonic time in milliseconds (platform-specific epoch)
     */
    virtual uint32_t GetTicksMs() const = 0;

    /**
     * @brief Platform-specific delay (sleep) for the given number of milliseconds
     *
     * Used for frame rate limiting. Default is a no-op (busy wait falls through).
     * Implementations:
     * - SDL3: SDL_Delay()
     * - ESP32: vTaskDelay(pdMS_TO_TICKS())
     */
    virtual void DelayMs(uint32_t ms) const {}
};

}  // namespace Deki
