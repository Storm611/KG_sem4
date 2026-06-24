#include "App.h"
#include <chrono>

static App* g_app = nullptr;
static std::chrono::steady_clock::time_point g_lastTime;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    const UINT W = 1280, H = 720;

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ParticleSystemDX12";
    RegisterClassEx(&wc);

    RECT wr = { 0, 0, (LONG)W, (LONG)H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindow(L"ParticleSystemDX12",
        L"Homework #6 — Particle System (DX12)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);

    try {
        App app(W, H, hwnd);
        g_app = &app;
        app.OnInit();
        g_lastTime = std::chrono::steady_clock::now();

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                auto now = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(now - g_lastTime).count();
                g_lastTime = now;
                dt = min(dt, 0.05f);  

                app.OnUpdate(dt);
                app.OnRender();
            }
        }
        app.OnDestroy();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Fatal Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    return 0;
}
