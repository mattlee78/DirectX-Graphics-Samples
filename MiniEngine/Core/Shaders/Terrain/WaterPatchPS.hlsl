#include "TerrainTessellation.hlsli"

float4 WaterPatchPS(MeshVertex input) : SV_Target
{
    //return DebugCracksPattern(input);
    float2 vWorldXZ = input.vWorldXYZ.xz;

    const float2 heightUV = worldXZtoHeightUV(vWorldXZ);
    const float terrainYpos = g_CoarseHeightMap.Sample(SamplerRepeatLinear, heightUV).r;
    const float waterDepth = g_WaterConstants.x - terrainYpos;
    const float shallowness = 1 - saturate(waterDepth * 100);
    const float shallowAlpha = 1 - pow(shallowness, 4);

//    const float2 heightUV = worldXZtoHeightUV(vWorldXZ);
//    const float2 grad = g_CoarseGradientMap.Sample(SamplerRepeatLinear, heightUV).rg;
//    const float vScale = g_CoarseSampleSpacing.y;
//    const float3 coarseNormal = normalize(float3(-vScale * grad.x, g_CoarseSampleSpacing.x, -vScale * grad.y));
//    const float3 normal = coarseNormal;
    const float3 normal = input.vNormal;

    // Texture coords have to be offset by the eye's 2D world position.  Why the 2x???
    const float2 texUV = vWorldXZ + float2(g_TextureWorldOffset.x, -g_TextureWorldOffset.z) * g_tileWorldSize.y;

//    const float4 MatMapSample = g_CoarseMaterialMap.Sample(SamplerRepeatLinear, heightUV);
    float3 TempSpecular = 1;
    float TempSpecularMask = 1;
//    float3 NormalSample;
//    float TerrainHeight = (MatMapSample.y * MaterialMapScale) - MaterialMapOffset;
//    float3 TempDiffuse = TerrainMaterialBlend(MatMapSample.x, TerrainHeight, texUV, TempSpecular, TempSpecularMask, NormalSample);
    float3 TempDiffuse = lerp(float3(0.5, 0.5, 1), float3(1, 1, 1), shallowness);

    float3 Tangent = normalize(cross(normal, float3(1, 0, 0)));
    float3 Bitangent = normalize(cross(Tangent, normal));

//    float3 NewNormal = normalize(NormalSample.x * -Tangent + NormalSample.y * Bitangent + NormalSample.z * normal);
    float3 NewNormal = normal;

    float3 viewDir = normalize(input.vViewDir);
    float3 shadowCoord = input.vShadowPos;
    float3 shadowCoordOuter = input.vShadowPosOuter;

    float3 LitResult = DefaultLightAndShadowModelNormal(TempDiffuse, TempSpecular, TempSpecularMask, NewNormal, uint2(input.vPosition.xy), viewDir, shadowCoord, shadowCoordOuter);
    if (g_DebugShowPatches)
    {
        LitResult *= input.vNormal.x;
    }
    //LitResult = NormalSample * 0.5 + 0.5;
    return float4(LitResult, 0.75f * shallowAlpha);
    //float4 WaterBump = g_WaterBumpMap.SampleLevel(SamplerRepeatLinear, texUV, 0).rbga;
    //return float4(input.vNormal * 0.5 + 0.5, 1);
}
