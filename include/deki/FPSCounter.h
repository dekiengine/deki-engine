#pragma once

#include <stdint.h>

/**
 * @brief FPS counter for tracking and displaying frame rate
 *
 * This utility measures actual FPS achieved by the engine including
 * all packages (rendering, input processing, etc.)
 */

namespace Deki
{

class FPSCounter
{
private:
    static constexpr int kSampleCount = 30;  // Number of frames to average

    uint32_t m_FrameTimes[kSampleCount];  // Rolling buffer of frame times
    uint32_t m_FrameTimeSum;              // Sum of the samples in the buffer
    int m_CurrentIndex;                   // Current position in buffer
    int m_SamplesCollected;               // How many samples we have
    uint32_t m_LastFrameTime;             // Time of last frame

    float m_CurrentFps;      // Calculated FPS
    float m_MinFps;          // Minimum FPS seen
    float m_MaxFps;          // Maximum FPS seen
    uint32_t m_TotalFrames;  // Total frames counted

    bool m_Enabled;  // Is FPS counter active

public:
    FPSCounter();

    /**
     * @brief Call this at the start of each frame
     */
    void BeginFrame();

    /**
     * @brief Call this at the end of each frame
     */
    void EndFrame();

    /**
     * @brief Get the current FPS
     */
    float GetFPS() const { return m_CurrentFps; }

    /**
     * @brief Get the minimum FPS recorded
     */
    float GetMinFPS() const { return m_MinFps; }

    /**
     * @brief Get the maximum FPS recorded
     */
    float GetMaxFPS() const { return m_MaxFps; }

    /**
     * @brief Get total frames counted
     */
    uint32_t GetTotalFrames() const { return m_TotalFrames; }

    /**
     * @brief Get the average frame time in milliseconds
     */
    float GetAverageFrameTime() const;

    /**
     * @brief Reset FPS statistics
     */
    void Reset();

    /**
     * @brief Enable or disable FPS counting
     */
    void SetEnabled(bool enable) { m_Enabled = enable; }

    /**
     * @brief Check if FPS counter is enabled
     */
    bool IsEnabled() const { return m_Enabled; }

    /**
     * @brief Print FPS stats to log
     */
    void PrintStats() const;
};

}  // namespace Deki
