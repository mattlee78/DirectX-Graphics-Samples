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

    // Scroll InPosXZ according to camera offset
    float2 OffsetXZ = g_TextureWorldOffset.xz * float2(-g_ModelSpaceSizeOffset.z, g_ModelSpaceSizeOffset.z);
    InPosXZ = frac(InPosXZ + OffsetXZ);

    InPosXZ = (InPosXZ * g_ModelSpaceSizeOffset.x - g_ModelSpaceSizeOffset.y);

    float2 InPosUV = worldXZtoHeightUV(InPosXZ);

    float HeightMapSample = g_CoarseHeightMap.SampleLevel(SamplerClampLinear, InPosUV, 0).x;
    float2 GradientMapSample = g_CoarseGradientMap.SampleLevel(SamplerClampLinear, InPosUV, 0).xy;
    float4 MaterialMapSample = g_CoarseMaterialMap.SampleLevel(SamplerClampLinear, InPosUV, 0);

    float InstanceScale = 0.01 * InRand;

    //HeightMapSample *= 10;

    InstancePlacementVertex Out;

    Out.PositionXYZScale = float4(InPosXZ.x, HeightMapSample, InPosXZ.y, InstanceScale);
    Out.OrientationQuaternion = float4(0, 0, 0, 1);
    Out.UVRect = float4(0, 0, 1, 1);
    Out.UVRect.xy = InPosUV;

    OutputVertices[index] = Out;
}
