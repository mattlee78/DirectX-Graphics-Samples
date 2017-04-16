#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

struct MeshPlacementVertex
{
    float2 BlockPositionXZ          : INSTANCEPOSITION;
    float4 OrientationQuaternion    : PARAM0;
    float  UniformScale             : PARAM1;
};

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct ObjectVSOutput
{
    float4 position : SV_Position;
    float2 texcoord0 : texcoord0;
    float3 viewDir : texcoord1;
    float3 shadowCoord : texcoord2;
    float3 shadowCoordOuter : texcoord3;
    float3 normal : normal;
    float3 tangent : tangent;
    float3 bitangent : bitangent;
};

cbuffer VSConstants : register(b0)
{
    float4   BlockToWorldXZ;
    float4   BlockToTerrainTexUV;
    float4x4 modelToProjection;
    float4x4 modelToShadow;
    float4x4 modelToShadowOuter;
    float4x4 modelToWorld;
    float3 ViewerPos;
};

float FetchTerrainHeight(float2 TerrainUV)
{
    float HeightMapSample = g_CoarseHeightMap.SampleLevel(SamplerClampLinear, TerrainUV, 0).x;
    return HeightMapSample;
}

ObjectVSOutput InstanceMeshVS(VSInput vsInput, MeshPlacementVertex InstanceInput)
{
    ObjectVSOutput vsOutput;

    float2 TerrainUV = (InstanceInput.BlockPositionXZ * BlockToTerrainTexUV.xy) + BlockToTerrainTexUV.zw;
    float TerrainYpos = FetchTerrainHeight(TerrainUV);

    float3 WorldPosition;
    WorldPosition.xz = (InstanceInput.BlockPositionXZ * BlockToWorldXZ.xy) + BlockToWorldXZ.zw;
    WorldPosition.y = TerrainYpos;

    float3 ModelPosition = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.position * InstanceInput.UniformScale);
    WorldPosition += ModelPosition;

    vsOutput.position = mul(modelToProjection, float4(WorldPosition, 1.0));
    vsOutput.texcoord0 = vsInput.texcoord0;
    vsOutput.viewDir = mul(modelToWorld, float4(WorldPosition, 1.0)).xyz - ViewerPos;
    vsOutput.shadowCoord = mul(modelToShadow, float4(WorldPosition, 1.0)).xyz;
    vsOutput.shadowCoordOuter = mul(modelToShadowOuter, float4(WorldPosition, 1.0)).xyz;

    vsOutput.normal = mul(modelToWorld, float4(vsInput.normal, 0.0)).xyz;
    vsOutput.tangent = mul(modelToWorld, float4(vsInput.tangent, 0.0)).xyz;
    vsOutput.bitangent = mul(modelToWorld, float4(vsInput.bitangent, 0.0)).xyz;

    return vsOutput;
}

float3 InstanceMeshPS(ObjectVSOutput vsOutput) : SV_Target0
{
    return DefaultMaterialLightAndShadow(
        vsOutput.texcoord0,
        uint2(vsOutput.position.xy),
        vsOutput.viewDir,
        vsOutput.tangent,
        vsOutput.bitangent,
        vsOutput.normal,
        vsOutput.shadowCoord,
        vsOutput.shadowCoordOuter
    );
}
