#include "Timer.h"

Timer::Timer()
    : secondsPerCount(0.0), deltaTime(0.0),
    baseTime(0), pausedTime(0), stopTime(0),
    previousTime(0), currentTime(0),
    isStopped(false)
{
    __int64 countsPerSec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
    secondsPerCount = 1.0 / static_cast<double>(countsPerSec);
}

void Timer::Reset()
{
    __int64 now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);

    baseTime = now;
    previousTime = now;
    stopTime = 0;
    isStopped = false;
}

void Timer::Start()
{
    if (isStopped)
    {
        __int64 startTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

        pausedTime += (startTime - stopTime);
        previousTime = startTime;
        stopTime = 0;
        isStopped = false;
    }
}

void Timer::Stop()
{
    if (!isStopped)
    {
        __int64 currentTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

        stopTime = currentTime;
        isStopped = true;
    }
}

void Timer::Tick()
{
    if (isStopped)
    {
        deltaTime = 0.0;
        return;
    }

    QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
    deltaTime = static_cast<double>(currentTime - previousTime) * secondsPerCount;
    previousTime = currentTime;

    // Ограничение deltaTime (на случай паузы или отладки)
    if (deltaTime < 0.0)
        deltaTime = 0.0;
}

float Timer::DeltaTime() const
{
    return static_cast<float>(deltaTime);
}

float Timer::TotalTime() const
{
    if (isStopped)
    {
        return static_cast<float>(((stopTime - pausedTime) - baseTime) * secondsPerCount);
    }
    else
    {
        return static_cast<float>(((currentTime - pausedTime) - baseTime) * secondsPerCount);
    }
}

float Timer::FPS() const
{
    if (deltaTime > 0.0)
        return static_cast<float>(1.0 / deltaTime);
    return 0.0f;
}

bool Timer::IsRunning() const
{
    return !isStopped;
}