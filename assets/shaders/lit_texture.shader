// Deferred PBR material: tint * diffuse texture (sRGB texture data).
#include "gbuffer.hlsli"

//@update material
cbuffer material
{
    //@editable
    float4 tint;
    //@editable
    //@range 0 1
    float roughness;
    //@editable
    //@range 0 1
    float metallic;
    //@editable
    float4 emissive;    // rgb color (sRGB), w = strength
}

Texture2D diffuseTexture : register(t0);
SamplerState samplerLinear : register(s0);

struct VS_Input
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    float4 pos = mul(float4(input.position, 1.0f), world);
    output.position = mul(pos, viewProjection);
    output.normal = worldNormal(input.normal);
    output.uv = input.uv;
    return output;
}

GBufferOutput PSMain(PS_Input input)
{
    float4 sample = diffuseTexture.Sample(samplerLinear, input.uv);
    float3 albedo = srgbToLinear(tint.rgb * sample.rgb);
    float3 glow = srgbToLinear(emissive.rgb) * emissive.w;
    return packGBuffer(albedo, metallic, input.normal, roughness, glow);
}
