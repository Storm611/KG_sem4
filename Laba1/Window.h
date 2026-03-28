// Window.h
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "camera.h"
#include "Timer.h"

class Camera;
class Timer;

class Window {
public:
    Window(const std::wstring& title, int width, int height);
    ~Window();
    int Run();

private:
    static Window* currentInstance;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RegisterWindowClass();
    HWND CreateNativeWindow();
    void InitializeDirectX();
    void CreateShaders();
    void CreateConstantBuffer();
    void CreatePSO();
    void LoadModel(const std::string& objFileName);
    void LoadMTL(const std::string& mtlFileName);
    HRESULT LoadTexture(const std::wstring& fileName, ID3D12Resource** textureResource, int& descriptorIndex);
    void UpdateMatrices();
    void RenderFrame();
    void RequestExit(int exitCode = 0);
    void WaitForPreviousFrame();
    void MoveToNextFrame();

    std::wstring windowTitle;
    int windowWidth, windowHeight;
    HWND hwnd;
    bool isExitRequested;
    int exitCode;

    // DX12 objects
    ID3D12Device* d3dDevice = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    ID3D12CommandAllocator* commandAllocators[2] = {};
    ID3D12GraphicsCommandList* commandList = nullptr;
    IDXGISwapChain3* swapChain = nullptr;
    ID3D12Resource* renderTargets[2] = {};
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    ID3D12Resource* depthStencilBuffer = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;

    ID3D12Resource* vertexBuffer = nullptr;
    ID3D12Resource* indexBuffer = nullptr;
    ID3D12Resource* constantBuffer = nullptr;
    void* constantBufferData = nullptr;
    UINT constantBufferSize;

    // Shaders
    ID3DBlob* vertexShaderBlob = nullptr;
    ID3DBlob* pixelShaderBlob = nullptr;

    // Resources
    std::vector<ID3D12Resource*> textures;
    std::vector<int> textureDescriptorIndices;

    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    UINT srvDescriptorSize;

    int frameIndex = 0;
    HANDLE fenceEvent = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValues[2] = {};

    DirectX::XMMATRIX viewMatrix;
    DirectX::XMMATRIX projectionMatrix;

    Camera camera;
    Timer timer;

    bool mouseCaptured = false;
    bool firstMouse = true;

    float mouseSensitivity = 0.02f;
    float cameraSpeed = 5.0f;

    // For model
    struct Vertex {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 tex;
    };

    struct ConstantBufferData {
        DirectX::XMMATRIX worldViewProjection;
        DirectX::XMMATRIX world;
        DirectX::XMFLOAT3 lightDir;
        float padding1;
        DirectX::XMFLOAT3 cameraPos;
        float time;
        DirectX::XMFLOAT2 tiling;
        DirectX::XMFLOAT2 animSpeed;
        DirectX::XMFLOAT3 diffuseColor;
        float specularIntensity;
    };

    ConstantBufferData cb;
        
    struct Material {
        std::string name;
        DirectX::XMFLOAT3 diffuse = { 1.0f, 1.0f, 1.0f };
        std::string textureName;
        ID3D12Resource* texture = nullptr;
        int textureDescriptorIndex = -1;
        float specularIntensity = 1.0f;
    };

    struct Submesh {
        int materialIndex;
        UINT startIndex;
        UINT indexCount;
    };

    std::vector<Material> materials;
    std::vector<Submesh> submeshes;
    std::vector<Vertex> vertices;
    std::vector<UINT> indices;
};