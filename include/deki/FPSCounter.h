#pragma once

#include <stdint.h>

// Measures the frame rate the engine actually reaches, with every package's
// work (rendering, input, ...) included.

namespace Deki
{

class FPSCounter
{
private:
    static constexpr int kSampleCount = 30;  // frames averaged

    uint32_t m_FrameTimes[kSampleCount];  // ring buffer of frame times
    uint32_t m_FrameTimeSum;
    int m_CurrentIndex;
    int m_SamplesCollected;
    uint32_t m_LastFrameTime;

    float m_CurrentFps;
    float m_MinFps;
    float m_MaxFps;
    uint32_t m_TotalFrames;

    bool m_Enabled;

public:
    FPSCounter();

    /// Call at the start of each frame.
    void BeginFrame();

    /// Call at the end of each frame.
    void EndFrame();

    /// Frames per second, averaged over the last kSampleCount frames.
    float GetFPS() const { return m_CurrentFps; }

    float GetMinFPS() const { return m_MinFps; }

    float GetMaxFPS() const { return m_MaxFps; }

    uint32_t GetTotalFrames() const { return m_TotalFrames; }

    /// Average frame time in milliseconds.
    float GetAverageFrameTime() const;

    /// Clears all statistics.
    void Reset();

    void SetEnabled(bool enable) { m_Enabled = enable; }

    bool IsEnabled() const { return m_Enabled; }

    /// Writes the statistics to the log.
    void PrintStats() const;
};

}  // namespace Deki
