
cbuffer CameraConstants : register(b0) {
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
Texture2D gDisplacementMap:register(t0); SamplerState gSamLinear:register(s0);
struct HSOut { float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
struct PatchTess { float EdgeTess[3]:SV_TessFactor; float InsideTess:SV_InsideTessFactor; };
struct DSOut { float4 PosH:SV_POSITION; float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
[domain("tri")]
DSOut main(PatchTess pt, float3 bary:SV_DomainLocation, const OutputPatch<HSOut,3> tri) {
    DSOut o;
    float3 pos = bary.x*tri[0].PosW + bary.y*tri[1].PosW + bary.z*tri[2].PosW;
    float3 nor = normalize(bary.x*tri[0].NormalW + bary.y*tri[1].NormalW + bary.z*tri[2].NormalW);
    float3 tan = bary.x*tri[0].TangentW+ bary.y*tri[1].TangentW+ bary.z*tri[2].TangentW;
    float2 uv  = bary.x*tri[0].TexC   + bary.y*tri[1].TexC   + bary.z*tri[2].TexC;
    float h = gDisplacementMap.SampleLevel(gSamLinear, uv, 0).r;
    pos += nor * (h * gDisplacementScale);
    o.PosH=mul(float4(pos,1),gViewProj); o.PosW=pos; o.NormalW=nor; o.TangentW=tan; o.TexC=uv;
    return o;
}
