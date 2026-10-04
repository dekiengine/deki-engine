#include <deki/FPSCounter.h>
#include <deki/Time.h>
#include <deki/LogSystem.h>
#include <cstring>
#include <cmath>

namespace Deki
{

FPSCounter::FPSCounter()
    : m_FrameTimeSum(0),
      m_CurrentIndex(0),
      m_SamplesCollected(0),
      m_LastFrameTime(0),
      m_CurrentFps(0.0f),
      m_MinFps(999999.0f),
      m_MaxFps(0.0f),
      m_TotalFrames(0),
      m_Enabled(true)
{
    memset(m_FrameTimes, 0, sizeof(m_FrameTimes));
}

void FPSCounter::BeginFrame()
{
    if (!m_Enabled)
    {
        return;
    }

    m_LastFrameTime = Time::GetTime();
}

void FPSCounter::EndFrame()
{
    if (!m_Enabled)
    {
        return;
    }

    uint32_t currentTime = Time::GetTime();
    uint32_t frameTime = currentTime - m_LastFrameTime;

    // Avoid division by zero
    if (frameTime == 0)
    {
        frameTime = 1;
    }

    // Rolling buffer of frame times with a running sum, so the average needs
    // no pass over the samples.
    m_FrameTimeSum -= m_FrameTimes[m_CurrentIndex];
    m_FrameTimes[m_CurrentIndex] = frameTime;
    m_FrameTimeSum += frameTime;
    m_CurrentIndex = (m_CurrentIndex + 1) % kSampleCount;

    if (m_SamplesCollected < kSampleCount)
    {
        m_SamplesCollected++;
    }

    m_TotalFrames++;

    float avgFrameTime = (float)m_FrameTimeSum / (float)m_SamplesCollected;
    m_CurrentFps = 1000.0f / avgFrameTime;

    // Track min/max FPS (after we have enough samples for stability)
    if (m_SamplesCollected >= kSampleCount / 2)
    {
        if (m_CurrentFps < m_MinFps)
        {
            m_MinFps = m_CurrentFps;
        }
        if (m_CurrentFps > m_MaxFps)
        {
            m_MaxFps = m_CurrentFps;
        }
    }
}

float FPSCounter::GetAverageFrameTime() const
{
    if (m_SamplesCollected == 0)
    {
        return 0.0f;
    }
    return (float)m_FrameTimeSum / (float)m_SamplesCollected;
}

void FPSCounter::Reset()
{
    m_CurrentIndex = 0;
    m_SamplesCollected = 0;
    m_LastFrameTime = 0;
    m_CurrentFps = 0.0f;
    m_MinFps = 999999.0f;
    m_MaxFps = 0.0f;
    m_TotalFrames = 0;
    m_FrameTimeSum = 0;
    memset(m_FrameTimes, 0, sizeof(m_FrameTimes));
}

void FPSCounter::PrintStats() const
{
    if (!m_Enabled)
    {
        return;
    }
}

}  // namespace Deki
