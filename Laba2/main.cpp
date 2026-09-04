#include "Application.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    try
    {
        Application app;
        return app.Run(hInstance, nCmdShow);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "DX12 Deferred Rendering - Fatal Error", MB_OK | MB_ICONERROR);
        return -1;
    }
}
