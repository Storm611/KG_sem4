// Camera.cpp
#include "Camera.h"
#include <cmath>

Camera::Camera()
    : position(0.0f, 5.0f, -10.0f),
    worldUp(0.0f, 1.0f, 0.0f) {
    // Направляем камеру в центр
    XMVECTOR posVec = XMLoadFloat3(&position);
    XMFLOAT3 zero(0.0f, 0.0f, 0.0f);
    XMVECTOR targetVec = XMLoadFloat3(&zero);
    XMVECTOR frontVec = XMVectorSubtract(targetVec, posVec);
    XMStoreFloat3(&front, XMVector3Normalize(frontVec));

    UpdateVectors();
}

Camera::Camera(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& up)
    : position(position), worldUp(up) {
    XMVECTOR posVec = XMLoadFloat3(&position);
    XMVECTOR targetVec = XMLoadFloat3(&target);
    XMVECTOR frontVec = XMVectorSubtract(targetVec, posVec);
    XMStoreFloat3(&front, XMVector3Normalize(frontVec));

    UpdateVectors();
}

void Camera::SetPosition(const XMFLOAT3& pos) {
    position = pos;
    UpdateViewMatrix();
}

void Camera::SetLookAt(const XMFLOAT3& target) {
    XMVECTOR posVec = XMLoadFloat3(&position);
    XMVECTOR targetVec = XMLoadFloat3(&target);
    XMVECTOR frontVec = XMVectorSubtract(targetVec, posVec);
    XMStoreFloat3(&front, XMVector3Normalize(frontVec));

    UpdateVectors();
}

void Camera::SetPerspective(float fov, float aspect, float nearZ, float farZ) {
    projectionMatrix = XMMatrixPerspectiveFovLH(fov, aspect, nearZ, farZ);
}

XMMATRIX Camera::GetViewMatrix() const {
    return viewMatrix;
}

XMMATRIX Camera::GetProjectionMatrix() const {
    return projectionMatrix;
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset) {
    const float sensitivity = 0.002f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    float yaw = atan2f(front.z, front.x) + xoffset;
    float lenXZ = sqrtf(front.x * front.x + front.z * front.z);
    float pitch = atan2f(front.y, lenXZ) + yoffset;

    // Ограничение pitch
    if (pitch > XM_PIDIV2 - 0.01f) pitch = XM_PIDIV2 - 0.01f;
    if (pitch < -XM_PIDIV2 + 0.01f) pitch = -XM_PIDIV2 + 0.01f;

    // Обновляем вектор направления
    front.x = cosf(pitch) * cosf(yaw);
    front.y = sinf(pitch);
    front.z = cosf(pitch) * sinf(yaw);

    UpdateVectors();
}

void Camera::ProcessKeyboard(float velocity) {
    // Вычисляем горизонтальное направление (без вертикальной составляющей)
    XMFLOAT3 frontHoriz(front.x, 0.0f, front.z);
    XMVECTOR frontNorm = XMVector3Normalize(XMLoadFloat3(&frontHoriz));
    XMFLOAT3 frontXZ;
    XMStoreFloat3(&frontXZ, frontNorm);

    // Вычисляем вектор вправо
    XMVECTOR rightVec = XMVector3Cross(XMLoadFloat3(&front), XMLoadFloat3(&worldUp));
    XMFLOAT3 right;
    XMStoreFloat3(&right, rightVec);

    // Обработка клавиш (как в вашем примере)
    if (GetAsyncKeyState('W') & 0x8000) {
        position.x += frontXZ.x * velocity;
        position.z += frontXZ.z * velocity;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        position.x -= frontXZ.x * velocity;
        position.z -= frontXZ.z * velocity;
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        position.x -= right.x * velocity;
        position.z -= right.z * velocity;
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        position.x += right.x * velocity;
        position.z += right.z * velocity;
    }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
        position.y += velocity;
    }
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000 || GetAsyncKeyState(VK_RSHIFT) & 0x8000) {
        position.y -= velocity;
    }

    UpdateViewMatrix();
}

void Camera::UpdateVectors() {
    // Нормализуем вектор направления
    XMVECTOR frontVec = XMLoadFloat3(&front);
    frontVec = XMVector3Normalize(frontVec);
    XMStoreFloat3(&front, frontVec);

    // Вычисляем вектор вправо
    XMVECTOR worldUpVec = XMLoadFloat3(&worldUp);
    XMVECTOR rightVec = XMVector3Cross(frontVec, worldUpVec);
    rightVec = XMVector3Normalize(rightVec);
    XMStoreFloat3(&right, rightVec);

    // Пересчитываем вектор вверх (для точности)
    XMVECTOR upVec = XMVector3Cross(rightVec, frontVec);
    upVec = XMVector3Normalize(upVec);
    XMStoreFloat3(&up, upVec);

    UpdateViewMatrix();
}

void Camera::UpdateViewMatrix() {
    XMVECTOR eye = XMLoadFloat3(&position);
    XMVECTOR at = XMVectorAdd(eye, XMLoadFloat3(&front));
    XMVECTOR upVec = XMLoadFloat3(&up);

    viewMatrix = XMMatrixLookAtLH(eye, at, upVec);
}