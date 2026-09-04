
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

Texture2D gAlbedoTex   : register(t0); 
Texture2D gNormalTex   : register(t1); 
Texture2D gWorldPosTex : register(t2);
StructuredBuffer<GPULight> gLights : register(t3);

SamplerState gPointSampler : register(s0);

cbuffer FrameConstants : register(b0)
{
    float3 gCameraPosW;
    uint   gLightCount;
    float3 gAmbientColor;
    float  gPad;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

float3 ApplyLight(GPULight light, float3 posW, float3 N, float3 V, float3 albedo, float roughness, float metallic)
{
    float3 L;
    float attenuation = 1.0f;

    if (light.Type == 0) 
    {
        L = normalize(-light.Direction);
    }
    else 
    {
        float3 toLight = light.Position - posW;

        float distSq = dot(toLight, toLight);
        float rangeSq = light.Range * light.Range;
        if (distSq > rangeSq)
            return float3(0, 0, 0);

        float dist = sqrt(distSq);
        L = toLight / max(dist, 1e-4f);


        float falloff = saturate(1.0f - pow(dist / max(light.Range, 1e-3f), 4.0f));
        attenuation = (falloff * falloff) / max(dist * dist, 1e-2f);

        if (light.Type == 2) 
        {
            float cosAngle = dot(-L, normalize(light.Direction));
            float spotFactor = saturate((cosAngle - light.SpotOuterCosine) /
                                         max(light.SpotCosine - light.SpotOuterCosine, 1e-4f));
            attenuation *= spotFactor * spotFactor;
        }
    }

    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0f || attenuation <= 0.0f)
        return float3(0, 0, 0);


    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float shininess = lerp(8.0f, 128.0f, 1.0f - roughness);
    float specStrength = lerp(0.04f, 1.0f, metallic);
    float spec = pow(NdotH, shininess) * specStrength;

    float3 diffuse = albedo * (1.0f - metallic);
    float3 radiance = light.Color * light.Intensity * attenuation;

    return (diffuse * NdotL + spec) * radiance;
}

float4 main(PSInput pin) : SV_TARGET
{
    int3 coord = int3(pin.PosH.xy, 0);

    float4 albedoSample = gAlbedoTex.Load(coord);
    float4 normalSample = gNormalTex.Load(coord);
    float4 posSample = gWorldPosTex.Load(coord);

    float3 albedo = albedoSample.rgb;
    float roughness = albedoSample.a;
    float3 N = normalize(normalSample.rgb * 2.0f - 1.0f);
    float metallic = normalSample.a;
    float3 posW = posSample.rgb;

  
    if (posSample.a == 0.0f)
        return float4(0.02f, 0.02f, 0.03f, 1.0f); 

    float3 V = normalize(gCameraPosW - posW);

    float3 color = albedo * gAmbientColor;

    for (uint i = 0; i < gLightCount; i++)
    {
        color += ApplyLight(gLights[i], posW, N, V, albedo, roughness, metallic);
    }

    return float4(color, 1.0f);
}
