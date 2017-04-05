#include "TerrainTessellation.hlsli"

float3 ComputeWaterMicrobump(float2 texUV)
{
    float2 microtexUV = texUV * g_WaterConstants.z;
    float2 timeoffset = float2(g_WaterConstants.y * 1.0f, g_WaterConstants.y * 0.7f);
    float3 BumpA = normalize(2 * g_WaterBumpMap.Sample(SamplerRepeatLinear, microtexUV - timeoffset * 0.2f).rbg - float3(1, -8, 1));
    float3 BumpB = normalize(2 * g_WaterBumpMap.Sample(SamplerRepeatLinear, microtexUV * 0.5f - timeoffset * 0.05f).rbg - float3(1, -8, 1));
    return normalize(BumpA + BumpB);
}

float4 WaterPatchPS(MeshVertex input) : SV_Target
{
    float2 vWorldXZ = input.vWorldXYZ.xz;

    const float2 heightUV = worldXZtoHeightUV(vWorldXZ);
    const float terrainYpos = g_CoarseHeightMap.Sample(SamplerRepeatLinear, heightUV).r;
    const float waterDepth = g_WaterConstants.x - terrainYpos;
    const float shallowness = 1 - saturate(waterDepth * 100);
    const float shallowAlpha = 1 - pow(shallowness, 4);
    const float3 normal = input.vNormal;

    const float2 texUV = vWorldXZ + float2(g_TextureWorldOffset.x, -g_TextureWorldOffset.z) * g_tileWorldSize.y;

    float3 TempSpecular = 1;
    float TempSpecularMask = 1;
    float3 TempDiffuse = lerp(float3(0.5, 0.5, 1), float3(1, 1, 1), shallowness);

    float3 Tangent = normalize(cross(normal, float3(1, 0, 0)));
    float3 Bitangent = normalize(cross(Tangent, normal));

    float3x3 BasisMatrix;
    BasisMatrix[0] = normal;
    BasisMatrix[1] = Tangent;
    BasisMatrix[2] = Bitangent;

    //float3 MicrobumpNormal = ComputeWaterMicrobump(texUV);

    float3 NewNormal = normal;

    float3 viewDir = normalize(input.vViewDir);
    float3 shadowCoord = input.vShadowPos;
    float3 shadowCoordOuter = input.vShadowPosOuter;

    float3 LitResult = DefaultLightAndShadowModelNormal(TempDiffuse, TempSpecular, TempSpecularMask, NewNormal, uint2(input.vPosition.xy), viewDir, shadowCoord, shadowCoordOuter);
    if (g_DebugShowPatches)
    {
        LitResult *= input.vNormal.x;
    }
    return float4(LitResult, 0.75f * shallowAlpha);
}
