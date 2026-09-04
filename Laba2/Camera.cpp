#include "Camera.h"
#include <algorithm>

void Camera::SetLens(float fovYRad, float aspect, float nearZ, float farZ)
{
    m_fovY = fovYRad;
    m_aspect = aspect;
    m_nearZ = nearZ;
    m_farZ = farZ;
    XMMATRIX p = XMMatrixPerspectiveFovLH(m_fovY, m_aspect, m_nearZ, m_farZ);
    XMStoreFloat4x4(&m_proj, p);
}

void Camera::Walk(float d)
{
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR l = XMLoadFloat3(&m_look);
    XMVECTOR p = XMLoadFloat3(&m_position);
    XMStoreFloat3(&m_position, XMVectorMultiplyAdd(s, l, p));
}

void Camera::Strafe(float d)
{
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR r = XMLoadFloat3(&m_right);
    XMVECTOR p = XMLoadFloat3(&m_position);
    XMStoreFloat3(&m_position, XMVectorMultiplyAdd(s, r, p));
}

void Camera::Ascend(float d)
{
    m_position.y += d;
}

void Camera::Rotate(float yawDelta, float pitchDelta)
{
    m_yaw += yawDelta;
    m_pitch += pitchDelta;
    m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
}

void Camera::UpdateViewMatrix()
{
    XMVECTOR look = XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f);
    look = XMVector3Normalize(look);

    XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, look));
    XMVECTOR up = XMVector3Cross(look, right);

    XMStoreFloat3(&m_look, look);
    XMStoreFloat3(&m_right, right);
    XMStoreFloat3(&m_up, up);

    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMMATRIX view = XMMatrixLookToLH(pos, look, up);
    XMStoreFloat4x4(&m_view, view);
}
