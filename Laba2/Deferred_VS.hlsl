
struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput vout;
    vout.TexC = float2((vertexID << 1) & 2, vertexID & 2);
    vout.PosH = float4(vout.TexC.x * 2.0f - 1.0f, 1.0f - vout.TexC.y * 2.0f, 0.0f, 1.0f);
    return vout;
}
