// ParticleVS.hlsl
// Reads one particle from the SRV using SV_VertexID, passes to GS

struct Particle
{
    float3 Position;
    float3 Velocity;
    float  Life;
    float  Size;
    float4 Color;
};

StructuredBuffer<Particle> g_Particles : register(t0);

struct VSOut
{
    float4 WorldPos : POSITION;   // world space (GS will project)
    float  Size     : SIZE;
    float4 Color    : COLOR;
};

VSOut main(uint vid : SV_VertexID)
{
    Particle p = g_Particles[vid];

    VSOut o;
    o.WorldPos = float4(p.Position, 1.0f);
    o.Size     = p.Size;
    o.Color    = p.Color;
    return o;
}
