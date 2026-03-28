// Camera.h
#pragma once

#include <Windows.h>
#include <DirectXMath.h>

using namespace DirectX;

class Camera {
public:
    Camera();
    Camera(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& up);

    void SetPosition(const XMFLOAT3& pos);
    void SetLookAt(const XMFLOAT3& target);
    void SetPerspective(float fov, float aspect, float nearZ, float farZ);

    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix() const;
    XMFLOAT3 GetPosition() const { return position; }
    XMFLOAT3 GetFront() const { return front; }
    XMFLOAT3 GetUp() const { return up; }

    void ProcessMouseMovement(float xoffset, float yoffset);
    void ProcessKeyboard(float velocity); // Убрали deltaTime, так как velocity уже включает её

private:
    XMFLOAT3 position;
    XMFLOAT3 front;
    XMFLOAT3 up;
    XMFLOAT3 right;
    XMFLOAT3 worldUp;

    XMMATRIX viewMatrix;
    XMMATRIX projectionMatrix;

    void UpdateViewMatrix();
    void UpdateVectors();
};