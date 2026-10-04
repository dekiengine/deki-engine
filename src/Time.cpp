#include <deki/Time.h>
#include <deki/LogSystem.h>
#include <deki/providers/ITimeProvider.h>

// Static member definitions

namespace Deki
{

static std::function<void(uint32_t)> s_DelayFn;
std::unique_ptr<ITimeProvider> Time::s_TimeProvider;
uint32_t Time::s_CurrentTime = 0;
uint32_t Time::s_LastFrameTime = 0;
uint32_t Time::s_DeltaTime = 0;
float Time::s_DeltaTimeF = 0.0f;
uint32_t Time::s_StartTime = 0;
float Time::s_Fps = 0.0f;
uint32_t Time::s_FpsCounter = 0;
uint32_t Time::s_FpsTime = 0;

void Time::SetTimeProvider(std::unique_ptr<ITimeProvider> provider)
{
    s_TimeProvider = std::move(provider);
}

void Time::Initialize()
{
    // Time provider must be set via SetTimeProvider() before Initialize()
    if (!s_TimeProvider)
    {
        DEKI_LOG_ERROR("DekiTime: No time provider available!");
        return;
    }

    s_StartTime = s_TimeProvider->GetTicksMs();
    s_CurrentTime = 0;
    s_LastFrameTime = 0;
    s_DeltaTime = 0;
    s_Fps = 0.0f;
    s_FpsCounter = 0;
    s_FpsTime = 0;

    DEKI_LOG_INTERNAL("DekiTime: Timing system initialized");
}

uint32_t Time::GetTime()
{
    if (!s_TimeProvider)
    {
        return 0;
    }
    return s_TimeProvider->GetTicksMs() - s_StartTime;
}

uint32_t Time::GetElapsed(uint32_t previousTime)
{
    uint32_t now = GetTime();
    if (now >= previousTime)
    {
        return now - previousTime;
    }
    // The millisecond clock wrapped (after about 49 days): report no time.
    return 0;
}

void Time::Update(uint32_t deltaTime)
{
    s_CurrentTime = GetTime();

    // Calculate delta time if not provided
    if (deltaTime == 0)
    {
        s_DeltaTime = s_CurrentTime - s_LastFrameTime;
        s_DeltaTimeF = static_cast<float>(s_DeltaTime);
    }
    else
    {
        s_DeltaTime = deltaTime;
        s_DeltaTimeF = static_cast<float>(deltaTime);
    }

    s_LastFrameTime = s_CurrentTime;

    // Calculate FPS (update once per second)
    s_FpsCounter++;
    s_FpsTime += s_DeltaTime;

    if (s_FpsTime >= 1000)  // One second has passed
    {
        s_Fps = (float)s_FpsCounter * 1000.0f / (float)s_FpsTime;
        s_FpsCounter = 0;
        s_FpsTime = 0;
    }
}

void Time::UpdateF(float deltaTimeMs)
{
    s_CurrentTime = GetTime();

    // Store both integer and float versions
    s_DeltaTime = static_cast<uint32_t>(deltaTimeMs + 0.5f);  // Round for integer version
    s_DeltaTimeF = deltaTimeMs;                               // Keep full precision

    s_LastFrameTime = s_CurrentTime;

    // Calculate FPS (update once per second)
    s_FpsCounter++;
    s_FpsTime += s_DeltaTime;

    if (s_FpsTime >= 1000)  // One second has passed
    {
        s_Fps = (float)s_FpsCounter * 1000.0f / (float)s_FpsTime;
        s_FpsCounter = 0;
        s_FpsTime = 0;
    }
}

uint32_t Time::GetDeltaTime()
{
    return s_DeltaTime;
}

float Time::GetDeltaTimeF()
{
    return s_DeltaTimeF;
}

float Time::GetFPS()
{
    return s_Fps;
}

void Time::ResetFrameTime()
{
    // The next Update() gets a delta of about 0. Call after loading, so the
    // load time does not arrive as one huge deltaTime.
    s_LastFrameTime = GetTime();
    s_DeltaTime = 0;
    s_DeltaTimeF = 0.0f;
}

void Time::SetDelayFunction(std::function<void(uint32_t)> fn)
{
    s_DelayFn = std::move(fn);
}

void Time::Delay(uint32_t ms)
{
    if (s_TimeProvider)
    {
        s_TimeProvider->DelayMs(ms);
    }
    else if (s_DelayFn)
    {
        s_DelayFn(ms);
    }
}

}  // namespace Deki
