#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <d3dx12.h> 

using Microsoft::WRL::ComPtr;
using namespace DirectX;


class DxException : public std::runtime_error
{
public:
    DxException(HRESULT hr, const std::string& functionName, const std::string& file, int line)
        : std::runtime_error(BuildMessage(hr, functionName, file, line)) {}

private:

    static std::string HResultToString(HRESULT hr)
    {
        char buffer[512] = {};
        DWORD len = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer, (DWORD)sizeof(buffer), nullptr);

        char hexCode[32];
        sprintf_s(hexCode, "0x%08X", (unsigned int)hr);

        if (len == 0)
            return std::string("Unknown error ") + hexCode;

       
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
            buffer[--len] = '\0';

        return std::string(buffer) + " (" + hexCode + ")";
    }

    static std::string BuildMessage(HRESULT hr, const std::string& fn, const std::string& file, int line)
    {
        std::string msg = "DirectX call failed: " + fn +
            "\nFile: " + file + " (line " + std::to_string(line) + ")" +
            "\nError: " + HResultToString(hr);
        return msg;
    }
};

inline void ThrowIfFailed(HRESULT hr, const char* fn, const char* file, int line)
{
    if (FAILED(hr))
        throw DxException(hr, fn, file, line);
}

#define ThrowIfFailedM(hr) ThrowIfFailed((hr), #hr, __FILE__, __LINE__)


inline std::string GetDeviceRemovedDiagnostics(ID3D12Device* device)
{
    std::string result;
    if (!device) return result;

    HRESULT reason = device->GetDeviceRemovedReason();
    char buf[128];
    sprintf_s(buf, "GetDeviceRemovedReason(): 0x%08X", (unsigned int)reason);
    result += buf;

    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred))))
    {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)) && breadcrumbs.pHeadAutoBreadcrumbNode)
        {
            result += "\nDRED: breadcrumb data was captured. Attach the Visual Studio "
                      "debugger and break here (or inspect the Output window) to see the "
                      "'D3D12 ERROR: DRED' entries naming the exact command list / op that hung.";
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)))
        {
            sprintf_s(buf, "\nDRED page-fault VA: 0x%016llX", (unsigned long long)pageFault.PageFaultVA);
            result += buf;
            if (pageFault.pHeadExistingAllocationNode)
                result += "\n(matched against a still-live GPU allocation — see Output window for its name/type)";
        }
    }
    else
    {
        result += "\n(DRED interface unavailable — rebuild in Debug config so the debug layer / DRED are enabled)";
    }

    return result;
}


inline UINT CalcConstantBufferByteSize(UINT byteSize)
{
    return (byteSize + 255) & ~255;
}


inline ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = byteSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailedM(device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&defaultBuffer)));

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    ThrowIfFailedM(device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = byteSize;

    D3D12_RESOURCE_BARRIER toCopyDest = {};
    toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopyDest.Transition.pResource = defaultBuffer.Get();
    toCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toCopyDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

    D3D12_RESOURCE_BARRIER toGenericRead = {};
    toGenericRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toGenericRead.Transition.pResource = defaultBuffer.Get();
    toGenericRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toGenericRead.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    toGenericRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toGenericRead);

    return defaultBuffer;
}


inline ComPtr<ID3DBlob> CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entryPoint,
    const std::string& target)
{

    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    ComPtr<ID3DBlob> byteCode = nullptr;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(filename.c_str(), defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(),
        compileFlags, 0, &byteCode, &errors);

    std::string errorText;
    if (errors != nullptr)
    {
        errorText.assign((char*)errors->GetBufferPointer(), errors->GetBufferSize());
        OutputDebugStringA(errorText.c_str());
    }

    if (FAILED(hr))
    {

        std::string narrowName(filename.begin(), filename.end());

        std::string msg = "Failed to compile shader: " + narrowName + " (entry '" + entryPoint + "')\n";
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        {
            msg += "File not found. Check that the working directory contains a 'shaders/' folder "
                   "next to the .exe (Project Properties -> Debugging -> Working Directory), "
                   "and that the .hlsl files were copied/exist there.";
        }
        else if (!errorText.empty())
        {
            msg += "Compiler output:\n" + errorText;
        }
        else
        {
            msg += "No compiler output was returned; HRESULT indicates a general failure "
                   "(missing d3dcompiler_47.dll, bad shader model target, or invalid file path).";
        }
        throw std::runtime_error(msg);
    }

    return byteCode;
}
