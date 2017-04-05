#include "TerrainTessellation.hlsli"

float ComputeWaterDisplacement(float3 worldPos)
{
    const float2 texUV = worldPos.xz + float2(g_TextureWorldOffset.x, -g_TextureWorldOffset.z) * g_tileWorldSize.y;
    float height = 0;
    float heightScale = 1;
    float2 texcoord = texUV * g_WaterConstants.z;
    float2 variance = float2(1, 1);
    float2 timeoffset = float2(g_WaterConstants.y * 1.0f, g_WaterConstants.y * 0.7f);
    for (int i = 0; i < 5; ++i)
    {
        float2 CurrentTexCoord = texcoord + variance * timeoffset;
        float4 TexSample = g_WaterBumpMap.SampleLevel(SamplerRepeatLinear, CurrentTexCoord, 0).rbga;
        height += (TexSample.w - 0.5) * heightScale;
        heightScale *= 0.65;
        texcoord *= 1.4;
        variance.x *= -1;
    }
    return height * g_WaterConstants.w;
}

float3 TessellatedWaterPos(HS_CONSTANT_DATA_OUTPUT input,
    float2 UV : SV_DomainLocation,
    const OutputPatch<HS_OUTPUT, 4> terrainQuad)
{
    // bilerp the position
    float3 worldPos = Bilerp(terrainQuad[0].vPosition, terrainQuad[1].vPosition, terrainQuad[2].vPosition, terrainQuad[3].vPosition, UV);

    const int mipLevel = 0;

    float height = g_WaterConstants.x;
    worldPos.y += height;

    float wh = ComputeWaterDisplacement(worldPos);
    worldPos.y += wh;

    return worldPos;
}

[domain("quad")]
MeshVertex WaterPatchDS(HS_CONSTANT_DATA_OUTPUT input,
    float2 UV : SV_DomainLocation,
    const OutputPatch<HS_OUTPUT, 4> terrainQuad,
    uint PatchID : SV_PrimitiveID)
{
    MeshVertex Output = (MeshVertex)0;

    const float3 worldPos = TessellatedWaterPos(input, UV, terrainQuad);
    Output.vPosition = mul(float4(worldPos.xyz, 1), g_WorldViewProj);
    Output.debugColour = lerpDebugColours(input.debugColour, UV);
    Output.vWorldXYZ = worldPos;
    Output.vNormal = float3(1, 1, 1);

    Output.vShadowPos = mul(float4(worldPos.xyz, 1), g_ModelToShadow).xyz;
    Output.vShadowPosOuter = mul(float4(worldPos.xyz, 1), g_ModelToShadowOuter).xyz;
    Output.vViewDir = mul(float4(worldPos.xyz, 1), g_WorldMatrix).xyz;

    // For debugging, darken a chequer board pattern of tiles to highlight tile boundaries.
    if (g_DebugShowPatches)
    {
        const uint patchY = PatchID / PATCHES_PER_TILE_EDGE;
        const uint patchX = PatchID - patchY * PATCHES_PER_TILE_EDGE;
        Output.vNormal *= (0.5 * ((patchX + patchY) % 2) + 0.5);
    }

    return Output;
}
