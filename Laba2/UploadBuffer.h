#pragma once
#include "DXHelpers.h"

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
        : m_isConstantBuffer(isConstantBuffer)
    {
        m_elementByteSize = sizeof(T);
        if (isConstantBuffer)
            m_elementByteSize = CalcConstantBufferByteSize(sizeof(T));

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (UINT64)m_elementByteSize * elementCount;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailedM(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_resource)));

        ThrowIfFailedM(m_resource->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedData)));

    }

    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;

    ~UploadBuffer()
    {
        if (m_resource != nullptr)
            m_resource->Unmap(0, nullptr);
        m_mappedData = nullptr;
    }

    ID3D12Resource* Resource() const { return m_resource.Get(); }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&m_mappedData[elementIndex * m_elementByteSize], &data, sizeof(T));
    }

    UINT ElementByteSize() const { return m_elementByteSize; }

private:
    ComPtr<ID3D12Resource> m_resource;
    BYTE* m_mappedData = nullptr;
    UINT m_elementByteSize = 0;
    bool m_isConstantBuffer = false;
};
