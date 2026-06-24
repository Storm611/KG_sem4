
cbuffer CameraConstants : register(b0) {
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
struct VSOut { float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
struct HSOut { float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
struct PatchTess { float EdgeTess[3]:SV_TessFactor; float InsideTess:SV_InsideTessFactor; };

float CalcTessFactor(float3 p) {
    float d = distance(p, gCameraPos);
    float t = saturate((d - gTessNearDist) / (gTessFarDist - gTessNearDist));
    return lerp(gTessMax, gTessMin, t);
}
PatchTess PatchHS(InputPatch<VSOut,3> patch, uint id:SV_PrimitiveID) {
    PatchTess pt;
    pt.EdgeTess[0] = CalcTessFactor(0.5*(patch[1].PosW+patch[2].PosW));
    pt.EdgeTess[1] = CalcTessFactor(0.5*(patch[2].PosW+patch[0].PosW));
    pt.EdgeTess[2] = CalcTessFactor(0.5*(patch[0].PosW+patch[1].PosW));
    pt.InsideTess  = CalcTessFactor((patch[0].PosW+patch[1].PosW+patch[2].PosW)/3.0);
    return pt;
}
[domain("tri")][partitioning("fractional_odd")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("PatchHS")]
HSOut main(InputPatch<VSOut,3> p, uint i:SV_OutputControlPointID, uint pid:SV_PrimitiveID) {
    HSOut o; o.PosW=p[i].PosW; o.NormalW=p[i].NormalW; o.TangentW=p[i].TangentW; o.TexC=p[i].TexC; return o;
}
