cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
};

struct VSInput
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct VSOutput
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION0;
    float3 NormalW : NORMAL0;
    float2 TexC    : TEXCOORD0;
};

VSOutput main(VSInput vin)
{
    VSOutput vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3)gWorld));
    vout.TexC = vin.TexC;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

    return vout;
}
