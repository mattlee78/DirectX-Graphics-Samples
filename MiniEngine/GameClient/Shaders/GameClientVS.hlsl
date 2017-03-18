//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  James Stanard
//             Alex Nankervis
//

#include "GameClientRS.hlsli"

cbuffer VSConstants : register(b0)
{
	float4x4 modelToProjection;
	float4x4 modelToShadow;
    float4x4 modelToShadowOuter;
    float4x4 modelToWorld;
	float3 ViewerPos;
};

struct VSInput
{
	float3 position : POSITION;
	float2 texcoord0 : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
};

[RootSignature(ModelViewer_RootSig)]
ObjectVSOutput main(VSInput vsInput)
{
	ObjectVSOutput vsOutput;

	vsOutput.position = mul(modelToProjection, float4(vsInput.position, 1.0));
	vsOutput.texcoord0 = vsInput.texcoord0;
	vsOutput.viewDir = mul(modelToWorld, float4(vsInput.position, 1.0)).xyz - ViewerPos;
	vsOutput.shadowCoord = mul(modelToShadow, float4(vsInput.position, 1.0)).xyz;
    vsOutput.shadowCoordOuter = mul(modelToShadowOuter, float4(vsInput.position, 1.0)).xyz;

	vsOutput.normal = mul(modelToWorld, float4(vsInput.normal, 0.0)).xyz;
	vsOutput.tangent = mul(modelToWorld, float4(vsInput.tangent, 0.0)).xyz;
	vsOutput.bitangent = mul(modelToWorld, float4(vsInput.bitangent, 0.0)).xyz;

	return vsOutput;
}
