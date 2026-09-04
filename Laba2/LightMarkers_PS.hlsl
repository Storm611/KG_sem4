struct PSInput
{
    float4 PosH    : SV_POSITION;
    float2 LocalUV : TEXCOORD0;
    float3 Color   : COLOR0;
};

float4 main(PSInput pin) : SV_TARGET
{
    float d = length(pin.LocalUV);
    float alpha = saturate(1.0f - d);
    alpha *= alpha; 

    if (alpha <= 0.001f)
        discard;

 
    return float4(pin.Color * alpha, alpha);
}
