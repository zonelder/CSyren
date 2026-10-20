// Forward PBR material (single target): used for transparent surfaces that
// cannot go through the GBuffer. Lit by the same light list / clusters as the
// deferred light pass. Pair it with a blended MaterialStateDesc.
#include "lighting.hlsli"

//@update entity
cbuffer perEntity
{
    //@semantic World
    matrix world;
}

//@update material
cbuffer material
{
    //@editable
    float4 tint;        // sRGB color, alpha = opacity
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
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
};

PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    float4 pos = mul(float4(input.position, 1.0f), world);
    output.worldPos = pos.xyz;
    output.position = mul(pos, viewProjection);
    output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    return output;
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    Surface s;
    s.position = input.worldPos;
    s.normal = normalize(input.normal);
    // two-sided: face the camera
    if (dot(s.normal, cameraPosition.xyz - s.position) < 0.0f) s.normal = -s.normal;
    s.albedo = srgbToLinear(tint.rgb);
    s.metallic = metallic;
    s.roughness = roughness;
    s.emissive = srgbToLinear(emissive.rgb) * emissive.w;
    uint count;
    float3 color = computeLighting(s, input.position.xy, count);
    return float4(color, tint.a);
}
