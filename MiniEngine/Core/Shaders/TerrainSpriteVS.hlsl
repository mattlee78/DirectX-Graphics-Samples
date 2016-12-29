#include "TerrainSpriteRS.hlsli"

cbuffer cbSpriteParams : register( b0 )
{
    float4 CenterPosInvScale;
    float HeightOffset;
}

struct VS_INPUT
{
	float4 XYZPosRot : POSITION;	// Quad center position and rotation in radians
    float4 UVRect : TEXCOORD0;
    float2 XZScale : TEXCOORD1;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;	     // Clip space position
    float3 TexHeight : TEXCOORD0;    // Tex coord and absolute height offset
};

[RootSignature(TerrainSprite_RootSig)]
VS_OUTPUT main( VS_INPUT input, uint VertID : SV_VertexID )
{
    float2 uv = float2(VertID & 1, (VertID >> 1) & 1);
    float2 CornerPos = (uv * 2) - 1;

    float2 RelativePos = (input.XYZPosRot.xz - CenterPosInvScale.xy);

    const uint2 uv0 = input.UVRect.xy;
	const uint2 uv1 = input.UVRect.xy + input.UVRect.zw;

    float sinrot = sin(input.XYZPosRot.w);
    float cosrot = cos(input.XYZPosRot.w);
    float2 AxisX = input.XZScale.x * float2(cosrot, -sinrot);
    float2 AxisZ = input.XZScale.y * float2(sinrot, cosrot);
    float2 XZPosRot = RelativePos + CornerPos.x * AxisX + CornerPos.y * AxisZ;

    float2 XZPosClip = XZPosRot * float2(CenterPosInvScale.z, -CenterPosInvScale.z);

    float2 XZPos = XZPosClip;

	VS_OUTPUT output;
	output.Pos = float4(XZPos.x, XZPos.y, 0.5f, 1);
	output.TexHeight.xy = lerp(uv0, uv1, uv);
    output.TexHeight.z = input.XYZPosRot.y + HeightOffset;
	return output;
}
