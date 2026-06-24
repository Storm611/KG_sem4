// Water_DS.hlsl — анимированные волны через сумму синусоид
cbuffer CameraConstants : register(b0) {
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
cbuffer WaterConstants : register(b2) {
    float gTime; float gWaveHeight; float gWaveFreq; float gWaveSpeed;
};
struct HSOut { float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
struct PatchTess { float EdgeTess[3]:SV_TessFactor; float InsideTess:SV_InsideTessFactor; };
struct DSOut { float4 PosH:SV_POSITION; float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };

// Сумма четырёх синусоид с разными направлениями и частотами
float WaveH(float x, float z, float t) {
    float h = 0;
    h += sin(x * gWaveFreq              + t * gWaveSpeed)       * gWaveHeight;
    h += sin(z * gWaveFreq * 1.3        + t * gWaveSpeed * 0.8) * gWaveHeight * 0.6;
    h += sin((x + z) * gWaveFreq * 0.7 + t * gWaveSpeed * 1.2) * gWaveHeight * 0.4;
    h += sin(x * gWaveFreq * 3.1        - t * gWaveSpeed * 2.0) * gWaveHeight * 0.15;
    return h;
}
// Нормаль — численная производная волновой функции (конечные разности)
float3 WaveNormal(float x, float z, float t) {
    float e = 0.05;
    float dX = WaveH(x+e,z,t) - WaveH(x-e,z,t);
    float dZ = WaveH(x,z+e,t) - WaveH(x,z-e,t);
    return normalize(float3(-dX, 2.0*e, -dZ));
}
[domain("tri")]
DSOut main(PatchTess pt, float3 bary:SV_DomainLocation, const OutputPatch<HSOut,3> tri) {
    DSOut o;
    float3 pos = bary.x*tri[0].PosW + bary.y*tri[1].PosW + bary.z*tri[2].PosW;
    float2 uv  = bary.x*tri[0].TexC + bary.y*tri[1].TexC + bary.z*tri[2].TexC;
    pos.y += WaveH(pos.x, pos.z, gTime);
    float3 N = WaveNormal(pos.x, pos.z, gTime);
    o.PosH=mul(float4(pos,1),gViewProj); o.PosW=pos;
    o.NormalW=N; o.TangentW=float3(1,0,0); o.TexC=uv;
    return o;
}
