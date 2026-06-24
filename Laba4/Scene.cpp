#include "Scene.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <filesystem>
#include <iostream>


static void BuildAABBFromVertices(const std::vector<Vertex>& verts, BoundingBox& out)
{
    if (verts.empty()) { out = {}; return; }
    XMFLOAT3 mn = verts[0].position, mx = verts[0].position;
    for (auto& v : verts)
    {
        mn.x = std::min(mn.x, v.position.x);
        mn.y = std::min(mn.y, v.position.y);
        mn.z = std::min(mn.z, v.position.z);
        mx.x = std::max(mx.x, v.position.x);
        mx.y = std::max(mx.y, v.position.y);
        mx.z = std::max(mx.z, v.position.z);
    }
    XMFLOAT3 center = { (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f, (mn.z+mx.z)*0.5f };
    XMFLOAT3 ext    = { (mx.x-mn.x)*0.5f, (mx.y-mn.y)*0.5f, (mx.z-mn.z)*0.5f };
    out = BoundingBox(center, ext);
}


void Scene::LoadOBJ(const std::string& path,
                    ID3D12Device* device,
                    ID3D12GraphicsCommandList* cmdList)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials,
                               &warn, &err, path.c_str(),
                               std::filesystem::path(path).parent_path().string().c_str());
    if (!warn.empty()) std::cout << "[OBJ warn] " << warn << "\n";
    if (!err.empty())  std::cerr << "[OBJ err]  " << err  << "\n";

    if (!ok || shapes.empty())
    {
 
        std::cout << "OBJ not found – generating procedural sphere\n";
        const int stacks = 12, slices = 16;
        for (int i = 0; i <= stacks; ++i)
        {
            float phi = XM_PI * i / stacks;
            for (int j = 0; j <= slices; ++j)
            {
                float theta = XM_2PI * j / slices;
                Vertex v;
                v.position = {
                    sinf(phi)*cosf(theta),
                    cosf(phi),
                    sinf(phi)*sinf(theta) };
                v.normal   = v.position;
                v.uv       = { (float)j/slices, (float)i/stacks };
                m_mesh.vertices.push_back(v);
            }
        }
        for (int i = 0; i < stacks; ++i)
            for (int j = 0; j < slices; ++j)
            {
                uint32_t a = i*(slices+1)+j, b=a+1,
                         c = (i+1)*(slices+1)+j, d=c+1;
                m_mesh.indices.insert(m_mesh.indices.end(), {a,c,b, b,c,d});
            }
    }
    else
    {

        for (auto& shape : shapes)
        {
            size_t idx_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
            {
                int fv = shape.mesh.num_face_vertices[f];
                for (int v = 0; v < fv; ++v)
                {
                    tinyobj::index_t idx = shape.mesh.indices[idx_offset + v];
                    Vertex vert;
                    vert.position = {
                        attrib.vertices[3*idx.vertex_index+0],
                        attrib.vertices[3*idx.vertex_index+1],
                        attrib.vertices[3*idx.vertex_index+2] };

                    if (idx.normal_index >= 0)
                        vert.normal = {
                            attrib.normals[3*idx.normal_index+0],
                            attrib.normals[3*idx.normal_index+1],
                            attrib.normals[3*idx.normal_index+2] };
                    else
                        vert.normal = {0,1,0};

                    if (idx.texcoord_index >= 0)
                        vert.uv = {
                            attrib.texcoords[2*idx.texcoord_index+0],
                            1.f - attrib.texcoords[2*idx.texcoord_index+1] };
                    else
                        vert.uv = {0,0};

                    m_mesh.indices.push_back((uint32_t)m_mesh.vertices.size());
                    m_mesh.vertices.push_back(vert);
                }
                idx_offset += fv;
            }
        }
    }

    BuildAABBFromVertices(m_mesh.vertices, m_mesh.localAABB);


    UINT64 vbSize = m_mesh.vertices.size() * sizeof(Vertex);
    m_mesh.vbGPU = CreateDefaultBuffer(device, cmdList,
        m_mesh.vertices.data(), vbSize, m_mesh.vbUpload);
    m_mesh.vbv.BufferLocation = m_mesh.vbGPU->GetGPUVirtualAddress();
    m_mesh.vbv.SizeInBytes    = (UINT)vbSize;
    m_mesh.vbv.StrideInBytes  = sizeof(Vertex);


    UINT64 ibSize = m_mesh.indices.size() * sizeof(uint32_t);
    m_mesh.ibGPU = CreateDefaultBuffer(device, cmdList,
        m_mesh.indices.data(), ibSize, m_mesh.ibUpload);
    m_mesh.ibv.BufferLocation = m_mesh.ibGPU->GetGPUVirtualAddress();
    m_mesh.ibv.SizeInBytes    = (UINT)ibSize;
    m_mesh.ibv.Format         = DXGI_FORMAT_R32_UINT;
}


void Scene::GenerateInstances(int n, float radius)
{
    std::mt19937 rng(42);
    auto frand = [&](float lo, float hi){
        return lo + (hi-lo)*(rng()/(float)UINT_MAX);
    };

    XMFLOAT4 palette[] = {
        {0.8f,0.3f,0.2f,1.f}, {0.2f,0.6f,0.8f,1.f},
        {0.3f,0.8f,0.3f,1.f}, {0.9f,0.8f,0.2f,1.f},
        {0.7f,0.3f,0.9f,1.f}, {0.9f,0.5f,0.1f,1.f} };

    m_objects.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        SceneObject obj;
        obj.position = { frand(-radius,radius), 0.f, frand(-radius,radius) };
        float s      = frand(0.01f, 0.05f);
        obj.scale    = { s, s, s };
        obj.rotY     = frand(0.f, XM_2PI);
        obj.color    = palette[i % 6];
        obj.meshIndex = 0;
        ComputeWorldAABB(obj);
        m_objects.push_back(obj);
    }
}


void Scene::ComputeWorldAABB(SceneObject& obj) const
{
    m_mesh.localAABB.Transform(obj.aabb, obj.GetWorld());
}


void Scene::BuildBoxMesh(ID3D12Device* device,
                         ID3D12GraphicsCommandList* cmdList)
{
    
    static const Vertex verts[] = {
      
        {{-0.5f,-0.5f, 0.5f},{0,0,1},{0,1}},
        {{ 0.5f,-0.5f, 0.5f},{0,0,1},{1,1}},
        {{ 0.5f, 0.5f, 0.5f},{0,0,1},{1,0}},
        {{-0.5f, 0.5f, 0.5f},{0,0,1},{0,0}},
      
        {{ 0.5f,-0.5f,-0.5f},{0,0,-1},{0,1}},
        {{-0.5f,-0.5f,-0.5f},{0,0,-1},{1,1}},
        {{-0.5f, 0.5f,-0.5f},{0,0,-1},{1,0}},
        {{ 0.5f, 0.5f,-0.5f},{0,0,-1},{0,0}},
     
        {{-0.5f,-0.5f,-0.5f},{-1,0,0},{0,1}},
        {{-0.5f,-0.5f, 0.5f},{-1,0,0},{1,1}},
        {{-0.5f, 0.5f, 0.5f},{-1,0,0},{1,0}},
        {{-0.5f, 0.5f,-0.5f},{-1,0,0},{0,0}},
      
        {{ 0.5f,-0.5f, 0.5f},{1,0,0},{0,1}},
        {{ 0.5f,-0.5f,-0.5f},{1,0,0},{1,1}},
        {{ 0.5f, 0.5f,-0.5f},{1,0,0},{1,0}},
        {{ 0.5f, 0.5f, 0.5f},{1,0,0},{0,0}},
        
        {{-0.5f, 0.5f, 0.5f},{0,1,0},{0,1}},
        {{ 0.5f, 0.5f, 0.5f},{0,1,0},{1,1}},
        {{ 0.5f, 0.5f,-0.5f},{0,1,0},{1,0}},
        {{-0.5f, 0.5f,-0.5f},{0,1,0},{0,0}},
      
        {{-0.5f,-0.5f,-0.5f},{0,-1,0},{0,1}},
        {{ 0.5f,-0.5f,-0.5f},{0,-1,0},{1,1}},
        {{ 0.5f,-0.5f, 0.5f},{0,-1,0},{1,0}},
        {{-0.5f,-0.5f, 0.5f},{0,-1,0},{0,0}},
    };
    static const uint32_t idxs[] = {
         0, 1, 2,  0, 2, 3,
         4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23,
    };
    m_boxMesh.indexCount = _countof(idxs);

    UINT64 vbSz = sizeof(verts), ibSz = sizeof(idxs);
    m_boxMesh.vbGPU = CreateDefaultBuffer(device, cmdList, verts, vbSz, m_boxMesh.vbUpload);
    m_boxMesh.ibGPU = CreateDefaultBuffer(device, cmdList, idxs, ibSz, m_boxMesh.ibUpload);

    m_boxMesh.vbv = { m_boxMesh.vbGPU->GetGPUVirtualAddress(), (UINT)vbSz, sizeof(Vertex) };
    m_boxMesh.ibv = { m_boxMesh.ibGPU->GetGPUVirtualAddress(), (UINT)ibSz, DXGI_FORMAT_R32_UINT };
}

void Scene::Build(ID3D12Device* device,
                  ID3D12GraphicsCommandList* cmdList,
                  const std::string& objPath,
                  int numObjects,
                  float sceneRadius)
{
    LoadOBJ(objPath, device, cmdList);
    BuildBoxMesh(device, cmdList);
    GenerateInstances(numObjects, sceneRadius);
}
