
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

//@update material
cbuffer material
{
    //@editable 
    float4 tint;
}

struct VS_Input
{
    float3 position : POSITION;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

// --- Vertex GraphicShader ---
PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    float4 pos = float4(input.position, 1.0f);
    pos = mul(pos, world);
    pos = mul(pos, viewProjection);

    output.position = pos;
    return output;
}

// --- Pixel GraphicShader ---
float4 PSMain(PS_Input input) : SV_TARGET
{
    return tint;
}

