// Shared declarations for GBuffer (deferred opaque) material shaders.
// A material shader is "deferred" when its pixel shader writes SV_TARGET0..2
// (MeshRenderSystem draws it into the GBuffer; the light pass shades it).
//   target0: albedo.rgb (linear), metallic
//   target1: world normal.xyz, roughness
//   target2: emissive.rgb (linear radiance)
#ifndef CSYREN_GBUFFER_HLSLI
#define CSYREN_GBUFFER_HLSLI

//@update frame
cbuffer perFrame
{
    //@semantic ViewProjection
    matrix viewProjection;
    //@semantic CameraPosition
    float4 cameraPosition;
}

//@update entity
cbuffer perEntity
{
    //@semantic World
    matrix world;
    //@semantic WorldNormal
    matrix worldNormalMatrix;   // inverse-transpose of world (non-uniform scale)
}

struct GBufferOutput
{
    float4 target0 : SV_TARGET0;
    float4 target1 : SV_TARGET1;
    float4 target2 : SV_TARGET2;
};

float3 srgbToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }

GBufferOutput packGBuffer(float3 albedoLinear, float metallic, float3 normal, float roughness, float3 emissiveLinear)
{
    GBufferOutput o;
    o.target0 = float4(saturate(albedoLinear), saturate(metallic));
    o.target1 = float4(normalize(normal), saturate(roughness));
    o.target2 = float4(max(emissiveLinear, 0.0f), 0.0f);
    return o;
}

// World-space normal for a (possibly non-uniformly scaled) entity.
float3 worldNormal(float3 normal)
{
    return normalize(mul(float4(normal, 0.0f), worldNormalMatrix).xyz);
}

#endif
