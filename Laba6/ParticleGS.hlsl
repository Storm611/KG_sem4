// ParticleGS.hlsl
// Expands each point particle into a camera-facing quad (billboard)

cbuffer CameraConstants : register(b0)
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float3   CameraPos;
    float    pad;
};

struct VSOut
{
    float4 WorldPos : POSITION;
    float  Size     : SIZE;
    float4 Color    : COLOR;
};

struct GSOut
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD;
    float4 Color    : COLOR;
};

[maxvertexcount(4)]
void main(point VSOut input[1], inout TriangleStream<GSOut> stream)
{
    float4 worldPos = input[0].WorldPos;
    float  halfSize = input[0].Size * 0.5f;
    float4 color    = input[0].Color;

    // Camera right and up in world space (from view matrix rows)
    // View is row-major after transpose, so:
    //   right = View[0].xyz  (first row, but stored transposed)
    //   up    = View[1].xyz
    float3 right = float3(View[0][0], View[1][0], View[2][0]);
    float3 up    = float3(View[0][1], View[1][1], View[2][1]);

    // Four corners of billboard quad
    //   TL, TR, BL, BR
    float3 corners[4] =
    {
        worldPos.xyz - right * halfSize + up * halfSize,   // top-left
        worldPos.xyz + right * halfSize + up * halfSize,   // top-right
        worldPos.xyz - right * halfSize - up * halfSize,   // bottom-left
        worldPos.xyz + right * halfSize - up * halfSize    // bottom-right
    };

    float2 uvs[4] =
    {
        float2(0, 0),
        float2(1, 0),
        float2(0, 1),
        float2(1, 1)
    };

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        GSOut o;
        o.Position = mul(float4(corners[i], 1.0f), ViewProj);
        o.TexCoord = uvs[i];
        o.Color    = color;
        stream.Append(o);
    }
    stream.RestartStrip();
}
