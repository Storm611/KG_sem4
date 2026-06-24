#include "App.h"
#include "Shaders.h"
#include <sstream>
#include <iomanip>
#include <functional>

static App* g_App = nullptr;


static ComPtr<ID3D12Resource> UploadAndCreateBuffer(
    ID3D12Device*               device,
    ID3D12GraphicsCommandList*  cmdList,
    const void*                 data,
    UINT64                      byteSize,
    ComPtr<ID3D12Resource>&     uploadBuf)
{
    
    ComPtr<ID3D12Resource> gpuBuf;
    {
        auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto dsc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &dsc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&gpuBuf)));
    }
    
    {
        auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto dsc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &dsc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuf)));
    }
    
    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData      = data;
    sub.RowPitch   = (LONG_PTR)byteSize;
    sub.SlicePitch = sub.RowPitch;
    UpdateSubresources(cmdList, gpuBuf.Get(), uploadBuf.Get(), 0, 0, 1, &sub);

   
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        gpuBuf.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &barrier);

    return gpuBuf;
}


App::App(HINSTANCE hInstance, int nCmdShow)
    : m_hInstance(hInstance)
{
    g_App = this;
    InitWindow();
    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
    InitDX12();
    CreateRootSignatureAndPSOs();
    BuildScene();
    m_lastFPSTime = GetTickCount();
    GetCursorPos(&m_lastMouse);
}

App::~App()
{
    WaitForGPU();
    for (UINT i = 0; i < FRAME_COUNT; i++)
        if (m_cbFramePtr[i])
            m_cbFrame[i]->Unmap(0, nullptr);
    if (m_cbObjectPtr)
        m_cbObject->Unmap(0, nullptr);
    if (m_fenceEvent)
        CloseHandle(m_fenceEvent);
}


void App::InitWindow()
{
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX12CullingDemo";
    RegisterClassEx(&wc);

    RECT rc = { 0, 0, m_width, m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    m_hWnd = CreateWindow(L"DX12CullingDemo",
        L"DX12 Frustum Culling + Octree + LOD",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, m_hInstance, nullptr);
}

LRESULT CALLBACK App::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_App)
    {
        switch (msg)
        {
        case WM_SIZE:
            g_App->OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


void App::InitDX12()
{
    
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
            dbg->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)));

    
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
            D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
    }
    if (!m_device) 
    {
        ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        ThrowIfFailed(D3D12CreateDevice(warp.Get(),
            D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    
    {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        ThrowIfFailed(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_cmdQueue)));
    }

    
    {
        DXGI_SWAP_CHAIN_DESC1 scd = {};
        scd.BufferCount      = FRAME_COUNT;
        scd.Width            = m_width;
        scd.Height           = m_height;
        scd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;
        ComPtr<IDXGISwapChain1> sc1;
        ThrowIfFailed(m_factory->CreateSwapChainForHwnd(
            m_cmdQueue.Get(), m_hWnd, &scd, nullptr, nullptr, &sc1));
        ThrowIfFailed(sc1.As(&m_swapChain));
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = FRAME_COUNT;
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)));
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 1;
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_dsvHeap)));
    }

    
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < FRAME_COUNT; i++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
            h.Offset(1, m_rtvDescSize);
        }
    }

    CreateDepthBuffer();

    
    for (UINT i = 0; i < FRAME_COUNT; i++)
        ThrowIfFailed(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocators[i])));

    
    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cmdAllocators[0].Get(), nullptr,
        IID_PPV_ARGS(&m_cmdList)));
    

    
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    
    for (UINT i = 0; i < FRAME_COUNT; i++)
        m_fenceValues[i] = 1;

    
    {
        UINT64 sz = (sizeof(FrameCB) + 255) & ~255ULL;
        for (UINT i = 0; i < FRAME_COUNT; i++)
        {
            auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto dsc = CD3DX12_RESOURCE_DESC::Buffer(sz);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &dsc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&m_cbFrame[i])));
            m_cbFrame[i]->Map(0, nullptr, reinterpret_cast<void**>(&m_cbFramePtr[i]));
        }
    }

    
    {
        UINT64 slotSz = (sizeof(ObjectConstants) + 255) & ~255ULL;
        UINT64 total  = slotSz * MAX_OBJECTS;
        auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto dsc = CD3DX12_RESOURCE_DESC::Buffer(total);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &dsc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_cbObject)));
        m_cbObject->Map(0, nullptr, reinterpret_cast<void**>(&m_cbObjectPtr));
    }

   
    m_camera = std::make_unique<Camera>(XM_PIDIV4, (float)m_width / (float)m_height, 0.1f, 500.f);
    m_culler  = std::make_unique<FrustumCuller>();
}

void App::CreateDepthBuffer()
{
    auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto dsc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, m_width, m_height,
        1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE cv = {};
    cv.Format             = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.f;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &dsc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
        IID_PPV_ARGS(&m_depthStencil)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dvd = {};
    dvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(
        m_depthStencil.Get(), &dvd,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}


ComPtr<ID3DBlob> App::CompileShaderFromString(
    const std::string& src, const std::string& entry, const std::string& target)
{
    ComPtr<ID3DBlob> code, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    HRESULT hr = D3DCompile(src.c_str(), src.size(), nullptr, nullptr, nullptr,
        entry.c_str(), target.c_str(), flags, 0, &code, &errors);
    if (FAILED(hr))
    {
        std::string msg = errors
            ? std::string((char*)errors->GetBufferPointer(), errors->GetBufferSize())
            : "Unknown shader error";
        MessageBoxA(nullptr, msg.c_str(), "Shader compile error", MB_OK | MB_ICONERROR);
        ThrowIfFailed(hr);
    }
    return code;
}

void App::CreateRootSignatureAndPSOs()
{
 
    CD3DX12_ROOT_PARAMETER rp[2];
    rp[0].InitAsConstantBufferView(0);
    rp[1].InitAsConstantBufferView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rsd(2, rp, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> sigBlob, sigErr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr));
    ThrowIfFailed(m_device->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)));


    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vs     = CompileShaderFromString(g_ShaderSrc,     "VS", "vs_5_0");
    auto ps     = CompileShaderFromString(g_ShaderSrc,     "PS", "ps_5_0");
    auto vsWire = CompileShaderFromString(g_WireShaderSrc, "VS", "vs_5_0");
    auto psWire = CompileShaderFromString(g_WireShaderSrc, "PS", "ps_5_0");


    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature        = m_rootSignature.Get();
    pd.InputLayout           = { layout, _countof(layout) };
    pd.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pd.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count      = 1;
    pd.SampleMask            = UINT_MAX;
    pd.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pd.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pd.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_meshPSO)));

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_boxPSO)));


    pd.VS = { vsWire->GetBufferPointer(), vsWire->GetBufferSize() };
    pd.PS = { psWire->GetBufferPointer(), psWire->GetBufferSize() };
    pd.RasterizerState.FillMode         = D3D12_FILL_MODE_WIREFRAME;
    pd.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_wireBoxPSO)));
}


void App::BuildScene()
{
    m_scene   = std::make_unique<Scene>();
    m_octTree = std::make_unique<OctTree>();


    m_scene->Build(m_device.Get(), m_cmdList.Get(),
                   "assets/model.obj", 10, 500.f);


    ThrowIfFailed(m_cmdList->Close());
    {
        ID3D12CommandList* lists[] = { m_cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists(1, lists);
    }
    WaitForGPU();


    ThrowIfFailed(m_cmdAllocators[0]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[0].Get(), nullptr));
    ThrowIfFailed(m_cmdList->Close());

    m_totalObjects = (int)m_scene->GetObjects().size();
    m_octTree->Build(m_scene->GetObjects(), 60.f, 5, 8);
}


int App::Run()
{
    DWORD prev = GetTickCount();
    MSG   msg  = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            DWORD now = GetTickCount();
            float dt  = std::max(0.0001f, (now - prev) * 0.001f);
            prev = now;
            HandleKeyboard(dt);
            Update(dt);
            Render();
        }
    }
    WaitForGPU();
    return (int)msg.wParam;
}


void App::HandleKeyboard(float dt)
{
    float spd = 15.f * dt;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) spd *= 3.f;

    if (GetAsyncKeyState('W') & 0x8000) m_camera->MoveForward( spd);
    if (GetAsyncKeyState('S') & 0x8000) m_camera->MoveForward(-spd);
    if (GetAsyncKeyState('A') & 0x8000) m_camera->MoveRight  (-spd);
    if (GetAsyncKeyState('D') & 0x8000) m_camera->MoveRight  ( spd);
    if (GetAsyncKeyState('Q') & 0x8000) m_camera->MoveUp     (-spd);
    if (GetAsyncKeyState('E') & 0x8000) m_camera->MoveUp     ( spd);


    if (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000)
        m_lodDistanceSq = std::min(m_lodDistanceSq + 500.f * dt, 200.f * 200.f);
    if (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000)
        m_lodDistanceSq = std::max(m_lodDistanceSq - 500.f * dt, 4.f);

    static bool p1 = false, p2 = false, p3 = false;
    bool c1 = (GetAsyncKeyState('1') & 0x8000) != 0;
    bool c2 = (GetAsyncKeyState('2') & 0x8000) != 0;
    bool c3 = (GetAsyncKeyState('3') & 0x8000) != 0;
    if (c1 && !p1) m_frustumCullingEnabled = !m_frustumCullingEnabled;
    if (c2 && !p2) m_octTreeEnabled        = !m_octTreeEnabled;
    if (c3 && !p3) m_showOctTree           = !m_showOctTree;
    p1 = c1; p2 = c2; p3 = c3;
}


void App::PerformCull()
{
    const auto& objs = m_scene->GetObjects();
    m_cullResult.visibleFull.clear();
    m_cullResult.visibleBox .clear();

    BoundingFrustum frustum = m_camera->GetFrustum();
    XMFLOAT3 camPosF = m_camera->GetPosition();
    XMVECTOR camPos  = XMLoadFloat3(&camPosF);

    if (m_frustumCullingEnabled && m_octTreeEnabled)
    {
        std::vector<int> candidates;
        candidates.reserve(objs.size());
        m_octTree->Query(frustum, candidates);

        
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        for (int i : candidates)
        {
            if (frustum.Contains(objs[i].aabb) == DISJOINT) continue;
            XMVECTOR vo  = XMLoadFloat3(&objs[i].position);
            float    dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vo, camPos)));
            (dsq > m_lodDistanceSq ? m_cullResult.visibleBox : m_cullResult.visibleFull).push_back(i);
        }
    }
    else
    {
        for (int i = 0; i < (int)objs.size(); ++i)
        {
            if (m_frustumCullingEnabled && frustum.Contains(objs[i].aabb) == DISJOINT) continue;
            XMVECTOR vo  = XMLoadFloat3(&objs[i].position);
            float    dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vo, camPos)));
            (dsq > m_lodDistanceSq ? m_cullResult.visibleBox : m_cullResult.visibleFull).push_back(i);
        }
    }

    m_visibleObjects = (int)(m_cullResult.visibleFull.size() + m_cullResult.visibleBox.size());
}


void App::Update(float dt)
{
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
    {
        POINT cur;
        GetCursorPos(&cur);
        float dx = (float)(cur.x - m_lastMouse.x) * 0.003f;
        float dy = (float)(cur.y - m_lastMouse.y) * 0.003f;
        m_camera->Rotate(dx, -dy);
        m_lastMouse = cur;
    }
    else
    {
        GetCursorPos(&m_lastMouse);
    }


    ++m_frameCount;
    DWORD now = GetTickCount();
    if (now - m_lastFPSTime >= 500)
    {
        m_fps         = m_frameCount * 1000.f / (float)(now - m_lastFPSTime);
        m_frameCount  = 0;
        m_lastFPSTime = now;
        UpdateWindowTitle();
    }

    PerformCull();
}


void App::Render()
{
   
    ThrowIfFailed(m_cmdAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[m_frameIndex].Get(), nullptr));

    
    {
        FrameCB fc = {};
        XMStoreFloat4x4(&fc.viewProj, XMMatrixTranspose(m_camera->GetViewProj()));
        const XMFLOAT3& cp = m_camera->GetPosition();
        fc.cameraPos     = { cp.x, cp.y, cp.z, 1.f };
        fc.lightDir      = { 0.577f, -0.577f, 0.577f, 0.f };
        fc.lodDistanceSq = m_lodDistanceSq;
        *m_cbFramePtr[m_frameIndex] = fc;
    }

    
    const UINT64 slotSz = (sizeof(ObjectConstants) + 255) & ~255ULL;
    const auto&  objs   = m_scene->GetObjects();

    auto writeObjCB = [&](int idx, bool asBox)
    {
        ObjectConstants oc = {};
        if (asBox)
        {
            const XMFLOAT3& ext = objs[idx].aabb.Extents;
            const XMFLOAT3& cen = objs[idx].aabb.Center;
            XMMATRIX w = XMMatrixScaling(ext.x * 2.f, ext.y * 2.f, ext.z * 2.f)
                       * XMMatrixTranslation(cen.x, cen.y, cen.z);
            XMStoreFloat4x4(&oc.world, XMMatrixTranspose(w));
        }
        else
        {
            XMStoreFloat4x4(&oc.world, XMMatrixTranspose(objs[idx].GetWorld()));
        }
        oc.color = objs[idx].color;
        oc.isBox = asBox ? 1 : 0;

        BYTE* dst = reinterpret_cast<BYTE*>(m_cbObjectPtr) + slotSz * idx;
        memcpy(dst, &oc, sizeof(ObjectConstants));
    };

    for (int i : m_cullResult.visibleFull) writeObjCB(i, false);
    for (int i : m_cullResult.visibleBox)  writeObjCB(i, true);

   
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmdList->ResourceBarrier(1, &barrier);
    }

   
    auto rtvH = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescSize);
    auto dsvH = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    const float bg[] = { 0.05f, 0.05f, 0.12f, 1.f };
    m_cmdList->ClearRenderTargetView(rtvH, bg, 0, nullptr);
    m_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    m_cmdList->OMSetRenderTargets(1, &rtvH, FALSE, &dsvH);

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)m_width, (float)m_height, 0.f, 1.f };
    D3D12_RECT     sr = { 0, 0, m_width, m_height };
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &sr);

   
    m_cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_cmdList->SetGraphicsRootConstantBufferView(0,
        m_cbFrame[m_frameIndex]->GetGPUVirtualAddress());
    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const MeshData& mesh = m_scene->GetMesh();
    const BoxMesh&  box  = m_scene->GetBoxMesh();

   
    auto drawObj = [&](int idx, bool asBox)
    {
        UINT64 offset = slotSz * (UINT64)idx;
        m_cmdList->SetGraphicsRootConstantBufferView(1,
            m_cbObject->GetGPUVirtualAddress() + offset);

        if (asBox)
        {
            m_cmdList->IASetVertexBuffers(0, 1, &box.vbv);
            m_cmdList->IASetIndexBuffer(&box.ibv);
            m_cmdList->DrawIndexedInstanced(box.indexCount, 1, 0, 0, 0);
        }
        else
        {
            m_cmdList->IASetVertexBuffers(0, 1, &mesh.vbv);
            m_cmdList->IASetIndexBuffer(&mesh.ibv);
            m_cmdList->DrawIndexedInstanced((UINT)mesh.indices.size(), 1, 0, 0, 0);
        }
    };

    
    m_cmdList->SetPipelineState(m_meshPSO.Get());
    for (int i : m_cullResult.visibleFull) drawObj(i, false);

   
    m_cmdList->SetPipelineState(m_boxPSO.Get());
    for (int i : m_cullResult.visibleBox)  drawObj(i, true);

    
    if (m_showOctTree)
    {
        m_cmdList->SetPipelineState(m_wireBoxPSO.Get());
        
        const UINT64 octOffset = slotSz * (MAX_OBJECTS - 1);
        BYTE* octDst = reinterpret_cast<BYTE*>(m_cbObjectPtr) + octOffset;

        std::function<void(const OctNode*)> drawNode = [&](const OctNode* node)
        {
            if (!node) return;
            ObjectConstants oc = {};
            const XMFLOAT3& ext = node->bounds.Extents;
            const XMFLOAT3& cen = node->bounds.Center;
            XMMATRIX w = XMMatrixScaling(ext.x * 2.f, ext.y * 2.f, ext.z * 2.f)
                       * XMMatrixTranslation(cen.x, cen.y, cen.z);
            XMStoreFloat4x4(&oc.world, XMMatrixTranspose(w));
            oc.color = { 0.1f, 1.f, 0.4f, 1.f };
            oc.isBox = 0;
            memcpy(octDst, &oc, sizeof(ObjectConstants));

            m_cmdList->SetGraphicsRootConstantBufferView(1,
                m_cbObject->GetGPUVirtualAddress() + octOffset);
            m_cmdList->IASetVertexBuffers(0, 1, &box.vbv);
            m_cmdList->IASetIndexBuffer(&box.ibv);
            m_cmdList->DrawIndexedInstanced(box.indexCount, 1, 0, 0, 0);

            if (!node->IsLeaf())
                for (auto& ch : node->children) drawNode(ch.get());
        };
        drawNode(m_octTree->GetRoot());
    }

    
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        m_cmdList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(m_cmdList->Close());
    {
        ID3D12CommandList* lists[] = { m_cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists(1, lists);
    }

    ThrowIfFailed(m_swapChain->Present(1, 0)); 
    MoveToNextFrame();
}


void App::WaitForGPU()
{
    const UINT64 val = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_cmdQueue->Signal(m_fence.Get(), val));
    ThrowIfFailed(m_fence->SetEventOnCompletion(val, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValues[m_frameIndex]++;
}

void App::MoveToNextFrame()
{
    
    const UINT64 signalVal = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_cmdQueue->Signal(m_fence.Get(), signalVal));

    
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

   
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(
            m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    
    m_fenceValues[m_frameIndex] = signalVal + 1;
}


void App::OnResize(int w, int h)
{
    if (w == 0 || h == 0 || (w == m_width && h == m_height)) return;
    m_width = w; m_height = h;

    WaitForGPU();

    for (auto& rt : m_renderTargets) rt.Reset();
    m_depthStencil.Reset();

    ThrowIfFailed(m_swapChain->ResizeBuffers(
        FRAME_COUNT, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescSize);
    }

    CreateDepthBuffer();
    m_camera->SetAspect((float)w / (float)h);
}


void App::UpdateWindowTitle()
{
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(1)
       << L"DX12 Culling  FPS:" << m_fps
       << L"  Vis:" << m_visibleObjects << L"/" << m_totalObjects
       << L"  (mesh:" << m_cullResult.visibleFull.size()
       << L" box:" << m_cullResult.visibleBox.size() << L")"
       << L"  [1]Frustum:" << (m_frustumCullingEnabled ? L"ON" : L"OFF")
       << L"  [2]Octree:"  << (m_octTreeEnabled        ? L"ON" : L"OFF")
       << L"  [3]Tree:"    << (m_showOctTree           ? L"ON" : L"OFF")
       << L"  LOD:" << std::setprecision(0) << sqrtf(m_lodDistanceSq)
       << L"u[+-]  RMB=look WASD/QE=move Shift=fast";
    SetWindowText(m_hWnd, ss.str().c_str());
}
