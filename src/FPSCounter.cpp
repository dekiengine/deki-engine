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

    uint32_t s_CurrentTime = Time::GetTime();
    uint32_t frame_time = s_CurrentTime - m_LastFrameTime;

    // Avoid division by zero
    if (frame_time == 0)
    {
        frame_time = 1;
    }

    // Store frame time in rolling buffer, keeping a running sum (the average
    // was recomputed by summing all samples twice per frame).
    m_FrameTimeSum -= m_FrameTimes[m_CurrentIndex];
    m_FrameTimes[m_CurrentIndex] = frame_time;
    m_FrameTimeSum += frame_time;
    m_CurrentIndex = (m_CurrentIndex + 1) % SAMPLE_COUNT;

    if (m_SamplesCollected < SAMPLE_COUNT)
    {
        m_SamplesCollected++;
    }

    m_TotalFrames++;

    float avg_frame_time = (float)m_FrameTimeSum / (float)m_SamplesCollected;
    m_CurrentFps = 1000.0f / avg_frame_time;

    // Track min/max FPS (after we have enough samples for stability)
    if (m_SamplesCollected >= SAMPLE_COUNT / 2)
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
