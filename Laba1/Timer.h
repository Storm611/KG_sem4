#pragma once
#include <Windows.h>

class Timer
{
public:
    Timer();

    void Reset();               // Сброс таймера
    void Tick();                // Вызывать каждый кадр
    void Start();               // Запуск
    void Stop();                // Пауза

    float DeltaTime() const;    // Время между кадрами (в секундах)
    float TotalTime() const;    // Общее время с запуска (без пауз)
    float FPS() const;          // Текущий FPS

    bool IsRunning() const;

private:
    double secondsPerCount;
    double deltaTime;

    __int64 baseTime;
    __int64 pausedTime;
    __int64 stopTime;
    __int64 previousTime;
    __int64 currentTime;

    bool isStopped;
};