#pragma once
#include <DirectXMath.h>
using namespace DirectX;


struct ObjectConstants
{
    XMFLOAT4X4 World;
    XMFLOAT4X4 WorldViewProj;
};


struct MaterialConstants
{
    XMFLOAT3 DiffuseColor;
    float Roughness;
    float Metallic;
    XMFLOAT3 _pad;
};


struct FrameConstants
{
    XMFLOAT3 CameraPosW;
    UINT     LightCount;
    XMFLOAT3 AmbientColor;
    float    _pad;
};


struct MarkerConstants
{
    XMFLOAT4X4 ViewProj;
    XMFLOAT3   CameraRight;
    float      MarkerSize;
    XMFLOAT3   CameraUp;
    float      _pad;
};
