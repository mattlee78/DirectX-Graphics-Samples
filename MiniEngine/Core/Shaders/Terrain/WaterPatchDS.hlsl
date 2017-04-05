#include "TerrainTessellation.hlsli"

// Returns a normal vector in XYZ plus a displacement height in W:
float4 ComputeWaterDisplacement(float3 worldPos)
{
    const float2 texUV = worldPos.xz + float2(g_TextureWorldOffset.x, -g_TextureWorldOffset.z) * g_tileWorldSize.y;
    float4 result = float4(0, 4, 0, 0);
    float heightScale = 1;
    float2 texcoord = texUV * g_WaterConstants.z;
    float2 variance = float2(1, 1);
    float2 timeoffset = float2(g_WaterConstants.y * 1.0f, g_WaterConstants.y * 0.7f);
    for (int i = 0; i < 5; ++i)
    {
        float2 CurrentTexCoord = texcoord + variance * timeoffset;
        float4 TexSample = g_WaterBumpMap.SampleLevel(SamplerRepeatLinear, CurrentTexCoord, 0).rbga;
        result.xz += (2 * TexSample.xz - 1) * heightScale;
        result.w += (TexSample.w - 0.5) * heightScale;
        heightScale *= 0.65;
        texcoord *= 1.4;
        variance.x *= -1;
    }
    //result.xz *= (g_WaterConstants.w * 100.0f);
    result.xyz = normalize(result.xyz);
    result.w *= g_WaterConstants.w;
    return result;
}

float3 TessellatedWaterPos(HS_CONSTANT_DATA_OUTPUT input,
    float2 UV : SV_DomainLocation,
    const OutputPatch<HS_OUTPUT, 4> terrainQuad,
    out float3 normal)
{
    // bilerp the position
    float3 worldPos = Bilerp(terrainQuad[0].vPosition, terrainQuad[1].vPosition, terrainQuad[2].vPosition, terrainQuad[3].vPosition, UV);

    const int mipLevel = 0;

    float height = g_WaterConstants.x;
    worldPos.y += height;

    const float2 heightUV = worldXZtoHeightUV(worldPos.xz);
    const float terrainYpos = g_CoarseHeightMap.SampleLevel(SamplerRepeatLinear, heightUV, 0).r;

    const float waterDepth = height - terrainYpos;
    if (waterDepth > -0.005f)
    {
        float lerp = saturate(waterDepth / 0.01f) * 0.5f + 0.5f;
        float4 wd = ComputeWaterDisplacement(worldPos);
        worldPos.y += wd.w * lerp;
        normal = wd.xyz;
    }
    else
    {
        normal = float3(0, 1, 0);
    }

    return worldPos;
}

[domain("quad")]
MeshVertex WaterPatchDS(HS_CONSTANT_DATA_OUTPUT input,
    float2 UV : SV_DomainLocation,
    const OutputPatch<HS_OUTPUT, 4> terrainQuad,
    uint PatchID : SV_PrimitiveID)
{
    MeshVertex Output = (MeshVertex)0;

    float3 waterNormal;
    const float3 worldPos = TessellatedWaterPos(input, UV, terrainQuad, waterNormal);
    Output.vPosition = mul(float4(worldPos.xyz, 1), g_WorldViewProj);
    Output.debugColour = lerpDebugColours(input.debugColour, UV);
    Output.vWorldXYZ = worldPos;
    Output.vNormal = waterNormal;

    Output.vShadowPos = mul(float4(worldPos.xyz, 1), g_ModelToShadow).xyz;
    Output.vShadowPosOuter = mul(float4(worldPos.xyz, 1), g_ModelToShadowOuter).xyz;
    Output.vViewDir = mul(float4(worldPos.xyz, 1), g_WorldMatrix).xyz;

    return Output;
}
