
cbuffer CameraConstants : register(b0)
{
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
struct VSIn  { float3 PosL:POSITION; float3 NormalL:NORMAL; float3 TangentL:TANGENT; float2 TexC:TEXCOORD; };
struct VSOut { float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
VSOut main(VSIn vin) {
    VSOut o;
    o.PosW    = mul(float4(vin.PosL,1),gWorld).xyz;
    o.NormalW = mul(float4(vin.NormalL,0),gWorld).xyz;
    o.TangentW= mul(float4(vin.TangentL,0),gWorld).xyz;
    o.TexC    = vin.TexC;
    return o;
}
