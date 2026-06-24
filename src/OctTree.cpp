#include "OctTree.h"


void OctTree::Build(const std::vector<SceneObject>& objects,
                    float halfExtent, int maxDepth, int maxPerLeaf)
{
    m_root = std::make_unique<OctNode>();
    m_root->bounds = BoundingBox({0,0,0}, {halfExtent, halfExtent, halfExtent});

    for (int i = 0; i < (int)objects.size(); ++i)
        Insert(m_root.get(), i, objects[i].aabb, 0, maxDepth, maxPerLeaf, objects);
}


void OctTree::Insert(OctNode* node, int objIdx,
                     const BoundingBox& objAABB,
                     int depth, int maxDepth, int maxPerLeaf,
                     const std::vector<SceneObject>& objects)
{
    if (depth >= maxDepth || (!node->IsLeaf() == false && (int)node->objectIndices.size() < maxPerLeaf))
    {
        
        node->objectIndices.push_back(objIdx);
        if ((int)node->objectIndices.size() > maxPerLeaf && depth < maxDepth)
            Split(node, depth, maxDepth, maxPerLeaf, objects);
        return;
    }

    if (!node->IsLeaf())
    {
        
        for (auto& child : node->children)
        {
            if (child && child->bounds.Contains(objAABB) != DISJOINT)
            {
                Insert(child.get(), objIdx, objAABB, depth+1, maxDepth, maxPerLeaf, objects);
                return;
            }
        }
        
        node->objectIndices.push_back(objIdx);
        return;
    }

    node->objectIndices.push_back(objIdx);
    if ((int)node->objectIndices.size() > maxPerLeaf && depth < maxDepth)
        Split(node, depth, maxDepth, maxPerLeaf, objects);
}


void OctTree::Split(OctNode* node, int depth, int maxDepth, int maxPerLeaf,
                    const std::vector<SceneObject>& objects)
{
    XMFLOAT3 c = node->bounds.Center;
    XMFLOAT3 e = node->bounds.Extents;
    XMFLOAT3 he = { e.x*0.5f, e.y*0.5f, e.z*0.5f };

    float ox[] = { c.x - he.x, c.x + he.x };
    float oy[] = { c.y - he.y, c.y + he.y };
    float oz[] = { c.z - he.z, c.z + he.z };

    int ci = 0;
    for (int ix=0;ix<2;ix++) for (int iy=0;iy<2;iy++) for (int iz=0;iz<2;iz++)
    {
        node->children[ci] = std::make_unique<OctNode>();
        node->children[ci]->bounds = BoundingBox(
            {ox[ix], oy[iy], oz[iz]}, he);
        ci++;
    }

  
    std::vector<int> remaining;
    for (int idx : node->objectIndices)
    {
        bool placed = false;
        for (auto& child : node->children)
        {
            if (child->bounds.Intersects(objects[idx].aabb))
            {
                child->objectIndices.push_back(idx);
                placed = true;
                
            }
        }

        bool inAny = false;
        for (auto& child : node->children)
            for (int ci2 : child->objectIndices)
                if (ci2 == idx) { inAny = true; break; }
        if (!inAny) remaining.push_back(idx);
    }
    node->objectIndices = remaining;


    for (auto& child : node->children)
        if (child && (int)child->objectIndices.size() > maxPerLeaf && depth+1 < maxDepth)
            Split(child.get(), depth+1, maxDepth, maxPerLeaf, objects);
}


void OctTree::Query(const BoundingFrustum& frustum,
                    std::vector<int>& outIndices) const
{
    if (m_root)
        QueryNode(m_root.get(), frustum, outIndices);
}


void OctTree::QueryNode(const OctNode* node,
                        const BoundingFrustum& frustum,
                        std::vector<int>& out) const
{
    ContainmentType ct = frustum.Contains(node->bounds);
    if (ct == DISJOINT) return;


    for (int i : node->objectIndices)
        out.push_back(i);


    if (!node->IsLeaf())
        for (auto& child : node->children)
            if (child) QueryNode(child.get(), frustum, out);
}
