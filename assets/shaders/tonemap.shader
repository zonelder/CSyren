// HDR resolve: exposure -> ACES (Narkowicz fit) -> gamma 2.2 into scene.color.
// Debug views 1..6 of the light pass are already display-ready and pass through.
//@update frame
cbuffer perFrame
{
    //@semantic LightParams
    float4 lightParams;     // z = debug view
    //@semantic AmbientColor
    float4 ambientColor;    // w = exposure
}

Texture2D<float4> hdrTexture : register(t0);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PS_Input VSMain(uint id : SV_VertexID)
{
    PS_Input output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    output.uv = uv;
    return output;
}

float3 aces(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    float4 hdr = hdrTexture.Load(int3(int2(input.position.xy), 0));
    const int debugView = int(lightParams.z);
    if (debugView >= 1 && debugView <= 6) return float4(saturate(hdr.rgb), 1.0f);
    float exposure = max(ambientColor.w, 0.0f);
    float3 mapped = aces(hdr.rgb * exposure);
    return float4(pow(mapped, 1.0f / 2.2f), 1.0f);
}
