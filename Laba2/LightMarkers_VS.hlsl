
struct GPULight
{
    float3 Position;
    float  Range;

    float3 Direction;
    float  Intensity;

    float3 Color;
    float  SpotCosine;

    int    Type; // 0 = Directional, 1 = Point, 2 = Spot
    float  SpotOuterCosine;
    float2 _pad;
};

StructuredBuffer<GPULight> gLights : register(t0);

cbuffer MarkerConstants : register(b0)
{
    float4x4 gViewProj;
    float3   gCameraRight;
    float    gMarkerSize;
    float3   gCameraUp;
    float    gPad;
};

struct VSOutput
{
    float4 PosH    : SV_POSITION;
    float2 LocalUV : TEXCOORD0;
    float3 Color   : COLOR0;
};


static const float2 kCorners[6] = {
    float2(-1, -1), float2(1, -1), float2(1, 1),
    float2(-1, -1), float2(1,  1), float2(-1, 1)
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    GPULight light = gLights[instanceID];
    VSOutput vout;


    float sizeScale = (light.Type == 0) ? 0.0f : 1.0f;

    float2 corner = kCorners[vertexID];
    float3 worldPos = light.Position
        + gCameraRight * corner.x * gMarkerSize * sizeScale
        + gCameraUp    * corner.y * gMarkerSize * sizeScale;

    vout.PosH = mul(float4(worldPos, 1.0f), gViewProj);
    vout.LocalUV = corner;

    vout.Color = light.Color * saturate(light.Intensity * 0.12f + 0.5f);
    return vout;
}
