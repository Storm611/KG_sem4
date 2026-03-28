#include "Window.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

Window* Window::currentInstance = nullptr;

Window::Window(const std::wstring& title, int width, int height)
    : windowTitle(title), windowWidth(width), windowHeight(height),
    isExitRequested(false), exitCode(0),
    camera(XMFLOAT3(1.0f, 2.0f, -5.0f), XMFLOAT3(0.0f, 3.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)),
    timer(),
    constantBufferSize((sizeof(ConstantBufferData) + 255) & ~255),
    cb{}
{
    RegisterWindowClass();
    hwnd = CreateNativeWindow();
    if (!hwnd) throw std::runtime_error("Failed to create window.");

    currentInstance = this;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    InitializeDirectX();

    // Load model
    try {
        LoadModel("Sofa_OBJ2.obj");
        std::cout << "Model loaded successfully!\n";
        std::cout << "Vertices: " << vertices.size() << "\n";
        std::cout << "Indices: " << indices.size() << "\n";
        std::cout << "Submeshes: " << submeshes.size() << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading model: " << e.what() << std::endl;
    }

    if (materials.empty()) {
        // Если нет материалов, создаем дефолтный
        Material defaultMat;
        defaultMat.name = "default";
        defaultMat.diffuse = XMFLOAT3(1.0f, 1.0f, 1.0f);
        defaultMat.textureName = "dark+wood.png";
        materials.push_back(defaultMat);
        std::cout << "Created default material with texture: dark+wood.png\n";
    }


    for (auto& mat : materials) {
        mat.textureName = "dark+wood.png";
    }

    // Потом загружаем
    for (auto& mat : materials) {
        if (!mat.textureName.empty()) {
            std::wstring wTextureName(mat.textureName.begin(), mat.textureName.end());
            ID3D12Resource* texture = nullptr;
            int descIndex = -1;

            HRESULT hr = LoadTexture(wTextureName, &texture, descIndex);

            if (SUCCEEDED(hr)) {
                mat.texture = texture;
                mat.textureDescriptorIndex = descIndex;
                std::cout << "Loaded texture: " << mat.textureName << "\n";
            }
            else {
                std::cerr << "Failed to load texture: " << mat.textureName << "\n";
            }
        }
    }


}

Window::~Window() {
    WaitForPreviousFrame();

    if (fenceEvent) CloseHandle(fenceEvent);

    for (auto& tex : textures) if (tex) tex->Release();
    if (vertexBuffer) vertexBuffer->Release();
    if (indexBuffer) indexBuffer->Release();
    if (constantBuffer) {
        constantBuffer->Unmap(0, nullptr);
        constantBuffer->Release();
    }
    if (depthStencilBuffer) depthStencilBuffer->Release();
    if (rootSignature) rootSignature->Release();
    if (pipelineState) pipelineState->Release();
    if (commandList) commandList->Release();
    for (int i = 0; i < 2; i++) if (commandAllocators[i]) commandAllocators[i]->Release();
    if (commandQueue) commandQueue->Release();
    for (int i = 0; i < 2; i++) if (renderTargets[i]) renderTargets[i]->Release();
    if (rtvHeap) rtvHeap->Release();
    if (dsvHeap) dsvHeap->Release();
    if (srvHeap) srvHeap->Release();
    if (swapChain) swapChain->Release();
    if (fence) fence->Release();
    if (d3dDevice) d3dDevice->Release();
    if (vertexShaderBlob) vertexShaderBlob->Release();
    if (pixelShaderBlob) pixelShaderBlob->Release();

    if (hwnd) DestroyWindow(hwnd);
}

void Window::RegisterWindowClass() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"MyAppWindowClass";
    if (!RegisterClassExW(&wcex)) {
        throw std::runtime_error("Failed to register window class.");
    }
}

HWND Window::CreateNativeWindow() {
    RECT rect = { 0, 0, windowWidth, windowHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowExW(
        0, L"MyAppWindowClass", windowTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }
    return hWnd;
}

void Window::InitializeDirectX() {
    // Enable debug layer
#ifdef _DEBUG
    ID3D12Debug* debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->Release();
    }
#endif

    // Create device
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3dDevice));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create D3D12 device.");
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) throw std::runtime_error("Failed to create command queue.");

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = windowWidth;
    swapChainDesc.Height = windowHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    IDXGIFactory4* dxgiFactory = nullptr;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) throw std::runtime_error("Failed to create DXGI factory.");

    hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, (IDXGISwapChain1**)&swapChain);
    if (FAILED(hr)) throw std::runtime_error("Failed to create swap chain.");

    dxgiFactory->Release();

    // Get frame index
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create RTV heap.");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create DSV heap.");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 100;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create SRV heap.");

    rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    srvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 2; i++) {
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        if (FAILED(hr)) throw std::runtime_error("Failed to get back buffer.");
        d3dDevice->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    // Create depth stencil buffer
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = windowWidth;
    depthDesc.Height = windowHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    hr = d3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthStencilBuffer)
    );
    if (FAILED(hr)) throw std::runtime_error("Failed to create depth buffer.");

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    d3dDevice->CreateDepthStencilView(depthStencilBuffer, nullptr, dsvHandle);

    // Create command allocators and list
    for (int i = 0; i < 2; i++) {
        hr = d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i]));
        if (FAILED(hr)) throw std::runtime_error("Failed to create command allocator.");
    }

    hr = d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[0], nullptr, IID_PPV_ARGS(&commandList));
    if (FAILED(hr)) throw std::runtime_error("Failed to create command list.");
    commandList->Close();

    // Create fence
    hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) throw std::runtime_error("Failed to create fence.");

    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) throw std::runtime_error("Failed to create fence event.");

    camera.SetPerspective(XM_PIDIV4, (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);
    viewMatrix = camera.GetViewMatrix();
    projectionMatrix = camera.GetProjectionMatrix();

    CreateShaders();
    CreateConstantBuffer();
    CreatePSO();
}

void Window::CreateShaders() {
    const char* vsSource = R"(
        struct VS_INPUT {
            float3 pos : POSITION;
            float3 normal : NORMAL;
            float2 tex : TEXCOORD0;
        };

        struct VS_OUTPUT {
            float4 pos : SV_POSITION;
            float3 worldPos : POSITION;
            float3 normal : NORMAL;
            float2 tex : TEXCOORD0;
        };

        cbuffer ConstantBuffer : register(b0) {
            float4x4 worldViewProjection;
            float4x4 world;
            float3 lightDir;
            float padding1;
            float3 cameraPos;
            float time;
            float2 tiling;
            float2 animSpeed;
            float3 diffuseColor;
            float specularIntensity;
        };

        VS_OUTPUT main(VS_INPUT input) {
            VS_OUTPUT output;
            
            // Преобразуем позицию в мировое пространство
            float4 worldPos = mul(float4(input.pos, 1.0f), world);
            output.worldPos = worldPos.xyz;
            
            // Преобразуем в пространство проекции
            output.pos = mul(worldPos, worldViewProjection);
            
            // Преобразуем нормали (транспонированная обратная матрица)
            output.normal = normalize(mul(input.normal, (float3x3)world));
            
            output.tex = input.tex;
            return output;
        }
    )";

    const char* psSource = R"(
        Texture2D diffuseMap : register(t0);
        SamplerState samLinear : register(s0);

        cbuffer ConstantBuffer : register(b0) {
            float4x4 worldViewProjection;
            float4x4 world;
            float3 lightDir;
            float padding1;
            float3 cameraPos;
            float time;
            float2 tiling;
            float2 animSpeed;
            float3 diffuseColor;
            float specularIntensity;
        };

        struct PS_INPUT {
            float4 pos : SV_POSITION;
            float3 worldPos : POSITION;
            float3 normal : NORMAL;
            float2 tex : TEXCOORD0;
        };

        float4 main(PS_INPUT input) : SV_TARGET {
            // Текстурные координаты
            float2 uv = input.tex * tiling + time * animSpeed;
            float4 texColor = diffuseMap.Sample(samLinear, uv);
            
            // Нормализуем нормали
            float3 N = normalize(input.normal);
            
            // Направление света (фиксированное)
            float3 L = normalize(lightDir);
            
            // Диффузное освещение (Ламберт)
            float NdotL = max(0.2, dot(N, L)); // Минимальное освещение 0.2
            
            // Цвета
            float3 albedo = texColor.rgb * diffuseColor;
            
            // Ambient (базовое освещение)
            float3 ambient = albedo * 0.3;
            
            // Diffuse
            float3 diffuse = albedo * NdotL * 0.7;
            
            // Итоговый цвет
            float3 finalColor = ambient + diffuse;
            
            // Добавляем небольшой оттенок для визуализации (для отладки)
            // finalColor += float3(0.1, 0.0, 0.0) * abs(input.normal.x); // Красный по X
            // finalColor += float3(0.0, 0.1, 0.0) * abs(input.normal.y); // Зеленый по Y
            // finalColor += float3(0.0, 0.0, 0.1) * abs(input.normal.z); // Синий по Z
            
            return float4(finalColor, texColor.a);
        }
    )";

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    HRESULT hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0",
        compileFlags, 0, &vertexShaderBlob, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile vertex shader\n";
        throw std::runtime_error("Failed to compile vertex shader");
    }

    hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0",
        compileFlags, 0, &pixelShaderBlob, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile pixel shader\n";
        throw std::runtime_error("Failed to compile pixel shader");
    }

    std::cout << "Shaders compiled successfully\n";
}

void Window::CreateConstantBuffer() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = constantBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer)
    );
    if (FAILED(hr)) throw std::runtime_error("Failed to create constant buffer.");

    // Map the constant buffer - используем nullptr вместо readRange
    hr = constantBuffer->Map(0, nullptr, &constantBufferData);
    if (FAILED(hr)) throw std::runtime_error("Failed to map constant buffer.");
}

void Window::CreatePSO() {
    // Create root signature
    D3D12_ROOT_PARAMETER rootParameters[2];

    // CBV parameter
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Descriptor table for texture SRV
    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange.NumDescriptors = 1;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 2;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signatureBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Root signature error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            errorBlob->Release();
        }
        throw std::runtime_error("Failed to serialize root signature.");
    }

    hr = d3dDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    signatureBlob->Release();
    if (FAILED(hr)) throw std::runtime_error("Failed to create root signature.");

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // PSO description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    if (FAILED(hr)) throw std::runtime_error("Failed to create pipeline state.");
}

HRESULT Window::LoadTexture(const std::wstring& fileName, ID3D12Resource** textureResource, int& descriptorIndex) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr)) return hr;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(fileName.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        factory->Release();
        return hr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    decoder->GetFrame(0, &frame);

    UINT w, h;
    frame->GetSize(&w, &h);

    std::vector<BYTE> data(w * h * 4);
    hr = frame->CopyPixels(nullptr, w * 4, static_cast<UINT>(data.size()), data.data());

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = w;
    textureDesc.Height = h;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(textureResource));

    if (SUCCEEDED(hr)) {
        // Upload texture data
        UINT64 uploadBufferSize = 0;
        d3dDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadBufferDesc = {};
        uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadBufferDesc.Width = uploadBufferSize;
        uploadBufferDesc.Height = 1;
        uploadBufferDesc.DepthOrArraySize = 1;
        uploadBufferDesc.MipLevels = 1;
        uploadBufferDesc.SampleDesc.Count = 1;
        uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource* uploadBuffer = nullptr;
        hr = d3dDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

        if (SUCCEEDED(hr)) {
            // Reset command list for upload
            commandList->Reset(commandAllocators[frameIndex], nullptr);

            // Copy texture data
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
            UINT rowCount = 0;
            UINT64 rowSizeInBytes = 0;
            d3dDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeInBytes, nullptr);

            // Fill upload buffer
            BYTE* uploadData = nullptr;
            uploadBuffer->Map(0, nullptr, (void**)&uploadData);

            for (UINT row = 0; row < rowCount; ++row) {
                memcpy(uploadData + footprint.Footprint.RowPitch * row,
                    data.data() + rowSizeInBytes * row,
                    rowSizeInBytes);
            }
            uploadBuffer->Unmap(0, nullptr);

            // Copy from upload to texture
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.pResource = uploadBuffer;
            src.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource = *textureResource;
            dst.SubresourceIndex = 0;

            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            // Transition to shader resource
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = *textureResource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            commandList->ResourceBarrier(1, &barrier);
            commandList->Close();

            // Execute command list
            ID3D12CommandList* cmdLists[] = { commandList };
            commandQueue->ExecuteCommandLists(1, cmdLists);
            WaitForPreviousFrame();

            uploadBuffer->Release();
        }

        // Create SRV
        descriptorIndex = static_cast<int>(textures.size());
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += descriptorIndex * srvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        d3dDevice->CreateShaderResourceView(*textureResource, &srvDesc, srvHandle);

        textures.push_back(*textureResource);
        textureDescriptorIndices.push_back(descriptorIndex);
    }

    if (factory) factory->Release();
    if (decoder) decoder->Release();
    if (frame) frame->Release();

    return hr;
}

void Window::LoadMTL(const std::string& mtlFileName) {
    std::ifstream file(mtlFileName);
    if (!file) {
        std::cerr << "Failed to open MTL: " << mtlFileName << std::endl;
        return;
    }

    Material currentMat;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        if (type == "newmtl") {
            if (!currentMat.name.empty()) materials.push_back(currentMat);
            iss >> currentMat.name;
            currentMat.diffuse = XMFLOAT3(1.0f, 1.0f, 1.0f);
            currentMat.specularIntensity = 1.0f;
        }
        else if (type == "Kd") {
            iss >> currentMat.diffuse.x >> currentMat.diffuse.y >> currentMat.diffuse.z;
        }
        else if (type == "map_Kd") {
            iss >> currentMat.textureName;
        }
        else if (type == "Ns") {
            float ns;
            iss >> ns;
            currentMat.specularIntensity = ns / 1000.0f;
        }
    }
    if (!currentMat.name.empty()) materials.push_back(currentMat);
}

void Window::LoadModel(const std::string& objFileName) {
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT2> texcoords;
    std::vector<XMFLOAT3> normals;
    std::map<std::tuple<int, int, int>, UINT> vertexMap;

    std::ifstream file(objFileName);
    if (!file) {
        std::cerr << "Failed to open OBJ: " << objFileName << std::endl;
        throw std::runtime_error("Failed to open OBJ.");
    }

    std::string mtlLib;
    int currentMaterial = -1;
    Submesh currentSubmesh = { -1, 0, 0 };
    std::map<std::string, int> materialMap;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v") {
            XMFLOAT3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (type == "vt") {
            XMFLOAT2 t;
            iss >> t.x >> t.y;
            t.y = 1.0f - t.y;
            texcoords.push_back(t);
        }
        else if (type == "vn") {
            XMFLOAT3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (type == "f") {
            if (currentSubmesh.indexCount == 0) currentSubmesh.startIndex = static_cast<UINT>(indices.size());
            for (int i = 0; i < 3; ++i) {
                int v = 0, t = 0, n = 0;
                char slash;
                iss >> v >> slash >> t >> slash >> n;
                std::tuple<int, int, int> key(v, t, n);
                auto it = vertexMap.find(key);
                if (it == vertexMap.end()) {
                    Vertex vert;
                    vert.pos = positions[v - 1];
                    vert.tex = texcoords[t - 1];
                    vert.normal = normals[n - 1];
                    vertices.push_back(vert);
                    UINT idx = static_cast<UINT>(vertices.size() - 1);
                    vertexMap[key] = idx;
                    indices.push_back(idx);
                }
                else {
                    indices.push_back(it->second);
                }
            }
            currentSubmesh.indexCount += 3;
        }
        else if (type == "mtllib") {
            iss >> mtlLib;
            LoadMTL(mtlLib);
            for (size_t i = 0; i < materials.size(); ++i) {
                materialMap[materials[i].name] = static_cast<int>(i);
            }
        }
        else if (type == "usemtl") {
            if (currentSubmesh.indexCount > 0) submeshes.push_back(currentSubmesh);
            std::string matName;
            iss >> matName;
            currentMaterial = materialMap[matName];
            currentSubmesh.materialIndex = currentMaterial;
            currentSubmesh.startIndex = static_cast<UINT>(indices.size());
            currentSubmesh.indexCount = 0;
        }
    }
    if (currentSubmesh.indexCount > 0) submeshes.push_back(currentSubmesh);

    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("No vertices or indices loaded from OBJ file.");
    }

    // Create vertex buffer
    UINT vertexBufferSize = static_cast<UINT>(sizeof(Vertex) * vertices.size());

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = vertexBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create vertex buffer.");
    }

    // Copy vertex data to buffer
    void* mappedData = nullptr;
    hr = vertexBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, vertices.data(), vertexBufferSize);
        vertexBuffer->Unmap(0, nullptr);
    }

    // Create index buffer
    UINT indexBufferSize = static_cast<UINT>(sizeof(UINT) * indices.size());

    bufferDesc.Width = indexBufferSize;

    hr = d3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create index buffer.");
    }

    hr = indexBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, indices.data(), indexBufferSize);
        indexBuffer->Unmap(0, nullptr);
    }
}

void Window::UpdateMatrices() {
    viewMatrix = camera.GetViewMatrix();
    projectionMatrix = camera.GetProjectionMatrix();

    // Масштабируем модель (увеличим для наглядности)
    XMMATRIX world = XMMatrixScaling(0.5f, 0.5f, 0.5f);
    // Добавляем небольшое вращение для проверки
    // world = XMMatrixRotationY(cb.time * 0.5f) * world;

    // Вычисляем MVP матрицу (обратите внимание на порядок умножения!)
    XMMATRIX wvp = world * viewMatrix * projectionMatrix;

    cb.worldViewProjection = XMMatrixTranspose(wvp);
    cb.world = XMMatrixTranspose(world);

    // Направление света - пусть светит сверху и спереди
    cb.lightDir = XMFLOAT3(0.5f, -1.0f, 0.5f);
    // Нормализуем
    float len = sqrt(cb.lightDir.x * cb.lightDir.x +
        cb.lightDir.y * cb.lightDir.y +
        cb.lightDir.z * cb.lightDir.z);
    cb.lightDir.x /= len;
    cb.lightDir.y /= len;
    cb.lightDir.z /= len;

    cb.cameraPos = camera.GetPosition();
    cb.time = timer.TotalTime();


    //Тайлинг и анимации
    cb.tiling = XMFLOAT2(1.0f, 1.0f);
    cb.animSpeed = XMFLOAT2(1.0f, 1.0f);

    // Устанавливаем яркий цвет (белый)
    cb.diffuseColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
    cb.specularIntensity = 1.0f;

    // Отладка
    static float lastTime = 0;
    if (cb.time - lastTime > 1.0f) {
        lastTime = cb.time;
        std::cout << "LightDir: " << cb.lightDir.x << ", " << cb.lightDir.y << ", " << cb.lightDir.z << "\n";
        std::cout << "Camera pos: " << cb.cameraPos.x << ", " << cb.cameraPos.y << ", " << cb.cameraPos.z << "\n";
    }
}

void Window::RenderFrame() {
    timer.Tick();
    float delta = timer.DeltaTime();
    camera.ProcessKeyboard(cameraSpeed * delta);

    // Update constant buffer
    UpdateMatrices();
    memcpy(constantBufferData, &cb, sizeof(cb));

    // Reset command allocator and list
    commandAllocators[frameIndex]->Reset();
    commandList->Reset(commandAllocators[frameIndex], pipelineState);

    // Set root signature
    commandList->SetGraphicsRootSignature(rootSignature);

    // Set viewport and scissor rect
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, windowWidth, windowHeight };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    // Set render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * rtvDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear render target and depth buffer
    float clearColor[] = { 0.1f, 0.15f, 0.25f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set vertex and index buffers
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(UINT) * indices.size());
    commandList->IASetIndexBuffer(&indexBufferView);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set constant buffer
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());

    // Set descriptor heap for textures
    ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    // Draw each submesh
    for (const auto& sub : submeshes) {
        if (sub.materialIndex >= 0 && sub.materialIndex < (int)materials.size()) {
            const Material& mat = materials[sub.materialIndex];

            // Set texture if available
            if (mat.texture && mat.textureDescriptorIndex >= 0) {
                D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
                srvHandle.ptr += mat.textureDescriptorIndex * srvDescriptorSize;
                commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
            }
        }

        commandList->DrawIndexedInstanced(sub.indexCount, 1, sub.startIndex, 0, 0);
    }

    // Transition render target to present state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets[frameIndex];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Close and execute command list
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, cmdLists);

    // Present
    swapChain->Present(1, 0);

    // Wait for frame completion
    WaitForPreviousFrame();
    MoveToNextFrame();
}

void Window::WaitForPreviousFrame() {
    fenceValues[frameIndex]++;
    commandQueue->Signal(fence, fenceValues[frameIndex]);

    if (fence->GetCompletedValue() < fenceValues[frameIndex]) {
        fence->SetEventOnCompletion(fenceValues[frameIndex], fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void Window::MoveToNextFrame() {
    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

int Window::Run() {
    MSG msg = {};
    while (!isExitRequested) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                RequestExit();
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) RequestExit();
        RenderFrame();
    }
    return exitCode;
}

void Window::RequestExit(int exitCode) {
    this->exitCode = exitCode;
    isExitRequested = true;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Window* pThis = nullptr;
    POINT center{ 0, 0 };

    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        switch (uMsg)
        {
        case WM_DESTROY:
        case WM_CLOSE:
            pThis->RequestExit();
            return 0;

        case WM_LBUTTONDOWN:
            ShowCursor(FALSE);
            pThis->mouseCaptured = true;
            pThis->firstMouse = true;

            RECT rect;
            GetClientRect(hwnd, &rect);

            center.x = rect.left + (rect.right - rect.left) / 2;
            center.y = rect.top + (rect.bottom - rect.top) / 2;

            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            return 0;

        case WM_LBUTTONUP:
            ShowCursor(TRUE);
            pThis->mouseCaptured = false;
            return 0;

        case WM_MOUSEMOVE:
            if (pThis->mouseCaptured)
            {
                POINT currentPos;
                GetCursorPos(&currentPos);
                ScreenToClient(hwnd, &currentPos);

                RECT rect;
                GetClientRect(hwnd, &rect);

                float centerX = static_cast<float>(rect.left + (rect.right - rect.left) / 2);
                float centerY = static_cast<float>(rect.top + (rect.bottom - rect.top) / 2);

                float xoffset = static_cast<float>(currentPos.x) - centerX;
                float yoffset = static_cast<float>(currentPos.y) - centerY;

                center.x = static_cast<LONG>(centerX);
                center.y = static_cast<LONG>(centerY);

                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);

                pThis->camera.ProcessMouseMovement(xoffset, yoffset);
            }
            return 0;

        default:
            break;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}