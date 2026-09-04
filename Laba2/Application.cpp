#include "Application.h"
#include <chrono>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>

static Application* g_app = nullptr;

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_app) return g_app->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam < 256) m_keys[wParam] = true;
        if (wParam == 'R') // toggle rain on/off
            m_renderer.GetLightManager().SetRainEnabled(!m_renderer.GetLightManager().IsRainEnabled());
        if (wParam == 'C') // clear all grounded drops
            m_renderer.GetLightManager().ClearRain();
        if (wParam == 'T') // teleport camera above the scene, looking down, to instantly see the rain
        {
            auto& lm = m_renderer.GetLightManager();
            XMFLOAT2 center = lm.GetSceneCenterXZ();
            float y = lm.GetSpawnY() + 8.0f;
            m_renderer.GetCamera().SetPosition({ center.x, y, center.y });
            m_renderer.GetCamera().SetOrientation(0.0f, -(XM_PIDIV2 - 0.05f)); // look almost straight down
        }
        if (wParam == VK_ESCAPE)
            PostQuitMessage(0);
        return 0;

    case WM_KEYUP:
        if (wParam < 256) m_keys[wParam] = false;
        return 0;

    case WM_RBUTTONDOWN:
        m_mouseDown = true;
        m_lastMousePos.x = GET_X_LPARAM(lParam);
        m_lastMousePos.y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;

    case WM_RBUTTONUP:
        m_mouseDown = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (m_mouseDown)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            float dx = XMConvertToRadians(0.25f * (float)(x - m_lastMousePos.x));
            float dy = XMConvertToRadians(0.25f * (float)(y - m_lastMousePos.y));
            m_renderer.GetCamera().Rotate(dx, dy);
            m_lastMousePos.x = x;
            m_lastMousePos.y = y;
        }
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            m_width = LOWORD(lParam);
            m_height = HIWORD(lParam);
            if (m_rendererReady)
                m_renderer.Resize(m_width, m_height);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Application::CreateAppWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Application::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX12DeferredRenderingWndClass";
    RegisterClassEx(&wc);

    RECT rc = { 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindow(wc.lpszClassName, L"DX12 Deferred Rendering - Homework 2 (Sponza + Light Rain)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(m_hwnd, nCmdShow);
}

void Application::ProcessInput(float deltaSeconds)
{
    const float moveSpeed = 12.0f * deltaSeconds;
    auto& cam = m_renderer.GetCamera();

    if (m_keys['W']) cam.Walk(moveSpeed);
    if (m_keys['S']) cam.Walk(-moveSpeed);
    if (m_keys['A']) cam.Strafe(-moveSpeed);
    if (m_keys['D']) cam.Strafe(moveSpeed);
    if (m_keys['Q']) cam.Ascend(-moveSpeed);
    if (m_keys['E']) cam.Ascend(moveSpeed);
}

int Application::Run(HINSTANCE hInstance, int nCmdShow)
{
    g_app = this;
    CreateAppWindow(hInstance, nCmdShow);

    m_renderer.Init(m_hwnd, m_width, m_height);
    m_rendererReady = true;

    MSG msg = {};
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f); // clamp huge spikes (e.g. window drag)

        ProcessInput(dt);
        m_renderer.Update(dt);
        m_renderer.Render();
    }

    m_renderer.Shutdown();
    return (int)msg.wParam;
}
