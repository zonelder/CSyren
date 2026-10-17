
//@update frame
cbuffer perFrame
{
    //@semantic ViewProjection
   matrix viewProjection;
}

//@update entity
cbuffer perEntity
{
    //@semantic World
    matrix world;
}

struct VS_Input
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

Texture2D diffuseTexture : register(t0);
SamplerState samplerLinear : register(s0);


// --- Vertex GraphicShader ---
PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    float4 pos = float4(input.position, 1.0f);
    pos = mul(pos, world);
    pos = mul(pos, viewProjection);
    output.position = pos;

    output.uv = input.uv;
    return output;
}

// --- Pixel GraphicShader ---
float4 PSMain(PS_Input input) : SV_TARGET
{
    float4 color = diffuseTexture.Sample(samplerLinear, input.uv);
    return color;
}

