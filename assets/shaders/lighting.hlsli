// Shared lighting code for the deferred light pass and lit forward shaders.
// Declares the frame cbuffer (engine semantics), the light list and the
// cluster buffers. A shader that includes this file must not declare another
// "//@update frame" cbuffer: only one per shader is bound by the engine.
#ifndef CSYREN_LIGHTING_HLSLI
#define CSYREN_LIGHTING_HLSLI

static const float PI = 3.14159265f;

//@update frame
cbuffer perFrame
{
    //@semantic ViewProjection
    matrix viewProjection;
    //@semantic View
    matrix view;
    //@semantic InvViewProjection
    matrix invViewProjection;
    //@semantic CameraPosition
    float4 cameraPosition;
    //@semantic ScreenParams
    float4 screenParams;        // width, height, 1/width, 1/height
    //@semantic CameraParams
    float4 cameraParams;        // near, far, projection[0][0], projection[1][1]
    //@semantic LightParams
    float4 lightParams;         // directional count, point count, debug view, clustered
    //@semantic ClusterParams
    float4 clusterParams;       // grid x, y, z, max lights per cluster
    //@semantic ClusterDepthParams
    float4 clusterDepthParams;  // slice scale, slice bias, log2(far/near)
    //@semantic AmbientColor
    float4 ambientColor;        // rgb ambient radiance, w exposure
    //@semantic BackgroundColor
    float4 backgroundColor;
}

// Matches csyren::render::GpuLight (64 bytes).
struct Light
{
    float3 position;
    float  radius;
    float3 color;
    uint   type;        // 0 directional, 1 point
    float3 direction;   // direction the light travels
    float  invRadiusSq;
    float4 extra;
};

StructuredBuffer<Light> lights         : register(t8);
StructuredBuffer<uint>  clusterGrid    : register(t9);
StructuredBuffer<uint>  clusterIndices : register(t10);

// ---------------------------------------------------------------- color space
float3 srgbToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 linearToSrgb(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }

// ---------------------------------------------------------------- clusters
// Screen position in pixels (SV_Position.xy) + view-space depth -> froxel index.
uint clusterIndexOf(float2 screenPos, float viewZ)
{
    const uint gx = uint(clusterParams.x), gy = uint(clusterParams.y), gz = uint(clusterParams.z);
    uint x = min(uint(screenPos.x * screenParams.z * gx), gx - 1);
    uint y = min(uint(screenPos.y * screenParams.w * gy), gy - 1);
    float slice = floor(log2(max(viewZ, 1e-4f)) * clusterDepthParams.x + clusterDepthParams.y);
    uint z = uint(clamp(slice, 0.0f, float(gz - 1)));
    return x + y * gx + z * gx * gy;
}

// ---------------------------------------------------------------- BRDF
float3 shadeLight(float3 N, float3 V, float3 L, float3 radiance, float3 diffuseColor, float3 F0, float roughness)
{
    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0f) return 0.0f;
    float3 H = normalize(L + V);
    float NdotV = max(dot(N, V), 1e-4f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float a = max(roughness * roughness, 0.002f);
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    float D = a2 / (PI * d * d);
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float G = (NdotL / (NdotL * (1.0f - k) + k)) * (NdotV / (NdotV * (1.0f - k) + k));
    float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
    float3 specular = D * G * F / max(4.0f * NdotL * NdotV, 1e-4f);
    float3 kd = 1.0f - F;
    return (kd * diffuseColor / PI + specular) * radiance * NdotL;
}

struct Surface
{
    float3 position;    // world
    float3 normal;      // world, normalized
    float3 albedo;      // linear
    float  metallic;
    float  roughness;
    float3 emissive;    // linear radiance
};

// Full direct + ambient lighting of a surface. screenPos: SV_Position.xy.
float3 computeLighting(Surface s, float2 screenPos, out uint lightsInCluster)
{
    float3 V = normalize(cameraPosition.xyz - s.position);
    float3 F0 = lerp(0.04f.xxx, s.albedo, s.metallic);
    float3 diffuseColor = s.albedo * (1.0f - s.metallic);
    float roughness = clamp(s.roughness, 0.03f, 1.0f);
    float3 result = 0.0f;

    // ambient: hemispherical weight, a little specular reflection on metals
    float hemisphere = 0.75f + 0.25f * s.normal.y;
    result += ambientColor.rgb * hemisphere * (diffuseColor + F0 * 0.25f);
    result += s.emissive;

    const uint directionalCount = uint(lightParams.x);
    const uint pointCount = uint(lightParams.y);
    for (uint i = 0; i < directionalCount; ++i)
    {
        Light light = lights[i];
        result += shadeLight(s.normal, V, -normalize(light.direction), light.color, diffuseColor, F0, roughness);
    }

    lightsInCluster = 0;
    if (lightParams.w > 0.5f)
    {
        float viewZ = mul(float4(s.position, 1.0f), view).z;
        uint cluster = clusterIndexOf(screenPos, viewZ);
        uint count = min(clusterGrid[cluster], uint(clusterParams.w));
        lightsInCluster = count;
        uint base = cluster * uint(clusterParams.w);
        for (uint j = 0; j < count; ++j)
        {
            Light light = lights[clusterIndices[base + j]];
            float3 toLight = light.position - s.position;
            float d2 = dot(toLight, toLight);
            float window = saturate(1.0f - d2 * d2 * light.invRadiusSq * light.invRadiusSq);
            float attenuation = window * window / max(d2, 0.01f);
            result += shadeLight(s.normal, V, toLight * rsqrt(max(d2, 1e-6f)), light.color * attenuation, diffuseColor, F0, roughness);
        }
    }
    else
    {
        // reference path: every point light
        for (uint j = 0; j < pointCount; ++j)
        {
            Light light = lights[directionalCount + j];
            float3 toLight = light.position - s.position;
            float d2 = dot(toLight, toLight);
            if (d2 > light.radius * light.radius) continue;
            float window = saturate(1.0f - d2 * d2 * light.invRadiusSq * light.invRadiusSq);
            float attenuation = window * window / max(d2, 0.01f);
            result += shadeLight(s.normal, V, toLight * rsqrt(max(d2, 1e-6f)), light.color * attenuation, diffuseColor, F0, roughness);
            ++lightsInCluster;
        }
    }
    return result;
}

#endif
