// SpherePS.hlsl
struct VSOut
{
    float4 Pos   : SV_Position;
    float4 Color : COLOR;
};

float4 main(VSOut i) : SV_Target
{
    return i.Color;
}
