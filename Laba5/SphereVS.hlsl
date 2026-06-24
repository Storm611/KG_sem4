
cbuffer CameraConstants : register(b0)
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float3   CameraPos;
    float    pad;
};

cbuffer SphereParams : register(b1)
{
    float3 SphereCenter;
    float  SphereRadius;
};



static const int SEGS  = 32;   
static const int RINGS = 8;    
static const int MERIDS= 8;    

static const float PI = 3.14159265f;

struct VSOut
{
    float4 Pos   : SV_Position;
    float4 Color : COLOR;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;
    o.Color = float4(0.3f, 0.7f, 1.0f, 0.5f); 



    int ringVerts   = RINGS  * SEGS * 2;
    int meridVerts  = MERIDS * SEGS * 2;

    float3 pos = float3(0,0,0);

    if ((int)vid < ringVerts)
    {
    
        int pairIdx = vid / 2;
        int seg     = pairIdx % SEGS;
        int ring    = pairIdx / SEGS;


        float lat = (-80.0f + (ring + 0.5f) * (160.0f / RINGS)) * (PI / 180.0f);
        float cosLat = cos(lat);
        float sinLat = sin(lat);

        int endpoint = vid & 1; 
        float lon0 = (seg + endpoint) * (2.0f * PI / SEGS);

        pos = float3(cosLat * cos(lon0), sinLat, cosLat * sin(lon0)) * SphereRadius + SphereCenter;
    }
    else
    {

        int adjustedVid = vid - ringVerts;
        int pairIdx = adjustedVid / 2;
        int seg     = pairIdx % SEGS;
        int merid   = pairIdx / SEGS;

        float lon    = merid * (2.0f * PI / MERIDS);
        float cosLon = cos(lon);
        float sinLon = sin(lon);

        int endpoint = adjustedVid & 1;
        float lat = (-PI/2.0f) + (seg + endpoint) * (PI / SEGS);

        pos = float3(cos(lat) * cosLon, sin(lat), cos(lat) * sinLon) * SphereRadius + SphereCenter;
    }

    o.Pos = mul(float4(pos, 1.0f), ViewProj);
    return o;
}
