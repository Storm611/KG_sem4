#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include "Window.h"
#include <iostream>
#include <objbase.h>  // Для CoInitialize/CoUninitialize

int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM: " << hr << std::endl;
        return 1;
    }

    try {
        Window window(L"Laba4_Render", 1920, 1080);
        int exitCode = window.Run();
        std::cout << "\nApplication exited with code: " << exitCode << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        CoUninitialize();
        return 1;
    }

    CoUninitialize();
    return 0;
}