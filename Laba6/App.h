#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <stdexcept>
#include "d3dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static const UINT FRAME_COUNT   = 2;
static const UINT MAX_PARTICLES = 16000;

struct Particle {
    XMFLOAT3 Position;
    XMFLOAT3 Velocity;
    float     Life;
    float     Size;
    XMFLOAT4 Color;
};

struct ParticleEmitConstants {
    XMFLOAT3 EmitterPos;
    float     DeltaTime;
    XMFLOAT3 SphereCenter;
    float     SphereRadius;
    UINT      MaxParticles;
    UINT      EmitCount;
    float     pad0, pad1;
};

struct CameraConstants {
    XMMATRIX View;
    XMMATRIX Proj;
    XMMATRIX ViewProj;
    XMFLOAT3 CameraPos;
    float     pad;
};

struct SphereParams {
    XMFLOAT3 SphereCenter;
    float     SphereRadius;
};

class App {
public:
    App(UINT width, UINT height, HWND hwnd);
    ~App();

    void OnInit();
    void OnUpdate(float dt);
    void OnRender();
    void OnDestroy();

private:
    UINT   m_width, m_height;
    HWND   m_hwnd;
    float  m_aspectRatio;

    ComPtr<IDXGIFactory6>        m_factory;
    ComPtr<ID3D12Device>         m_device;
    ComPtr<ID3D12CommandQueue>   m_commandQueue;
    ComPtr<IDXGISwapChain3>      m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
    ComPtr<ID3D12Resource>       m_renderTargets[FRAME_COUNT];
    ComPtr<ID3D12Resource>       m_depthStencil;
    ComPtr<ID3D12CommandAllocator>    m_commandAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT   m_rtvDescSize;
    UINT   m_srvUavDescSize;

    ComPtr<ID3D12Fence> m_fence;
    UINT64  m_fenceValues[FRAME_COUNT];
    HANDLE  m_fenceEvent;
    UINT    m_frameIndex;

    // Compute
    ComPtr<ID3D12RootSignature> m_computeRootSig;
    ComPtr<ID3D12PipelineState> m_emitPSO;
    ComPtr<ID3D12PipelineState> m_updatePSO;

    // Particle render
    ComPtr<ID3D12RootSignature> m_renderRootSig;
    ComPtr<ID3D12PipelineState> m_renderPSO;

    // Sphere wireframe render
    ComPtr<ID3D12RootSignature> m_sphereRootSig;
    ComPtr<ID3D12PipelineState> m_spherePSO;
    ComPtr<ID3D12Resource>      m_sphereParamsCB;
    UINT8*                      m_pSphereParamsData = nullptr;

    // Particle buffers
    ComPtr<ID3D12Resource> m_particleBufferA;
    ComPtr<ID3D12Resource> m_particleBufferB;
    ComPtr<ID3D12Resource> m_particleCounterA;
    ComPtr<ID3D12Resource> m_particleCounterB;
    ComPtr<ID3D12Resource> m_counterReset;

    // Constant buffers
    ComPtr<ID3D12Resource> m_emitCB;
    ComPtr<ID3D12Resource> m_cameraCB;
    UINT8* m_pEmitCBData   = nullptr;
    UINT8* m_pCameraCBData = nullptr;

    enum HeapIdx {
        SRV_A      = 0,
        UAV_A      = 1,
        SRV_B      = 2,
        UAV_B      = 3,
        SRV_RENDER = 4,
        HEAP_SIZE  = 5
    };

    ComPtr<ID3D12Resource>       m_indirectArgsBuf;
    ComPtr<ID3D12Resource>       m_indirectArgsBufUpload;
    ComPtr<ID3D12CommandSignature> m_drawCommandSig;

    float     m_totalTime = 0.0f;
    bool      m_pingPong  = false;

    XMFLOAT3  m_cameraPos;
    float     m_camYaw, m_camPitch, m_camDist;

    XMFLOAT3  m_sphereCenter;
    float     m_sphereRadius;
    XMFLOAT3  m_emitterPos;

    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateDescriptorHeaps();
    void CreateDepthStencil();
    void CreateParticleBuffers();
    void CreateConstantBuffers();
    void CreateComputePipelines();
    void CreateRenderPipeline();
    void CreateSpherePipeline();
    void CreateIndirectArgs();

    void WaitForGpu();
    void MoveToNextFrame();

    ComPtr<ID3DBlob> CompileShader(const std::wstring& filename,
                                    const std::string&  entrypoint,
                                    const std::string&  target);

    D3D12_GPU_DESCRIPTOR_HANDLE SrvUavGpuHandle(UINT index);
    D3D12_CPU_DESCRIPTOR_HANDLE SrvUavCpuHandle(UINT index);
};
