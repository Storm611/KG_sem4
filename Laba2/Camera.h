#pragma once
#include <DirectXMath.h>

using namespace DirectX;

class Camera
{
public:
    Camera() = default;

    void SetLens(float fovYRad, float aspect, float nearZ, float farZ);
    void SetPosition(const XMFLOAT3& pos) { m_position = pos; }
    const XMFLOAT3& GetPosition() const { return m_position; }

    void Walk(float d);  
    void Strafe(float d); 
    void Ascend(float d); 

    
    void Rotate(float yawDelta, float pitchDelta);

    
    void SetOrientation(float yaw, float pitch) { m_yaw = yaw; m_pitch = pitch; }

    void UpdateViewMatrix();

    XMMATRIX GetView() const { return XMLoadFloat4x4(&m_view); }
    XMMATRIX GetProj() const { return XMLoadFloat4x4(&m_proj); }
    XMMATRIX GetViewProj() const { return XMMatrixMultiply(GetView(), GetProj()); }

    XMFLOAT3 GetRight() const { return m_right; }
    XMFLOAT3 GetUp() const { return m_up; }

private:
    XMFLOAT3 m_position = { 0.0f, 2.0f, -5.0f };
    XMFLOAT3 m_right = { 1.0f, 0.0f, 0.0f };
    XMFLOAT3 m_up = { 0.0f, 1.0f, 0.0f };
    XMFLOAT3 m_look = { 0.0f, 0.0f, 1.0f };

    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_nearZ = 0.1f;
    float m_farZ = 500.0f;
    float m_fovY = XM_PIDIV4;
    float m_aspect = 16.0f / 9.0f;

    XMFLOAT4X4 m_view = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    XMFLOAT4X4 m_proj = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
};
