#pragma once


static const char* g_ShaderSrc = R"HLSL(

// ---- Constant buffers ----
cbuffer FrameCB : register(b0)
{
    float4x4 gViewProj;
    float4   gCameraPos;
    float4   gLightDir;
    float    gLodDistanceSq;
    float3   _pad;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4   gColor;
    int      gIsBox;
    float3   _padObj;
};

// ---- Vertex shader ----
struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct PSIn
{
    float4 posH  : SV_POSITION;
    float3 posW  : WORLDPOS;
    float3 norm  : NORMAL;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};

PSIn VS(VSIn vin)
{
    PSIn vout;
    float4 posW = mul(float4(vin.pos, 1.0f), gWorld);
    vout.posH   = mul(posW, gViewProj);
    vout.posW   = posW.xyz;
    vout.norm   = normalize(mul(float4(vin.normal, 0.0f), gWorld).xyz);
    vout.uv     = vin.uv;
    vout.color  = gColor;
    return vout;
}

// ---- Pixel shader ----
float4 PS(PSIn pin) : SV_Target
{
    float3 lightDir = normalize(-gLightDir.xyz);
    float  ndotl    = saturate(dot(pin.norm, lightDir));
    float3 ambient  = pin.color.rgb * 0.25f;
    float3 diffuse  = pin.color.rgb * ndotl;

    // LOD boxes get a slight wireframe tint overlay
    float3 finalColor = ambient + diffuse;
    if (gIsBox)
        finalColor = lerp(finalColor, float3(1,0.6,0.1), 0.25f);

    return float4(finalColor, 1.0f);
}

)HLSL";


static const char* g_WireShaderSrc = R"HLSL(

cbuffer FrameCB : register(b0)
{
    float4x4 gViewProj;
    float4   gCameraPos;
    float4   gLightDir;
    float    gLodDistanceSq;
    float3   _pad;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4   gColor;
    int      gIsBox;
    float3   _padObj;
};

struct VSIn { float3 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };
struct PSIn { float4 posH : SV_POSITION; float4 color : COLOR; };

PSIn VS(VSIn vin)
{
    PSIn vout;
    float4 posW = mul(float4(vin.pos,1), gWorld);
    vout.posH   = mul(posW, gViewProj);
    vout.color  = gColor;
    return vout;
}

float4 PS(PSIn pin) : SV_Target { return pin.color; }

)HLSL";
