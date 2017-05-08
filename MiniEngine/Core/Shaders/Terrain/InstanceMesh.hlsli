#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

struct MeshPlacementVertex
{
    float3 WorldPosition            : INSTANCEPOSITION;
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

struct DepthVSOutput
{
    float4 position : SV_Position;
    float2 texcoord0 : texcoord0;
};

cbuffer VSConstants : register(b0)
{
    float4x4 modelToProjection;
    float4x4 modelToShadow;
    float4x4 modelToShadowOuter;
    float4x4 modelToWorld;
    float3 ViewerPos;
};

ObjectVSOutput InstanceMeshVS(VSInput vsInput, MeshPlacementVertex InstanceInput)
{
    ObjectVSOutput vsOutput;

    float3 WorldPosition = InstanceInput.WorldPosition;

    float3 ModelPosition = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.position * InstanceInput.UniformScale);
    WorldPosition += ModelPosition;

    vsOutput.position = mul(modelToProjection, float4(WorldPosition, 1.0));
    vsOutput.texcoord0 = vsInput.texcoord0;
    vsOutput.viewDir = WorldPosition - ViewerPos;
    vsOutput.shadowCoord = mul(modelToShadow, float4(WorldPosition, 1.0)).xyz;
    vsOutput.shadowCoordOuter = mul(modelToShadowOuter, float4(WorldPosition, 1.0)).xyz;

    vsOutput.normal = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.normal);
    vsOutput.tangent = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.tangent);
    vsOutput.bitangent = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.bitangent);

    return vsOutput;
}

DepthVSOutput InstanceMeshDepthVS(VSInput vsInput, MeshPlacementVertex InstanceInput)
{
    DepthVSOutput vsOutput;

    float3 WorldPosition = InstanceInput.WorldPosition;

    float3 ModelPosition = QuaternionTransformVector(InstanceInput.OrientationQuaternion, vsInput.position * InstanceInput.UniformScale);
    WorldPosition += ModelPosition;

    vsOutput.position = mul(modelToProjection, float4(WorldPosition, 1.0));
    vsOutput.texcoord0 = vsInput.texcoord0;

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

void InstanceMeshDepthPS(DepthVSOutput vsOutput)
{
    float Alpha = DefaultMaterialAlphaOnly(vsOutput.texcoord0);
    if (Alpha < 0.5) discard;
}

StructuredBuffer<MeshPlacementVertex> InputVertices : register(t0);
RWStructuredBuffer<MeshPlacementVertex> OutputVerticesLOD0 : register(u0);
RWStructuredBuffer<MeshPlacementVertex> OutputVerticesLOD1 : register(u1);
RWStructuredBuffer<MeshPlacementVertex> OutputVerticesLOD2 : register(u2);
RWStructuredBuffer<MeshPlacementVertex> OutputVerticesLOD3 : register(u3);

cbuffer cbInstanceMeshCulling : register(b0)
{
    float4x4 g_CameraWorldViewProj : register(c0);
    float3 g_CameraWorldPos : register(c4);
    float3 g_CameraWorldDir : register(c5);
    float4 g_LOD0Params : register(c6);
    float4 g_LOD1Params : register(c7);
    float4 g_LOD2Params : register(c8);
    uint g_MaxVertexCount : register(c9);
};

[numthreads(8, 8, 1)]
void InstanceMeshPrepassCS(uint3 DTid : SV_DispatchThreadID)
{
    bool Visible = true;

    const uint index = DTid.y * 8 + DTid.x;
    if (index >= g_MaxVertexCount)
    {
        Visible = false;
    }

    MeshPlacementVertex NewVertex = InputVertices[index];

    Visible = Visible & inFrustum(NewVertex.WorldPosition, g_CameraWorldPos, g_CameraWorldDir, NewVertex.UniformScale * 2, g_CameraWorldViewProj);

    if (Visible)
    {
        float3 WorldPos = NewVertex.WorldPosition;
        // TODO: adjust NewVertex.WorldPosition.y with terrain lookup
        NewVertex.WorldPosition = WorldPos;

        float DistanceFromCamera = length(WorldPos - g_CameraWorldPos);

        if (DistanceFromCamera < g_LOD0Params.x)
        {
            OutputVerticesLOD0[OutputVerticesLOD0.IncrementCounter()] = NewVertex;
        }
        else if (DistanceFromCamera < g_LOD1Params.x)
        {
            OutputVerticesLOD1[OutputVerticesLOD1.IncrementCounter()] = NewVertex;
        }
        else if (DistanceFromCamera < g_LOD2Params.x)
        {
            OutputVerticesLOD2[OutputVerticesLOD2.IncrementCounter()] = NewVertex;
        }
        else
        {
            OutputVerticesLOD3[OutputVerticesLOD3.IncrementCounter()] = NewVertex;
        }
    }
}

struct DrawIndexedInstancedParams
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

StructuredBuffer<DrawIndexedInstancedParams> InputDrawParams : register(t0);
StructuredBuffer<uint> InputInstanceOffsets : register(t1);
RWStructuredBuffer<DrawIndexedInstancedParams> OutputDrawParams : register(u0);

[numthreads(64, 1, 1)]
void CreateDrawParamsCS(uint3 DTid : SV_DispatchThreadID)
{
    const uint index = DTid.y * 8 + DTid.x;

    DrawIndexedInstancedParams InParams = InputDrawParams[index];
    uint ModelIndex = InParams.StartInstanceLocation;
    uint LODIndex = InParams.InstanceCount;

    uint OffsetIndex = ((ModelIndex + 1) * 4) + LODIndex;
    uint InstanceOffset = InputInstanceOffsets[OffsetIndex - 4];
    uint InstanceCount = InputInstanceOffsets[OffsetIndex] - InstanceOffset;

    DrawIndexedInstancedParams OutParams = InParams;
    OutParams.StartInstanceLocation = InstanceOffset;
    OutParams.InstanceCount = InstanceCount;

    OutputDrawParams[index] = OutParams;
}
