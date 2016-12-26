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
// Author:  James Stanard 
//

#include "TextRS.hlsli"

cbuffer cbFontParams : register( b0 )
{
	float4 Color;
	float2 ShadowOffset;
	float ShadowHardness;
	float ShadowOpacity;
	float HeightRange;	// The range of the signed distance field.
}

Texture2D BlitTex : register( t0 );
SamplerState LinearSampler : register( s0 );

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

[RootSignature(Text_RootSig)]
float4 main( PS_INPUT Input ) : SV_Target
{
    float4 diffuse = BlitTex.Sample(LinearSampler, Input.uv);
    diffuse.a = max(diffuse.a, ShadowOpacity);
    if (ShadowHardness > 0)
    {
        diffuse.rgb = diffuse.rrr;
    }
    return diffuse * Color;
}
