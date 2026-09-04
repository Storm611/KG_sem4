#pragma once
#include "RenderingSystem.h"
#include <Windows.h>

class Application
{
public:
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateAppWindow(HINSTANCE hInstance, int nCmdShow);
    void ProcessInput(float deltaSeconds);

    HWND m_hwnd = nullptr;
    UINT m_width = 1280;
    UINT m_height = 720;

    RenderingSystem m_renderer;

    bool m_keys[256] = {};
    bool m_mouseDown = false;
    POINT m_lastMousePos = {};
    bool m_rendererReady = false;
};
