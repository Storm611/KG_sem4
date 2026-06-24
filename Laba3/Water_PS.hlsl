
cbuffer CameraConstants : register(b0) {
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
cbuffer LightConstants : register(b1) { float3 gLightDir; float gPad; float3 gLightColor; float gAmbient; };
struct DSOut { float4 PosH:SV_POSITION; float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
float4 main(DSOut pin):SV_Target {
    float3 N=normalize(pin.NormalW);
    float3 L=normalize(gLightDir);
    float3 V=normalize(gCameraPos-pin.PosW);
    float3 H=normalize(L+V);
    float diff = max(dot(N,L), 0.0);
    float spec = pow(max(dot(N,H), 0.0), 128.0);

    float fresnel = pow(1.0 - saturate(dot(N,V)), 2.0);
    float3 deep    = float3(0.02, 0.08, 0.25); 
    float3 shallow = float3(0.10, 0.35, 0.55);
    float3 waterColor = lerp(shallow, deep, fresnel);
    float3 color = waterColor * (gAmbient + diff * gLightColor)
                 + float3(1,1,1) * spec * 0.9;
    return float4(color, 0.88);
}
