#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

StructuredBuffer<InstanceSourcePlacementVertex> InputVertices : register(t4);
RWStructuredBuffer<InstancePlacementVertex> OutputVertices : register(u0);

[numthreads(64, 1, 1)]
void InstancePrepassCS( uint3 DTid : SV_DispatchThreadID )
{
    float2 InPosXZ = InputVertices[DTid.x].PositionXZ;
    const float InRand = InputVertices[DTid.x].RandomValue;

    // TODO: scroll InPosXZ according to camera offset
    //InPosXZ = frac(InPosXZ + OffsetXZ);

    InPosXZ = (InPosXZ * g_ModelSpaceSizeOffset.x - g_ModelSpaceSizeOffset.y);

    float2 InPosUV = worldXZtoHeightUV(InPosXZ);

    const float HeightMapSample = g_CoarseHeightMap.SampleLevel(SamplerClampLinear, InPosUV, 0).x;
    const float2 GradientMapSample = g_CoarseGradientMap.SampleLevel(SamplerClampLinear, InPosUV, 0).xy;
    const float4 MaterialMapSample = g_CoarseMaterialMap.SampleLevel(SamplerClampLinear, InPosUV, 0);

    float InstanceScale = 0.5 * InRand;

    OutputVertices[DTid.x].PositionXYZScale = float4(InPosXZ.x, HeightMapSample, InPosXZ.y, InstanceScale);
    OutputVertices[DTid.x].OrientationQuaternion = float4(0, 0, 0, 1);
    OutputVertices[DTid.x].UVRect = float4(0, 0, 1, 1);
}
