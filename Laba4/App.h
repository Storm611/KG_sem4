#pragma once
#include "DX12Common.h"
#include "Camera.h"
#include "Scene.h"
#include "OctTree.h"
#include "FrustumCuller.h"
#include <string>

class App
{
public:
    App(HINSTANCE hInstance, int nCmdShow);
    ~App();
    int Run();

private:
    
    HINSTANCE m_hInstance;
    HWND      m_hWnd;
    int       m_width  = 1280;
    int       m_height = 720;

    
    static const UINT FRAME_COUNT = 2;

    ComPtr<IDXGIFactory4>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_cmdQueue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap>      m_dsvHeap;
    ComPtr<ID3D12Resource>            m_renderTargets[FRAME_COUNT];
    ComPtr<ID3D12Resource>            m_depthStencil;
    ComPtr<ID3D12CommandAllocator>    m_cmdAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence>               m_fence;
    UINT64                            m_fenceValues[FRAME_COUNT] = {};
    HANDLE                            m_fenceEvent   = nullptr;
    UINT                              m_frameIndex   = 0;
    UINT                              m_rtvDescSize  = 0;

   
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_meshPSO;    
    ComPtr<ID3D12PipelineState> m_boxPSO;      
    ComPtr<ID3D12PipelineState> m_wireBoxPSO;   

 
    std::unique_ptr<Camera>        m_camera;
    std::unique_ptr<Scene>         m_scene;
    std::unique_ptr<OctTree>       m_octTree;
    std::unique_ptr<FrustumCuller> m_culler;

    bool  m_frustumCullingEnabled = true;
    bool  m_octTreeEnabled        = true;
    bool  m_showOctTree           = false;
    float m_lodDistanceSq         = 300.f * 300.f;

   
    struct FrameCB
    {
        XMFLOAT4X4 viewProj;
        XMFLOAT4   cameraPos;
        XMFLOAT4   lightDir;
        float      lodDistanceSq;
        float      pad[3];
    };
    ComPtr<ID3D12Resource> m_cbFrame[FRAME_COUNT];
    FrameCB*               m_cbFramePtr[FRAME_COUNT] = {};

    static const UINT MAX_OBJECTS = 512;
    ComPtr<ID3D12Resource> m_cbObject; 
    ObjectConstants*       m_cbObjectPtr = nullptr;


    CullResult m_cullResult;

 
    int   m_totalObjects   = 0;
    int   m_visibleObjects = 0;
    DWORD m_lastFPSTime    = 0;
    int   m_frameCount     = 0;
    float m_fps            = 0.f;

 
    void InitWindow();
    void InitDX12();
    void CreateRootSignatureAndPSOs();
    void CreateDepthBuffer();
    void BuildScene();

    void Update(float dt);
    void Render();
    void WaitForGPU();
    void MoveToNextFrame();


    void PerformCull();

    void HandleKeyboard(float dt);
    POINT m_lastMouse = {};

    void OnResize(int w, int h);
    void UpdateWindowTitle();

    ComPtr<ID3DBlob> CompileShaderFromString(const std::string& src,
        const std::string& entry, const std::string& target);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
