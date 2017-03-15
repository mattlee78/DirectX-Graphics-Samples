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

float4 QuaternionBetweenVectors(float3 a, float3 b)
{
    float3 CrossNormal = cross(a, b);
    float DotNormal = dot(a, b);
    return normalize(float4(CrossNormal, 1 + DotNormal));
}

float4 QuaternionRotationNormal(float3 axis, float angle)
{
    float sha = sin(angle * 0.5);
    float cha = cos(angle * 0.5);
    return float4(axis * sha, cha);
}

float4 QuaternionMultiply(float4 a, float4 b)
{
    float3 cr = cross(a.xyz, b.xyz);
    float d = dot(a.xyz, b.xyz);
    return float4(a.xyz * b.w + b.xyz * a.w + cr, a.w * b.w - d);
}
