//@update frame
cbuffer perFrame
{
    //@semantic ViewProjection
    matrix viewProjection;
    //@semantic Time
    float time;
    //@semantic LightDirection
    float4 lightDir;
}

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
    float4 tint;
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
    PS_Input o;
    float4 pos = float4(input.position, 1.0f);
    pos = mul(pos, world);
    pos = mul(pos, viewProjection);
    o.position = pos;
    o.normal = mul(input.normal,(float3x3)world);
    o.normal = normalize(o.normal);
    return o;
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    float3 L = normalize(-lightDir);
    float3 N = normalize(input.normal);

    // diffuse
    float NdotL = max(dot(N, L), 0.0f);

    // базовый цвет
    float3 c = tint.rgb * NdotL;

    return float4(c, 1.0f);
}

