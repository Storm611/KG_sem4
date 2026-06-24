#include "FrustumCuller.h"

CullResult FrustumCuller::Cull(const std::vector<SceneObject>& objects,
                                const Camera& camera,
                                bool frustumEnabled,
                                float lodDistanceSq) const
{
    CullResult result;
    result.visibleFull.reserve(objects.size());
    result.visibleBox .reserve(objects.size());

    BoundingFrustum frustum = camera.GetFrustum();
    XMFLOAT3 camPos = camera.GetPosition();
    XMVECTOR vCam   = XMLoadFloat3(&camPos);

    for (int i = 0; i < (int)objects.size(); ++i)
    {
        const SceneObject& obj = objects[i];

        
        if (frustumEnabled)
        {
            ContainmentType ct = frustum.Contains(obj.aabb);
            if (ct == DISJOINT) continue;
        }

 
        XMVECTOR vObj = XMLoadFloat3(&obj.position);
        float distSq  = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vObj, vCam)));

        if (distSq > lodDistanceSq)
            result.visibleBox .push_back(i);
        else
            result.visibleFull.push_back(i);
    }

    return result;
}
