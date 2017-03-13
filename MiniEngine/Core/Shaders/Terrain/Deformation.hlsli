// Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA 
// OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR  
// CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS 
// OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY 
// OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, 
// EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Please direct any bugs or questions to SDKFeedback@nvidia.com

#include "Common.hlsli"
#include "INoise.hlsli"

// z in both cases is the height scale.
cbuffer cbDeform : register(b0)
{
    float3 g_DeformMin : packoffset(c0);
    float3 g_DeformMax : packoffset(c1);  // Z component is a UV scaling factor applied to the initialization pass
    float4 g_DeformConstants : packoffset(c2);
};

// Null in that there is no associated VB set by the API.
struct NullVertex
{
    uint VertexId   : SV_VertexID;
    uint InstanceId : SV_InstanceID;
};

struct DeformVertex
{
    float4 pos : SV_Position;
    float2 texCoord : TEXCOORD1;
};

DeformVertex InitializationVS( NullVertex input )
{
    DeformVertex output = (DeformVertex) 0;
    
	if (input.VertexId == 0)
	{
	    output.pos = float4(g_DeformMin.x, g_DeformMin.y, 0, 1);
	    output.texCoord = float2(0,0) * g_DeformMax.z;
	}
	else if (input.VertexId == 1)
	{
	    output.pos = float4(g_DeformMax.x, g_DeformMin.y, 0, 1);
	    output.texCoord = float2(1,0) * g_DeformMax.z;
	}
	else if (input.VertexId == 2)
	{
	    output.pos = float4(g_DeformMin.x, g_DeformMax.y, 0, 1);
	    output.texCoord = float2(0,1) * g_DeformMax.z;
	}
	else if (input.VertexId == 3)
	{
	    output.pos = float4(g_DeformMax.x, g_DeformMax.y, 0, 1);
	    output.texCoord = float2(1,1) * g_DeformMax.z;
	}
    
    return output;
}

float3 debugCubes(float2 uv)
{
	const float HORIZ_SCALE = 4, VERT_SCALE = 1;
	uv *= HORIZ_SCALE;
	return VERT_SCALE * floor(fmod(uv.x, 2.0)) * floor(fmod(uv.y, 2.0));
}

float3 debugXRamps(float2 uv)
{
	const float HORIZ_SCALE = 4, VERT_SCALE = 1;
	uv *= HORIZ_SCALE;
	return VERT_SCALE * frac(uv.x);
}

float3 debugSineHills(float2 uv)
{
	const float HORIZ_SCALE = 2 * 3.14159, VERT_SCALE = 0.5;
	uv *= HORIZ_SCALE;
	//uv += 2.8;			// arbitrarily not centered - test asymetric fns.
	return VERT_SCALE * (sin(uv.x) + 1) * (sin(uv.y) + 1);
}

float3 debugFlat(float2 uv)
{
	const float VERT_SCALE = 0.1;
	return VERT_SCALE;
}

float4 debugDualRamps(float2 uv)
{
    if (uv.x >= 0 && uv.x < 0.5)
    {
        return 0.5;
    }
    else if (uv.y >= 0 && uv.y < 0.5)
    {
        return 0.5;
    }
//    if (floor(uv.x) == 0 && floor(uv.y) == 0)
//    {
//        return float4(1, 1, 1, 1);
//    }
    return float4(max(frac(uv.x), frac(uv.y)).xxx, 1);
}

void InitializationPS( DeformVertex input, out float4 Heightmap : SV_Target0, out float4 Zonemap : SV_Target1 )
{
	const float2 uv = g_TextureWorldOffset.xz + input.texCoord;
    float4 result;
	//result = float4(debugXRamps(uv),  1);
	//result = float4(debugFlat(uv),  1);
	//result = float4(debugSineHills(uv * 2) * 0.25f,  1);
	//result = float4(debugCubes(uv), 1);
    //result = debugDualRamps(uv * 16);
    result = hybridTerrain(uv, g_FractalOctaves);
    result.xyz = ((result.x * g_DeformConstants.x) + g_DeformConstants.y).xxx;

    Heightmap = result;
    Zonemap = fBm4(uv * 0.01, 3, 2.0, 0.5);
}

Texture2D g_InputTexture : register(t0);

void GradientPS( DeformVertex input, out float4 GradientMap : SV_Target0, out float4 MaterialMap : SV_Target1 )
{
	input.texCoord.y = 1 - input.texCoord.y;
	float x0 = g_InputTexture.Sample(SamplerClampLinear, input.texCoord, int2( 1,0)).x;
	float x1 = g_InputTexture.Sample(SamplerClampLinear, input.texCoord, int2(-1,0)).x;
	float y0 = g_InputTexture.Sample(SamplerClampLinear, input.texCoord, int2(0, 1)).x;
	float y1 = g_InputTexture.Sample(SamplerClampLinear, input.texCoord, int2(0,-1)).x;
	GradientMap = float4(x0-x1, y0-y1, 0, 0);

    float MatchedScale = g_CoarseSampleSpacing.z;
    float2 scaledgradient = GradientMap.xy * MatchedScale;
    float3 normal = normalize(float3(scaledgradient.x, 16, scaledgradient.y));
    float smoothedheight = 0.25 * (x0 + x1 + y0 + y1);

    MaterialMap = float4(normal.y * normal.y, smoothedheight * 0.5f, 0, 1);
}

