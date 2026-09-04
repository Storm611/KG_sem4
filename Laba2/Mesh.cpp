#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>   // user-provided header, place in external/
#include "Mesh.h"
#include <unordered_map>
#include <stdexcept>

// Combines position/normal/uv indices into a single key so we can weld
// duplicate vertices the way a GPU vertex/index buffer needs them.
struct IndexKey
{
    int v, n, t;
    bool operator==(const IndexKey& o) const { return v == o.v && n == o.n && t == o.t; }
};
struct IndexKeyHash
{
    size_t operator()(const IndexKey& k) const
    {
        return ((std::hash<int>()(k.v) * 73856093) ^
                (std::hash<int>()(k.n) * 19349663) ^
                (std::hash<int>()(k.t) * 83492791));
    }
};

void Mesh::LoadFromOBJ(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                        const std::string& objPath, const std::string& mtlBaseDir)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                objPath.c_str(), mtlBaseDir.c_str(), /*triangulate=*/true);

    if (!warn.empty()) OutputDebugStringA(("tinyobj warning: " + warn + "\n").c_str());
    if (!err.empty())  OutputDebugStringA(("tinyobj error: " + err + "\n").c_str());
    if (!ok) throw std::runtime_error("Failed to load OBJ: " + objPath);

    // --- materials -------------------------------------------------------
    m_materials.clear();
    for (const auto& m : materials)
    {
        SubMeshMaterial sm;
        sm.Name = m.name;
        sm.DiffuseColor = XMFLOAT3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
        sm.Roughness = std::clamp(1.0f - m.shininess / 1000.0f, 0.05f, 1.0f);
        sm.Metallic = 0.0f; // OBJ/MTL has no physically-based metallic term
        m_materials.push_back(sm);
    }
    if (m_materials.empty())
        m_materials.push_back(SubMeshMaterial{}); // default material, index 0

    // --- vertices / indices, grouped per-material into submeshes --------
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<IndexKey, uint32_t, IndexKeyHash> uniqueVerts;

    // Gather (materialIndex -> list of face index triples) so all faces that
    // share a material end up contiguous in the index buffer (one draw call
    // per submesh).
    std::unordered_map<int, std::vector<IndexKey>> facesByMaterial;

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
        {
            int matId = shape.mesh.material_ids[f];
            if (matId < 0) matId = 0;
            unsigned char fv = shape.mesh.num_face_vertices[f];

            for (unsigned char v = 0; v < fv; v++)
            {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                facesByMaterial[matId].push_back({ idx.vertex_index, idx.normal_index, idx.texcoord_index });
            }
            indexOffset += fv;
        }
    }

    m_subMeshes.clear();
    for (auto& [matId, keys] : facesByMaterial)
    {
        SubMesh sub;
        sub.MaterialIndex = matId;
        sub.IndexStart = (UINT)indices.size();

        for (const auto& key : keys)
        {
            auto it = uniqueVerts.find(key);
            uint32_t vertIndex;
            if (it != uniqueVerts.end())
            {
                vertIndex = it->second;
            }
            else
            {
                Vertex vert = {};
                vert.Position = {
                    attrib.vertices[3 * key.v + 0],
                    attrib.vertices[3 * key.v + 1],
                    attrib.vertices[3 * key.v + 2]
                };
                if (key.n >= 0 && !attrib.normals.empty())
                {
                    vert.Normal = {
                        attrib.normals[3 * key.n + 0],
                        attrib.normals[3 * key.n + 1],
                        attrib.normals[3 * key.n + 2]
                    };
                }
                else
                {
                    vert.Normal = { 0, 1, 0 };
                }
                if (key.t >= 0 && !attrib.texcoords.empty())
                {
                    vert.TexCoord = {
                        attrib.texcoords[2 * key.t + 0],
                        1.0f - attrib.texcoords[2 * key.t + 1]
                    };
                }
                else
                {
                    vert.TexCoord = { 0, 0 };
                }

                m_boundsMin.x = std::min(m_boundsMin.x, vert.Position.x);
                m_boundsMin.y = std::min(m_boundsMin.y, vert.Position.y);
                m_boundsMin.z = std::min(m_boundsMin.z, vert.Position.z);
                m_boundsMax.x = std::max(m_boundsMax.x, vert.Position.x);
                m_boundsMax.y = std::max(m_boundsMax.y, vert.Position.y);
                m_boundsMax.z = std::max(m_boundsMax.z, vert.Position.z);

                vertIndex = (uint32_t)vertices.size();
                vertices.push_back(vert);
                uniqueVerts.emplace(key, vertIndex);
            }
            indices.push_back(vertIndex);
        }

        sub.IndexCount = (UINT)indices.size() - sub.IndexStart;
        m_subMeshes.push_back(sub);
    }

    // --- upload to GPU -----------------------------------------------------
    m_vertexBufferByteSize = (UINT)(vertices.size() * sizeof(Vertex));
    m_indexBufferByteSize = (UINT)(indices.size() * sizeof(uint32_t));

    m_vertexBufferGPU = CreateDefaultBuffer(device, cmdList, vertices.data(), m_vertexBufferByteSize, m_vbUploader);
    m_indexBufferGPU = CreateDefaultBuffer(device, cmdList, indices.data(), m_indexBufferByteSize, m_ibUploader);
}

D3D12_VERTEX_BUFFER_VIEW Mesh::VertexBufferView() const
{
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = m_vertexBufferGPU->GetGPUVirtualAddress();
    vbv.StrideInBytes = m_vertexByteStride;
    vbv.SizeInBytes = m_vertexBufferByteSize;
    return vbv;
}

D3D12_INDEX_BUFFER_VIEW Mesh::IndexBufferView() const
{
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = m_indexBufferGPU->GetGPUVirtualAddress();
    ibv.Format = m_indexFormat;
    ibv.SizeInBytes = m_indexBufferByteSize;
    return ibv;
}
