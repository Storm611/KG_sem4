// ParticlePS.hlsl
// Renders a soft circular sprite (procedural texture) for fire/sparks

struct GSOut
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD;
    float4 Color    : COLOR;
};

float4 main(GSOut input) : SV_Target
{
    // UV in [-1,1]
    float2 uv = input.TexCoord * 2.0f - 1.0f;
    float  d  = dot(uv, uv);  // distance^2 from center

    // Discard outside circle
    if (d > 1.0f)
        discard;

    // Soft radial falloff (Gaussian-like)
    float alpha = exp(-d * 3.0f);

    // Inner glow: brighter at center
    float glow = saturate(1.0f - d * 1.5f);

    float4 col = input.Color;
    col.rgb += float3(0.4f, 0.2f, 0.0f) * glow;  // inner hot spot
    col.a   *= alpha;

    return col;
}
