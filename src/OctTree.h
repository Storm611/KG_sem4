#pragma once
#include "DX12Common.h"
#include "Scene.h"
#include <vector>
#include <array>
#include <memory>

struct OctNode
{
    BoundingBox               bounds;
    std::vector<int>          objectIndices; 
    std::array<std::unique_ptr<OctNode>,8> children;
    bool IsLeaf() const
    {
        for (auto& c : children) if (c) return false;
        return true;
    }
};


class OctTree
{
public:
    void Build(const std::vector<SceneObject>& objects,
               float halfExtent,          
               int   maxDepth   = 5,
               int   maxPerLeaf = 8);

 
    void Query(const BoundingFrustum& frustum,
               std::vector<int>& outIndices) const;


    const OctNode* GetRoot() const { return m_root.get(); }

private:
    std::unique_ptr<OctNode> m_root;

    void Insert(OctNode* node, int objIdx,
                const BoundingBox& objAABB,
                int depth, int maxDepth, int maxPerLeaf,
                const std::vector<SceneObject>& objects);

    void Split(OctNode* node,
               int depth, int maxDepth, int maxPerLeaf,
               const std::vector<SceneObject>& objects);

    void QueryNode(const OctNode* node,
                   const BoundingFrustum& frustum,
                   std::vector<int>& out) const;
};
