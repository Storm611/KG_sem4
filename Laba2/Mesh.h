#pragma once
#include "DXHelpers.h"
#include <vector>
#include <string>

struct Vertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
};

struct SubMeshMaterial
{
    XMFLOAT3 DiffuseColor = { 0.8f, 0.8f, 0.8f };
    float    Roughness = 0.6f;
    float    Metallic = 0.0f;
    std::string Name;
};

struct SubMesh
{
    UINT IndexStart = 0;
    UINT IndexCount = 0;
    int  MaterialIndex = -1;
};


class Mesh
{
public:

    void LoadFromOBJ(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                      const std::string& objPath, const std::string& mtlBaseDir);

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW  IndexBufferView() const;

    const std::vector<SubMesh>& SubMeshes() const { return m_subMeshes; }
    const std::vector<SubMeshMaterial>& Materials() const { return m_materials; }

    XMFLOAT3 BoundsMin() const { return m_boundsMin; }
    XMFLOAT3 BoundsMax() const { return m_boundsMax; }

private:
    ComPtr<ID3D12Resource> m_vertexBufferGPU;
    ComPtr<ID3D12Resource> m_indexBufferGPU;
    ComPtr<ID3D12Resource> m_vbUploader;
    ComPtr<ID3D12Resource> m_ibUploader;

    UINT m_vertexByteStride = sizeof(Vertex);
    UINT m_vertexBufferByteSize = 0;
    UINT m_indexBufferByteSize = 0;
    DXGI_FORMAT m_indexFormat = DXGI_FORMAT_R32_UINT;

    std::vector<SubMesh> m_subMeshes;
    std::vector<SubMeshMaterial> m_materials;

    XMFLOAT3 m_boundsMin = { FLT_MAX, FLT_MAX, FLT_MAX };
    XMFLOAT3 m_boundsMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
};
