
cbuffer CameraConstants : register(b0) {
    float4x4 gWorld; float4x4 gViewProj; float3 gCameraPos; float gDisplacementScale;
    float gTessMin; float gTessMax; float gTessNearDist; float gTessFarDist;
};
cbuffer LightConstants : register(b1) { float3 gLightDir; float gLightPad; float3 gLightColor; float gAmbient; };
Texture2D gNormalMap:register(t1); Texture2D gAlbedoMap:register(t2); SamplerState gSamLinear:register(s0);
struct DSOut { float4 PosH:SV_POSITION; float3 PosW:POSITION; float3 NormalW:NORMAL; float3 TangentW:TANGENT; float2 TexC:TEXCOORD; };
float4 main(DSOut pin):SV_Target {
    float3 N=normalize(pin.NormalW); float3 T=normalize(pin.TangentW);
    T=normalize(T-dot(T,N)*N); float3 B=cross(N,T);
    float3x3 TBN=float3x3(T,B,N);
    float3 nMap=gNormalMap.Sample(gSamLinear,pin.TexC).rgb*2-1;
    float3 finalN=normalize(mul(nMap,TBN));
    float3 L=normalize(gLightDir); float3 V=normalize(gCameraPos-pin.PosW); float3 H=normalize(L+V);
    float diff=max(dot(finalN,L),0); float spec=pow(max(dot(finalN,H),0),64);
    float3 albedo=gAlbedoMap.Sample(gSamLinear,pin.TexC).rgb;
    albedo=max(albedo,float3(0.6,0.55,0.5));
    float3 color=albedo*(gAmbient+diff*gLightColor)+float3(1,1,1)*spec*0.4;
    return float4(color,1);
}
