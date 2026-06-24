#pragma once
#include "DX12Common.h"

class Camera
{
public:
    Camera(float fovY = XM_PIDIV4, float aspect = 16.f/9.f,
           float nearZ = 0.1f, float farZ = 500.f)
        : m_fovY(fovY), m_aspect(aspect), m_near(nearZ), m_far(farZ)
    {
        m_pos    = { 0, 5, -20 };
        m_yaw    = 0.f;
        m_pitch  = 0.f;
        UpdateVectors();
    }

    void SetAspect(float a) { m_aspect = a; }


    void MoveForward (float d) { auto f = GetForward(); m_pos = { m_pos.x+f.x*d, m_pos.y+f.y*d, m_pos.z+f.z*d }; }
    void MoveRight   (float d) { auto r = GetRight();   m_pos = { m_pos.x+r.x*d, m_pos.y+r.y*d, m_pos.z+r.z*d }; }
    void MoveUp      (float d) { m_pos.y += d; }
    void Rotate      (float dyaw, float dpitch)
    {
        m_yaw   += dyaw;
        m_pitch  = std::clamp(m_pitch + dpitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
        UpdateVectors();
    }

    const XMFLOAT3& GetPosition() const { return m_pos; }

    XMMATRIX GetView() const
    {
        XMVECTOR pos    = XMLoadFloat3(&m_pos);
        XMVECTOR target = XMVectorAdd(pos, XMLoadFloat3(&m_forward));
        XMVECTOR up     = { 0,1,0,0 };
        return XMMatrixLookAtLH(pos, target, up);
    }
    XMMATRIX GetProj() const
    {
        return XMMatrixPerspectiveFovLH(m_fovY, m_aspect, m_near, m_far);
    }
    XMMATRIX GetViewProj() const { return GetView() * GetProj(); }

    
    BoundingFrustum GetFrustum() const
    {
        BoundingFrustum frust;
        BoundingFrustum::CreateFromMatrix(frust, GetProj());
        
        XMMATRIX invView = XMMatrixInverse(nullptr, GetView());
        frust.Transform(frust, invView);
        return frust;
    }

    float GetNear() const { return m_near; }
    float GetFar()  const { return m_far; }

private:
    XMFLOAT3 m_pos;
    float    m_yaw, m_pitch;
    float    m_fovY, m_aspect, m_near, m_far;
    XMFLOAT3 m_forward = {0,0,1};

    void UpdateVectors()
    {
        float cosP = cosf(m_pitch);
        m_forward = {
            cosP * sinf(m_yaw),
            sinf(m_pitch),
            cosP * cosf(m_yaw)
        };
    }
    XMFLOAT3 GetForward() const { return m_forward; }
    XMFLOAT3 GetRight() const
    {
        XMVECTOR f = XMLoadFloat3(&m_forward);
        XMVECTOR u = { 0,1,0,0 };
        XMFLOAT3 r;
        XMStoreFloat3(&r, XMVector3Normalize(XMVector3Cross(f, u)));
        return r;
    }
};
