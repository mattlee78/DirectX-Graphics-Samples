cbuffer cb0 : register(b0)
{
    row_major float4x4 mWorld         : packoffset(c0);
    row_major float4x4 mViewProj      : packoffset(c4);
    float4 PositionToTexCoord         : packoffset(c8);
    float4 LODConstants               : packoffset(c9);
    float4 BlockColor                 : packoffset(c10);
    float4 BlockToHeightmap           : packoffset(c11);
    float4 BlockToSurfacemap          : packoffset(c12);
};

cbuffer cb1 : register(b1)
{
    float4 TexCoordTransform0            : packoffset(c0);
    float4 TexCoordTransform1            : packoffset(c1);
    float3 vAmbientLightColor            : packoffset(c2);
    float3 vInverseLightDirection        : packoffset(c3);
    float3 vDirectionalLightColor        : packoffset(c4);
    float4 vWaterConstants               : packoffset(c5);
    float3 vCameraPosWorld               : packoffset(c6);

    float4 vShadowSize0                  : packoffset(c7);
    row_major float4x4 matWorldToShadow0 : packoffset(c8);
    row_major float4x4 matWorldToShadow1 : packoffset(c12);
    row_major float4x4 matWorldToShadow2 : packoffset(c16);
};

Texture2D HeightmapTexture : register(t2);

struct VS_INPUT
{
    float2 PositionXZ : POSITION;
};

struct VS_OUTPUT
{
    float4 TexCoord01 : TEXCOORD0;
    float4 PosToCamera: TEXCOORD1;
    float3 ShadowPos0 : TEXCOORD2;
    float4 Position   : SV_Position;
};

struct VS_OUTPUT_HI
{
    float4 TexCoord01 : TEXCOORD0;
    float4 PosToCamera: TEXCOORD1;
    float3 ShadowPos0 : TEXCOORD2;
    float3 ShadowPos1 : TEXCOORD3;
    float3 ShadowPos2 : TEXCOORD4;
    float4 Position   : SV_Position;
};

struct PS_INPUT_HI
{
    float4 TexCoord01 : TEXCOORD0;
    float4 PosToCamera: TEXCOORD1;
    float3 ShadowPos0 : TEXCOORD2;
    float3 ShadowPos1 : TEXCOORD3;
    float3 ShadowPos2 : TEXCOORD4;
};

struct PS_INPUT
{
    float4 TexCoord01 : TEXCOORD0;
    float4 PosToCamera: TEXCOORD1;
    float3 ShadowPos0 : TEXCOORD2;
};

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState linearSampler : register(s0);

struct DECORATIONMODEL_VS_INPUT
{
    float4 Position   : POSITION;
    float4 Normal     : NORMAL;
    float4 TexCoord   : TEXCOORD0;
};

struct DECORATIONINSTANCE_VS_INPUT
{
    float4 Position   : POSITION1;
    float4 Orientation: ORIENTATION;
    float4 BottomColor: COLOR0;
    float4 TopColor   : COLOR1;
    float4 TexUVWH    : TEXCOORD1;
};

struct DECORATION_VS_OUTPUT
{
    float2 TexCoord   : TEXCOORD0;
    float3 Normal     : TEXCOORD1;
    float4 Color      : TEXCOORD2;
    float3 WorldPos   : TEXCOORD3;
    float3 ShadowPos0 : TEXCOORD4;
    float3 ShadowPos1 : TEXCOORD5;
    float3 ShadowPos2 : TEXCOORD6;
    float4 Position   : SV_Position;
};

struct DECORATION_PS_INPUT
{
    float2 TexCoord   : TEXCOORD0;
    float3 Normal     : TEXCOORD1;
    float4 Color      : TEXCOORD2;
    float3 WorldPos   : TEXCOORD3;
    float3 ShadowPos0 : TEXCOORD4;
    float3 ShadowPos1 : TEXCOORD5;
    float3 ShadowPos2 : TEXCOORD6;
};

float3 QuaternionTransformVector(float4 q, float3 v)
{
    return v.xyz + cross(q.xyz, cross(q.xyz, v.xyz) + q.www * v.xyz) * float3(2, 2, 2);
}

