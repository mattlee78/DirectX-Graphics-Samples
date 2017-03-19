#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

StructuredBuffer<InstanceSourcePlacementVertex> InputVertices : register(t4);
RWStructuredBuffer<InstancePlacementVertex> OutputVertices : register(u0);

[numthreads(8, 8, 1)]
void InstancePrepassCS( uint3 DTid : SV_DispatchThreadID )
{
    const uint index = DTid.y * 8 + DTid.x;

    float2 InPosXZ = InputVertices[index].PositionXZ;
    const float InRand = InputVertices[index].RandomValue;

    float InstanceScale = 0.002 * InRand + 0.002;

    // Scroll InPosXZ according to camera offset
    float2 OffsetXZ = g_TextureWorldOffset.xz * float2(-g_ModelSpaceSizeOffset.z, g_ModelSpaceSizeOffset.z);
    InPosXZ = frac(InPosXZ + OffsetXZ);

    InPosXZ = (InPosXZ * g_ModelSpaceSizeOffset.x - g_ModelSpaceSizeOffset.y);

    float2 InPosUV = worldXZtoHeightUV(InPosXZ);
    float HeightMapSample = g_CoarseHeightMap.SampleLevel(SamplerClampLinear, InPosUV, 0).x;

    float3 WorldPos = float3(InPosXZ.x, HeightMapSample, InPosXZ.y);
    float DistanceFromCamera = length(WorldPos - (g_EyePos / g_CoarseSampleSpacing.y));

    bool Visible = true;
    if (DistanceFromCamera > g_LODFadeRadius.x)
    {
        Visible = false;
    }
    else
    {
        Visible = inFrustum(WorldPos, g_EyePos / g_CoarseSampleSpacing.y, g_ViewDir, InstanceScale * 2);
    }

    if (Visible)
    {
        float LODFade = 1.0f - saturate((DistanceFromCamera - g_LODFadeRadius.y) * g_LODFadeRadius.z);
        float LODScale = g_LODFadeRadius.w * DistanceFromCamera + 1.0f;

        float2 GradientMapSample = g_CoarseGradientMap.SampleLevel(SamplerClampLinear, InPosUV, 0).xy;
        float4 MaterialMapSample = g_CoarseMaterialMap.SampleLevel(SamplerClampLinear, InPosUV, 0);

        float MatchedScale = g_CoarseSampleSpacing.z;
        float2 scaledgradient = GradientMapSample * MatchedScale * 0.5;
        float3 TerrainNormal = normalize(float3(scaledgradient.x, 16, scaledgradient.y));

        const float4 GradientQuaternion = QuaternionBetweenVectors(TerrainNormal, float3(0, 1, 0));
        const float4 RotationQuaternion = QuaternionRotationNormal(float3(0, 1, 0), 6.28 * InRand);
        const float4 OutQuaternion = QuaternionMultiply(RotationQuaternion, GradientQuaternion);

        if (MaterialMapSample.x >= 0.65)
        {
            InstancePlacementVertex Out;

            Out.PositionXYZScale = float4(WorldPos, InstanceScale * LODFade * LODScale);
            Out.OrientationQuaternion = OutQuaternion;
            Out.UVRect = float4(0, 0.5, 0.25, 0.5);

            OutputVertices[OutputVertices.IncrementCounter()] = Out;
        }
    }
}
