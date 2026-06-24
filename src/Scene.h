#pragma once
#include "DX12Common.h"
#include <random>


struct SceneObject
{
    XMFLOAT3        position;
    XMFLOAT3        scale;
    float           rotY;

    BoundingBox     aabb;


    XMFLOAT4        color;


    int             meshIndex = 0;

    XMMATRIX GetWorld() const
    {
        return XMMatrixScaling(scale.x, scale.y, scale.z)
             * XMMatrixRotationY(rotY)
             * XMMatrixTranslation(position.x, position.y, position.z);
    }
};

struct MeshData
{
    std::vector<Vertex>  vertices;
    std::vector<uint32_t> indices;

  
    BoundingBox localAABB;


    ComPtr<ID3D12Resource> vbGPU, ibGPU;
    ComPtr<ID3D12Resource> vbUpload, ibUpload;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW  ibv = {};
};


struct BoxMesh
{
    ComPtr<ID3D12Resource> vbGPU, ibGPU;
    ComPtr<ID3D12Resource> vbUpload, ibUpload;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW  ibv = {};
    UINT indexCount = 0;
};

class Scene
{
public:
    Scene() = default;


    void Build(ID3D12Device* device,
               ID3D12GraphicsCommandList* cmdList,
               const std::string& objPath,
               int numObjects,
               float sceneRadius);

    void BuildBoxMesh(ID3D12Device* device,
                      ID3D12GraphicsCommandList* cmdList);

    const std::vector<SceneObject>& GetObjects() const { return m_objects; }
    const MeshData&  GetMesh()    const { return m_mesh; }
    const BoxMesh&   GetBoxMesh() const { return m_boxMesh; }

private:
    std::vector<SceneObject> m_objects;
    MeshData                 m_mesh;
    BoxMesh                  m_boxMesh;

    void LoadOBJ(const std::string& path,
                 ID3D12Device* device,
                 ID3D12GraphicsCommandList* cmdList);
    void GenerateInstances(int n, float radius);
    void ComputeWorldAABB(SceneObject& obj) const;
};
