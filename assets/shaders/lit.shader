// Deferred PBR material: flat color. Colors are authored in sRGB.
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

struct VS_Input
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
};

PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    float4 pos = mul(float4(input.position, 1.0f), world);
    output.position = mul(pos, viewProjection);
    output.normal = worldNormal(input.normal);
    return output;
}

GBufferOutput PSMain(PS_Input input)
{
    float3 albedo = srgbToLinear(tint.rgb);
    float3 glow = srgbToLinear(emissive.rgb) * emissive.w;
    return packGBuffer(albedo, metallic, input.normal, roughness, glow);
}
