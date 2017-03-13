cbuffer CBInstancedDecorationLayer : register(b2)
{
    float4 g_ModelSpaceSizeOffset;
    float4 g_LODFadeRadiusSquared;
};

struct InstanceSourcePlacementVertex
{
    float2 PositionXZ;
    float RandomValue;
};

struct InstancePlacementVertex
{
    float4 PositionXYZScale         : POSITION0;
    float4 OrientationQuaternion    : TEXCOORD0;
    float4 UVRect                   : TEXCOORD1;
};

struct InstanceMeshVertex
{
    float3 PositionXYZ : POSITION1;
    float2 TexCoord    : TEXCOORD2;
    float3 Normal      : NORMAL;
    float3 Tangent     : TANGENT;
    float3 Binormal    : BINORMAL;
};

struct InstanceMeshOutputVertex
{
    float3 Normal      : TEXCOORD0;
    float3 Tangent     : TEXCOORD1;
    float3 Binormal    : TEXCOORD2;
    float2 TexCoord    : TEXCOORD3;
    float4 Position    : SV_POSITION;
};

float3 QuaternionTransformVector(float4 q, float3 v)
{
    return v.xyz + cross(q.xyz, cross(q.xyz, v.xyz) + q.www * v.xyz) * 2;
}
