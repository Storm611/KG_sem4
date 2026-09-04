cbuffer MaterialConstants : register(b1)
{
    float3 gDiffuseColor;
    float  gRoughness;
    float  gMetallic;
    float3 gMaterialPad;
};

struct PSInput
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION0;
    float3 NormalW : NORMAL0;
    float2 TexC    : TEXCOORD0;
};

struct PSOutput
{
    float4 Albedo  : SV_TARGET0; 
    float4 Normal  : SV_TARGET1; 
    float4 WorldPos: SV_TARGET2; 
};

PSOutput main(PSInput pin)
{
    PSOutput pout;

    float3 n = normalize(pin.NormalW);

    pout.Albedo = float4(gDiffuseColor, gRoughness);
    pout.Normal = float4(n * 0.5f + 0.5f, gMetallic);
    pout.WorldPos = float4(pin.PosW, 1.0f);

    return pout;
}
