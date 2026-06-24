// ParticleCS.hlsl

struct Particle
{
    float3 Position;
    float3 Velocity;
    float  Life;
    float  Size;
    float4 Color;
};

cbuffer EmitConstants : register(b0)
{
    float3 EmitterPos;
    float  DeltaTime;
    float3 SphereCenter;
    float  SphereRadius;
    uint   MaxParticles;
    uint   EmitCount;
    float  pad0;
    float  pad1;
};

AppendStructuredBuffer<Particle> g_AppendParticles  : register(u0);
StructuredBuffer<Particle>       g_ConsumeParticles : register(t0);


uint WangHash(uint n)
{
    n = (n ^ 61u) ^ (n >> 16u);
    n = n + (n << 3u);
    n = n ^ (n >> 4u);
    n = n * 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return n;
}

float Rand01(uint seed)
{
    return float(WangHash(seed) & 0x00FFFFFFu) / float(0x00FFFFFFu);
}

float3 RandomFireDir(uint seed)
{
    float phi  = Rand01(seed * 3u + 0u) * 6.28318f;
    float cosT = Rand01(seed * 3u + 1u);
    float sinT = sqrt(max(0.0f, 1.0f - cosT * cosT));
    return float3(sinT * cos(phi), cosT, sinT * sin(phi));
}


[numthreads(64, 1, 1)]
void UpdateCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= MaxParticles)
        return;

    Particle p = g_ConsumeParticles[idx];

    if (p.Life <= 0.0f)
        return;

 
    p.Velocity += float3(0.0f, -5.0f, 0.0f) * DeltaTime;


    p.Position += p.Velocity * DeltaTime;


    if (SphereRadius > 0.0f)
    {
        float3 diff = p.Position - SphereCenter;
        float  dist = length(diff);

        if (dist < SphereRadius && dist > 0.0001f)
        {
        
            float3 n   = diff / dist;
            p.Position = SphereCenter + n * SphereRadius;

            
            float vDotN = dot(p.Velocity, n);
            if (vDotN < 0.0f) 
                p.Velocity = p.Velocity - 2.0f * vDotN * n;

            p.Velocity *= 0.65f;
        }
    }


    p.Life -= DeltaTime * 0.4f;

    float t = saturate(p.Life);
    p.Color = lerp(float4(0.1f, 0.0f, 0.0f, 0.0f),
                   float4(1.0f, 0.55f + t * 0.2f, 0.02f, 1.0f),
                   t);
    p.Size  = 0.1f + t * 0.4f;

    if (p.Life > 0.0f)
        g_AppendParticles.Append(p);
}


[numthreads(64, 1, 1)]
void EmitCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= EmitCount)
        return;

    uint seed = WangHash(idx * 7919u + asuint(DeltaTime) * 6271u + asuint(EmitterPos.x) * 3571u);

    Particle p;
    p.Life     = 0.85f + Rand01(seed + 5u) * 0.15f;
    p.Velocity = RandomFireDir(seed) * (3.5f + Rand01(seed + 100u) * 5.0f);
    p.Position = EmitterPos + float3(
        (Rand01(seed + 200u) - 0.5f) * 1.0f,
        (Rand01(seed + 201u) - 0.5f) * 0.3f,
        (Rand01(seed + 202u) - 0.5f) * 1.0f);
    p.Size  = 0.3f + Rand01(seed + 300u) * 0.3f;
    p.Color = float4(1.0f, 0.6f + Rand01(seed + 400u) * 0.3f, 0.05f, 1.0f);

    g_AppendParticles.Append(p);
}
