#pragma once
#include <DirectXMath.h>

using namespace DirectX;

enum class LightType : int
{
    Directional = 0,
    Point = 1,
    Spot = 2
};


struct GPULight
{
    XMFLOAT3 Position;   
    float    Range;      

    XMFLOAT3 Direction;  // world-space direction 
    float    Intensity;  // multiplier applied to Color

    XMFLOAT3 Color;      // linear RGB
    float    SpotCosine; // cos(inner cone angle) for spot lights

    int      Type;       // LightType
    float    SpotOuterCosine;
    XMFLOAT2 _pad;
};

static_assert(sizeof(GPULight) % 16 == 0, "GPULight must be 16-byte aligned for HLSL");
