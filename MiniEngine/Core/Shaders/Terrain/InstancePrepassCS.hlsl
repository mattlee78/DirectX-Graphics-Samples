#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

StructuredBuffer<InstanceSourcePlacementVertex> InputVertices : register(t4);
RWStructuredBuffer<InstancePlacementVertex> OutputVertices : register(u0);

[numthreads(8, 8, 1)]
void InstancePrepassCS( uint3 DTid : SV_DispatchThreadID )
{
    const uint index = DTid.y * 8 + DTid.x;

    float2 InPosXZ = InputVertices[index].PositionXZ;
    const float InRand = InputVertices[index].RandomValue.x;
    const float InRand2 = InputVertices[index].RandomValue.y;

    float InstanceScale = (g_InstanceAppearance.y * InRand) + g_InstanceAppearance.x;

    // Scroll InPosXZ according to camera offset
    float2 OffsetXZ = g_TextureWorldOffset.xz * float2(-g_ModelSpaceSizeOffset.z, g_ModelSpaceSizeOffset.z);
    InPosXZ = frac(InPosXZ + OffsetXZ);

    float2 CenterXZ = InPosXZ * 2 - 1;

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

    float LODScale = 1.0f;
    float LODFraction = pow(DistanceFromCamera / g_LODFadeRadius.x, 1.0f);
    float TopLODFraction = LODFraction * 1.25f;
    if (InRand < LODFraction)
    {
        Visible = false;
    }
    else
    {
        float Lerp = saturate((InRand - LODFraction) / (TopLODFraction - LODFraction));
        LODScale = Lerp;
    }

    if (Visible)
    {
        float LODFade = 1.0f - saturate((DistanceFromCamera - g_LODFadeRadius.y) * g_LODFadeRadius.z);
        LODScale *= (g_LODFadeRadius.w * DistanceFromCamera + 1.0f);
        const float FinalScale = InstanceScale * LODFade * LODScale;

        float WindDot = dot(CenterXZ, g_WindXZVT.xy);
        WindDot += InRand * 2.0f;
        float Breeze = sin((WindDot + g_WindXZVT.w) * 3) * g_WindXZVT.z * FinalScale;

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

            Out.PositionXYZScale = float4(WorldPos, FinalScale);
            Out.OrientationQuaternion = OutQuaternion;
            Out.UVRect = g_UVRects[(uint)(InRand2 * 16)];
            Out.Params = float4(Breeze * g_WindXZVT.xy, 0, 0);

            OutputVertices[OutputVertices.IncrementCounter()] = Out;
        }
    }
}
