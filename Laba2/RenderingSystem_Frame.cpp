#include "RenderingSystem.h"
#include <d3dx12.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Scene / constant buffers
// ---------------------------------------------------------------------------
void RenderingSystem::LoadScene()
{
    // Assumes assets/sponza/sponza.obj + sponza.mtl + textures were placed
    // by the user (see README.md). Adjust the path if you use a different model.
    m_sceneMesh.LoadFromOBJ(m_device.Get(), m_commandList.Get(),
        "assets/sponza/sponza.obj", "assets/sponza/");

    XMStoreFloat4x4(&m_sceneWorld, XMMatrixIdentity());

    XMFLOAT3 bMin = m_sceneMesh.BoundsMin();
    XMFLOAT3 bMax = m_sceneMesh.BoundsMax();

    // Rain spawns above the model and lands roughly at its lowest point (the floor).
    m_lightManager = std::make_unique<LightManager>(
        XMFLOAT2(bMin.x, bMin.z), XMFLOAT2(bMax.x, bMax.z),
        /*floorY*/ bMin.y, /*spawnY*/ bMax.y + (bMax.y - bMin.y) * 0.5f);

    // Homework-required light sources: one directional (sun) plus a handful
    // of point/spot lights scattered through the scene, in addition to the rain.
    m_lightManager->AddDirectionalLight({ -0.4f, -1.0f, 0.3f }, { 1.0f, 0.95f, 0.85f }, 1.2f);

    float midY = (bMin.y + bMax.y) * 0.5f;
    m_lightManager->AddPointLight({ bMin.x * 0.5f, midY, 0.0f }, { 1.0f, 0.4f, 0.2f }, 6.0f, 15.0f);
    m_lightManager->AddPointLight({ bMax.x * 0.5f, midY, 0.0f }, { 0.2f, 0.4f, 1.0f }, 6.0f, 15.0f);
    m_lightManager->AddPointLight({ 0.0f, midY, bMin.z * 0.5f }, { 0.3f, 1.0f, 0.3f }, 6.0f, 15.0f);
    m_lightManager->AddSpotLight({ 0.0f, bMax.y, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f }, 8.0f, bMax.y - bMin.y, 20.0f, 35.0f);

    m_lightManager->SetSpawnRatePerSecond(4.0f);
    m_lightManager->SetMaxDrops(std::min<size_t>(48, kMaxLights - m_lightManager->StaticLightCount() - 8));
}

void RenderingSystem::CreateConstantBuffers()
{
    // One ObjectConstants slot (single static mesh with an identity world transform).
    m_objectCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);

    // One MaterialConstants slot per OBJ material.
    UINT materialCount = (UINT)m_sceneMesh.Materials().size();
    m_materialCB = std::make_unique<UploadBuffer<MaterialConstants>>(m_device.Get(), materialCount, true);
    for (UINT i = 0; i < materialCount; i++)
    {
        const auto& mat = m_sceneMesh.Materials()[i];
        MaterialConstants mc;
        mc.DiffuseColor = mat.DiffuseColor;
        mc.Roughness = mat.Roughness;
        mc.Metallic = mat.Metallic;
        m_materialCB->CopyData(i, mc);
    }

    for (UINT i = 0; i < kFrameCount; i++)
    {
        m_frameCB[i] = std::make_unique<UploadBuffer<FrameConstants>>(m_device.Get(), 1, true);
        m_markerCB[i] = std::make_unique<UploadBuffer<MarkerConstants>>(m_device.Get(), 1, true);
        m_lightBuffer[i] = std::make_unique<UploadBuffer<GPULight>>(m_device.Get(), kMaxLights, false);
    }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void RenderingSystem::Update(float deltaSeconds)
{
    m_camera.UpdateViewMatrix();
    m_lightManager->Update(deltaSeconds);

    // Object constants (single static mesh, so this only really needs to be
    // written once, but keeping it per-frame makes animating the model trivial later).
    XMMATRIX world = XMLoadFloat4x4(&m_sceneWorld);
    XMMATRIX viewProj = m_camera.GetViewProj();

    ObjectConstants oc;
    XMStoreFloat4x4(&oc.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&oc.WorldViewProj, XMMatrixTranspose(world * viewProj));
    m_objectCB->CopyData(0, oc);

    // Frame constants + light list for the *current* backbuffer slot.
    FrameConstants fc;
    fc.CameraPosW = m_camera.GetPosition();
    fc.AmbientColor = { 0.04f, 0.045f, 0.05f };

    const auto& lights = m_lightManager->BuildGPULightList();
    UINT lightCount = (UINT)std::min<size_t>(lights.size(), kMaxLights);
    fc.LightCount = lightCount;
    m_frameCB[m_frameIndex]->CopyData(0, fc);
    m_currentLightCount = lightCount;

    for (UINT i = 0; i < lightCount; i++)
        m_lightBuffer[m_frameIndex]->CopyData(i, lights[i]);

    // Marker constants (billboards for visualizing each light's position).
    MarkerConstants mc;
    XMStoreFloat4x4(&mc.ViewProj, XMMatrixTranspose(viewProj));
    mc.CameraRight = m_camera.GetRight();
    mc.CameraUp = m_camera.GetUp();
    mc.MarkerSize = 0.3f;
    m_markerCB[m_frameIndex]->CopyData(0, mc);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void RenderingSystem::Render()
{
    ThrowIfFailedM(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailedM(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    D3D12_VIEWPORT viewport = { 0, 0, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    RenderGBufferPass();

    m_gbuffer.TransitionToRead(m_commandList.Get());

    D3D12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(
        m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRT);

    RenderLightingPass();

    RenderLightMarkersPass();

    D3D12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &toPresent);

    ThrowIfFailedM(m_commandList->Close());
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    HRESULT presentHr = m_swapChain->Present(1, 0);
    if (FAILED(presentHr))
    {
        char hexBuf[32];
        sprintf_s(hexBuf, "0x%08X", (unsigned int)presentHr);
        std::string msg = std::string("m_swapChain->Present(1, 0) failed: ") + hexBuf;

        if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET ||
            presentHr == DXGI_ERROR_DEVICE_HUNG)
        {
            msg += "\n\nThe GPU crashed or hung (this is a driver-level TDR, not a simple API-usage error).\n\n";
            msg += GetDeviceRemovedDiagnostics(m_device.Get());
            msg += "\n\nCheck the Visual Studio Output window (Debug category) for 'D3D12 ERROR' "
                   "lines logged just before this point — they were printed by the debug layer / DRED "
                   "and name the exact resource or draw call involved.";
        }

        throw std::runtime_error(msg);
    }

    MoveToNextFrame();
}

void RenderingSystem::RenderGBufferPass()
{
    m_gbuffer.TransitionToWrite(m_commandList.Get());
    m_gbuffer.Clear(m_commandList.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GBuffer::kNumRenderTargets] = {
        m_gbuffer.RTVHandle(0), m_gbuffer.RTVHandle(1), m_gbuffer.RTVHandle(2)
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_gbuffer.DSVHandle();
    m_commandList->OMSetRenderTargets(GBuffer::kNumRenderTargets, rtvs, FALSE, &dsv);

    m_commandList->SetGraphicsRootSignature(m_gbufferRootSig.Get());
    m_commandList->SetPipelineState(m_gbufferPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto vbv = m_sceneMesh.VertexBufferView();
    auto ibv = m_sceneMesh.IndexBufferView();
    m_commandList->IASetVertexBuffers(0, 1, &vbv);
    m_commandList->IASetIndexBuffer(&ibv);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_objectCB->Resource()->GetGPUVirtualAddress());

    for (const auto& sub : m_sceneMesh.SubMeshes())
    {
        int matIndex = std::max(sub.MaterialIndex, 0);
        D3D12_GPU_VIRTUAL_ADDRESS matAddr = m_materialCB->Resource()->GetGPUVirtualAddress() +
            (UINT64)matIndex * m_materialCB->ElementByteSize();
        m_commandList->SetGraphicsRootConstantBufferView(1, matAddr);

        m_commandList->DrawIndexedInstanced(sub.IndexCount, 1, sub.IndexStart, 0, 0);
    }
}

void RenderingSystem::RenderLightingPass()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_backBufferRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    m_commandList->SetGraphicsRootSignature(m_deferredRootSig.Get());
    m_commandList->SetPipelineState(m_deferredPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_frameCB[m_frameIndex]->Resource()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootShaderResourceView(2, m_lightBuffer[m_frameIndex]->Resource()->GetGPUVirtualAddress());

    m_commandList->DrawInstanced(3, 1, 0, 0); // fullscreen triangle, positions generated in VS
}

void RenderingSystem::RenderLightMarkersPass()
{
    if (m_currentLightCount == 0) return;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_backBufferRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_gbuffer.DSVHandle();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    m_commandList->SetGraphicsRootSignature(m_markerRootSig.Get());
    m_commandList->SetPipelineState(m_markerPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_markerCB[m_frameIndex]->Resource()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(1, m_lightBuffer[m_frameIndex]->Resource()->GetGPUVirtualAddress());

    m_commandList->DrawInstanced(6, m_currentLightCount, 0, 0); // 6 verts (quad) x N light instances
}

// ---------------------------------------------------------------------------
// Sync / resize / shutdown
// ---------------------------------------------------------------------------
void RenderingSystem::MoveToNextFrame()
{
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailedM(m_commandQueue->Signal(m_fence.Get(), currentFenceValue + 1));

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailedM(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

void RenderingSystem::WaitForGPU()
{
    ThrowIfFailedM(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex] + 1));
    ThrowIfFailedM(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex] + 1, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValues[m_frameIndex]++;
}

void RenderingSystem::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;

    WaitForGPU();

    m_width = width;
    m_height = height;

    for (UINT i = 0; i < kFrameCount; i++)
        m_backBuffers[i].Reset();

    ThrowIfFailedM(m_swapChain->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateBackBufferRTVs();

    m_gbuffer.Resize(m_device.Get(), width, height);
    CreateGBufferSRVs();

    m_camera.SetLens(XM_PIDIV4, (float)width / (float)height, 0.1f, 500.0f);
}

void RenderingSystem::Shutdown()
{
    WaitForGPU();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}
