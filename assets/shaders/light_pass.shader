// Deferred light pass: GBuffer + depth -> linear HDR radiance (scene.hdr).
// Drawn as one fullscreen triangle by LightPassSystem.
#include "lighting.hlsli"

Texture2D<float4> gbuffer0     : register(t0); // albedo, metallic
Texture2D<float4> gbuffer1     : register(t1); // normal, roughness
Texture2D<float4> gbuffer2     : register(t2); // emissive
Texture2D<float>  depthTexture : register(t3); // reverse-Z

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

float3 heat(float t)
{
    // blue -> green -> yellow -> red
    t = saturate(t);
    return saturate(float3(t * 2.0f - 0.5f, 1.0f - abs(t * 2.0f - 1.0f), 1.0f - t * 2.0f));
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    int3 pixel = int3(int2(input.position.xy), 0);
    float depth = depthTexture.Load(pixel);
    const int debugView = int(lightParams.z);
    if (depth <= 0.0f)
    {
        // nothing was drawn here: camera background (sky)
        if (debugView == 5) return float4(0, 0, 0, 1);
        return debugView != 0 ? float4(backgroundColor.rgb, 1.0f) : float4(srgbToLinear(backgroundColor.rgb), 1.0f);
    }

    float4 g0 = gbuffer0.Load(pixel);
    float4 g1 = gbuffer1.Load(pixel);
    float4 g2 = gbuffer2.Load(pixel);

    // reconstruct world position from depth
    float2 ndc = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(clip, invViewProjection);
    world /= world.w;

    Surface s;
    s.position = world.xyz;
    s.normal = normalize(g1.xyz);
    s.albedo = g0.rgb;
    s.metallic = g0.a;
    s.roughness = g1.a;
    s.emissive = g2.rgb;

    if (debugView == 7) { s.albedo = 0.6f.xxx; s.metallic = 0.0f; s.emissive = 0.0f; }

    uint lightsInCluster;
    float3 color = computeLighting(s, input.position.xy, lightsInCluster);

    switch (debugView)
    {
    case 1: return float4(linearToSrgb(s.albedo), 1.0f);
    case 2: return float4(s.normal * 0.5f + 0.5f, 1.0f);
    case 3: return float4(s.roughness, s.metallic, 0.0f, 1.0f);
    case 4: { float viewZ = mul(float4(s.position, 1.0f), view).z; return float4((viewZ / cameraParams.y).xxx, 1.0f); }
    case 5: return float4(heat(float(lightsInCluster) / 16.0f), 1.0f);
    case 6: return float4(linearToSrgb(s.emissive), 1.0f);
    default: break;
    }
    return float4(color, 1.0f);
}
