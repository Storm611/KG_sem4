#pragma once
#include "DXHelpers.h"
#include "GBuffer.h"
#include "Mesh.h"
#include "Camera.h"
#include "LightManager.h"
#include "ConstantBuffers.h"
#include "UploadBuffer.h"
#include <memory>


class RenderingSystem
{
public:
    static const UINT kFrameCount = 3;
    static const UINT kMaxLights = 512;

    void Init(HWND hwnd, UINT width, UINT height);
    void Resize(UINT width, UINT height);
    void Update(float deltaSeconds);
    void Render();
    void Shutdown();

    Camera& GetCamera() { return m_camera; }
    LightManager& GetLightManager() { return *m_lightManager; }

private:

    void CreateDeviceAndQueue();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateCommandObjects();
    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateConstantBuffers();
    void LoadScene();
    void CreateBackBufferRTVs();
    void CreateGBufferSRVs();


    void RenderGBufferPass();
    void RenderLightingPass();
    void RenderLightMarkersPass();
    void MoveToNextFrame();
    void WaitForGPU();


    HWND m_hwnd = nullptr;
    UINT m_width = 1280;
    UINT m_height = 720;

    ComPtr<ID3D12Device> m_device;
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    ComPtr<ID3D12Resource> m_backBuffers[kFrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12DescriptorHeap> m_backBufferRTVHeap;
    UINT m_rtvDescriptorSize = 0;


    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_srvDescriptorSize = 0;

    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;

    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[kFrameCount] = {};
    HANDLE m_fenceEvent = nullptr;
    UINT m_frameIndex = 0;


    ComPtr<ID3D12RootSignature> m_gbufferRootSig;
    ComPtr<ID3D12RootSignature> m_deferredRootSig;
    ComPtr<ID3D12RootSignature> m_markerRootSig;
    ComPtr<ID3D12PipelineState> m_gbufferPSO;
    ComPtr<ID3D12PipelineState> m_deferredPSO;
    ComPtr<ID3D12PipelineState> m_markerPSO;

    GBuffer m_gbuffer;

    Mesh m_sceneMesh;
    XMFLOAT4X4 m_sceneWorld;
    Camera m_camera;
    std::unique_ptr<LightManager> m_lightManager;


    std::unique_ptr<UploadBuffer<ObjectConstants>> m_objectCB;
    std::unique_ptr<UploadBuffer<MaterialConstants>> m_materialCB;
    std::unique_ptr<UploadBuffer<FrameConstants>> m_frameCB[kFrameCount];
    std::unique_ptr<UploadBuffer<MarkerConstants>> m_markerCB[kFrameCount];
    std::unique_ptr<UploadBuffer<GPULight>> m_lightBuffer[kFrameCount];

    UINT m_currentLightCount = 0;


};
