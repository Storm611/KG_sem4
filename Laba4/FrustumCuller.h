#pragma once
#include "DX12Common.h"
#include "Scene.h"
#include "Camera.h"
#include <vector>


struct CullResult
{
    std::vector<int> visibleFull; 
    std::vector<int> visibleBox; 
};

class FrustumCuller
{
public:

    CullResult Cull(const std::vector<SceneObject>& objects,
                    const Camera& camera,
                    bool frustumEnabled,
                    float lodDistanceSq) const;
};
