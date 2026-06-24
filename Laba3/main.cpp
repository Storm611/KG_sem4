#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <DirectXMath.h>
#include <string>
#include <stdexcept>
#include <cmath>
#include <chrono>

#include "Renderer.h"
#include "WaterRenderer.h"

using namespace DirectX;

static Renderer      g_Renderer;
static WaterRenderer g_Water;
static UINT  g_Width=1280, g_Height=720;
static bool  g_WaterInited = false;

// Orbit-камера
static float g_Yaw=0.f, g_Pitch=0.35f, g_Radius=8.f;
static bool  g_LMB=false;
static POINT g_LastMouse{};

// Таймер для анимации воды
static float GetTime() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now-start).count();
}

static XMFLOAT3 GetCamPos() {
    return { g_Radius*cosf(g_Pitch)*sinf(g_Yaw),
             g_Radius*sinf(g_Pitch),
             g_Radius*cosf(g_Pitch)*cosf(g_Yaw) };
}

template<typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_SIZE:
        if(wp!=SIZE_MINIMIZED && g_Width>0 && g_Renderer.IsInitialized()) {
            g_Width=LOWORD(lp); g_Height=HIWORD(lp);
            g_Renderer.Resize(g_Width,g_Height);
        }
        return 0;
    case WM_LBUTTONDOWN: g_LMB=true; g_LastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; SetCapture(hwnd); return 0;
    case WM_LBUTTONUP:   g_LMB=false; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE:
        if(g_LMB) {
            POINT c={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            g_Yaw  +=(c.x-g_LastMouse.x)*0.005f;
            g_Pitch = clamp(g_Pitch-(c.y-g_LastMouse.y)*0.005f,-1.4f,1.4f);
            g_LastMouse=c;
        }
        return 0;
    case WM_MOUSEWHEEL:
        g_Radius=clamp(g_Radius-GET_WHEEL_DELTA_WPARAM(wp)*0.005f,0.5f,80.f);
        return 0;
    case WM_KEYDOWN:
        if(wp=='E'||wp==VK_OEM_PLUS)  g_Renderer.mDisplacementScale=std::min(g_Renderer.mDisplacementScale+0.01f,1.f);
        if(wp=='Q'||wp==VK_OEM_MINUS) g_Renderer.mDisplacementScale=std::max(g_Renderer.mDisplacementScale-0.01f,0.f);
        // W/S — высота волн воды
        if(wp=='W') g_Water.WaveHeight=std::min(g_Water.WaveHeight+0.02f,1.0f);
        if(wp=='S') g_Water.WaveHeight=std::max(g_Water.WaveHeight-0.02f,0.0f);
        if(wp==VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WNDCLASSEX wc{sizeof(WNDCLASSEX)};
    wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.lpszClassName=L"DX12TessWindow"; RegisterClassEx(&wc);

    RECT rc={0,0,(LONG)g_Width,(LONG)g_Height};
    AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowEx(0,L"DX12TessWindow",
        L"DX12 Tessellation  |  Q/E: displacement  |  W/S: wave height",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        rc.right-rc.left,rc.bottom-rc.top,nullptr,nullptr,hInst,nullptr);
    ShowWindow(hwnd,nCmdShow);

    // ---- Инициализация основного рендерера ----
    try {
        g_Renderer.Init(hwnd,g_Width,g_Height,
            "assets/mesh.obj",
            L"assets/displacement.png",
            L"assets/normal.png",
            L"assets/albedo.png");

        g_Renderer.mDisplacementScale = 0.15f;
        g_Renderer.mTessMax           = 32.0f;
        g_Renderer.mTessNearDist      = 1.0f;
        g_Renderer.mTessFarDist       = 20.0f;
    }
    catch(const std::exception& e) {
        MessageBoxA(nullptr,e.what(),"Init Error",MB_OK|MB_ICONERROR);
        return -1;
    }

    // ---- Инициализация воды (в том же command list что и меш) ----
    // Открываем новый command list специально для загрузки сетки воды
    try {
        // Используем уже открытый после Init command list
        // Воду инициализируем в отдельном проходе
        ID3D12CommandAllocator* alloc = nullptr;
        g_Renderer.GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&alloc));

        ComPtr<ID3D12GraphicsCommandList> initCL;
        g_Renderer.GetDevice()->CreateCommandList(0,
            D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr,
            IID_PPV_ARGS(&initCL));

        g_Water.Init(g_Renderer.GetDevice(), initCL.Get(),
                     g_Renderer.GetRTVFormat(), g_Renderer.GetDSVFormat());

        initCL->Close();
        ID3D12CommandList* cls[]={initCL.Get()};
        g_Renderer.GetCmdQueue()->ExecuteCommandLists(1,cls);

        // Ждём завершения загрузки сетки воды
        auto& fv = g_Renderer.GetFenceValue();
        fv++;
        g_Renderer.GetCmdQueue()->Signal(g_Renderer.GetFence(), fv);
        if(g_Renderer.GetFence()->GetCompletedValue()<fv) {
            HANDLE ev=CreateEventEx(nullptr,nullptr,0,EVENT_ALL_ACCESS);
            g_Renderer.GetFence()->SetEventOnCompletion(fv,ev);
            WaitForSingleObject(ev,INFINITE); CloseHandle(ev);
        }
        alloc->Release();
        g_WaterInited=true;
    }
    catch(const std::exception& e) {
        MessageBoxA(nullptr,e.what(),"Water Init Error",MB_OK|MB_ICONWARNING);
        // Продолжаем без воды
    }

    // ---- Render loop ----
    MSG msg{};
    while(msg.message!=WM_QUIT) {
        if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        } else {
            float time = GetTime();
            XMFLOAT3 camPos = GetCamPos();
            XMVECTOR eye = XMLoadFloat3(&camPos);
            XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorZero(), XMVectorSet(0,1,0,0));
            XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f),
                (float)g_Width/g_Height, 0.1f, 1000.f);

            // Основная модель — чуть поднята над водой
            XMMATRIX meshWorld = XMMatrixTranslation(0.f, 1.5f, 0.f);

            g_Renderer.Update(meshWorld, view, proj, camPos);

            // --- Рендер кадра ---
            g_Renderer.BeginFrame();

            // 1. Рисуем основную модель
            g_Renderer.DrawMesh();

            // 2. Рисуем воду (в том же command list, после модели)
            if(g_WaterInited) {
                UINT fi = g_Renderer.GetFrameIdx();
                g_Water.Draw(
                    g_Renderer.GetCmdList(),
                    g_Renderer.GetCBCamera(fi),
                    g_Renderer.GetCBLight(fi),
                    fi, time);
            }

            g_Renderer.EndFrame();
        }
    }

    CoUninitialize();
    return 0;
}
