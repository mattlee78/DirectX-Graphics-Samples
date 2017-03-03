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

#ifndef INCLUDED_COMMON_FXH
#define INCLUDED_COMMON_FXH

static const int CONTROL_VTX_PER_TILE_EDGE = 9;
static const int PATCHES_PER_TILE_EDGE = 8;
static const float RECIP_CONTROL_VTX_PER_TILE_EDGE = 1.0 / 9;

cbuffer cbCommon : register(b1)
{
    int3   g_FractalOctaves : packoffset(c0);		// ridge, fBm, uv twist
    float3 g_TextureWorldOffset : packoffset(c1);	// Offset of fractal terrain in texture space.
    float3 g_CoarseSampleSpacing : packoffset(c2);	// x = World space distance between samples in the coarse height map. y = world scale z = vertical scale
};

struct Adjacency
{
	// These are the size of the neighbours along +/- x or y axes.  For interior tiles
	// this is 1.  For edge tiles it is 0.5 or 2.0.
	float neighbourMinusX : ADJACENCY_SIZES0;
	float neighbourMinusY : ADJACENCY_SIZES1;
	float neighbourPlusX  : ADJACENCY_SIZES2;
	float neighbourPlusY  : ADJACENCY_SIZES3;
};

struct AppVertex
{
	float2 position  : POSITION_2D;
	Adjacency adjacency;
    uint VertexId    : SV_VertexID;
    uint InstanceId  : SV_InstanceID;
};

SamplerState SamplerRepeatMaxAniso : register(s3)
{
    Filter = ANISOTROPIC;
	MaxAnisotropy = 16;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState SamplerRepeatMedAniso : register(s4)
{
    Filter = ANISOTROPIC;
	MaxAnisotropy = 4;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState SamplerRepeatLinear : register(s1)
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState SamplerClampLinear : register(s0)
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
}; 

SamplerState SamplerRepeatPoint : register(s2)
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};

Texture2D g_TerrainRockDiffuse      : register(t16);
Texture2D g_TerrainRockNormal       : register(t17);
Texture2D g_TerrainRockHeight       : register(t18);

Texture2D g_TerrainDirtDiffuse      : register(t20);
Texture2D g_TerrainDirtNormal       : register(t21);
Texture2D g_TerrainDirtHeight       : register(t22);

Texture2D g_TerrainGrassDiffuse     : register(t24);
Texture2D g_TerrainGrassNormal      : register(t25);
Texture2D g_TerrainGrassHeight      : register(t26);

Texture2D g_TerrainSnowDiffuse      : register(t28);
Texture2D g_TerrainSnowNormal       : register(t29);
Texture2D g_TerrainSnowHeight       : register(t30);

float3 SmoothLerpColor(float3 ColorA, float3 ColorB, float Value, float Center, float Distance)
{
    float LerpParam = saturate((Value - Center) / Distance);
    return lerp(ColorA, ColorB, LerpParam);
}

float3 SampleNormal(Texture2D TexN, float2 texUV)
{
    float2 nxy = TexN.Sample(SamplerRepeatLinear, texUV).xy * 2 - 1;
    float zsq = 1.0 - dot(nxy, nxy);
    return float3(nxy.xy, sqrt(zsq));
}

float3 SmoothLerpTex(Texture2D TexA, Texture2D TexB, float2 texUV, float Value, float Center, float Distance)
{
    if (Value < Center)
    {
        return TexA.Sample(SamplerRepeatLinear, texUV).xyz;
    }
    else if (Value >= (Center + Distance))
    {
        return TexB.Sample(SamplerRepeatLinear, texUV).xyz;
    }
    float LerpParam = (Value - Center) / Distance;
    return lerp(TexA.Sample(SamplerRepeatLinear, texUV), TexB.Sample(SamplerRepeatLinear, texUV), LerpParam).xyz;
}

float3 SmoothLerpTexColor(Texture2D TexA, float3 ColorB, float2 texUV, float Value, float Center, float Distance)
{
    if (Value < Center)
    {
        return TexA.Sample(SamplerRepeatLinear, texUV).xyz;
    }
    else if (Value >= Center + Distance)
    {
        return ColorB;
    }
    float LerpParam = (Value - Center) / Distance;
    return lerp(TexA.Sample(SamplerRepeatLinear, texUV).xyz, ColorB, LerpParam);
}

float3 TerrainMaterialBlend(float normalYSquared, float ypos, float2 texUV, out float3 SpecularColor, out float SpecularMask, out float3 NormalSample)
{
    SpecularColor = float3(0, 0, 0);
    SpecularMask = 0;

    const float3 TempGrass = float3(0, 0.75, 0);
    const float3 TempDirt = float3(0.5, 0.25, 0);
    const float3 TempRock = float3(0.25, 0.25, 0.25);
    const float3 TempSnow = float3(1, 1, 1);
    const float3 TempSand = float3(1, 0.95, 0);

    float2 ModTexUV = texUV * 16;
    float2 RockTexUV = texUV * 2;

    float3 Diffuse;

    const float RockSlope = 0.49;
    const float RockSlopeBlend = 0.10;
    const float DirtSlope = 0.64;
    const float DirtSlopeBlend = 0.15;

    const float SnowAltitude = 1.25;
    const float RockAltitude = 1.0;
    const float AltitudeBlend = 0.001;

    /*
    float3 GrassOrSnow = SmoothLerpTex(g_TerrainGrassDiffuse, g_TerrainSnowDiffuse, ModTexUV, ypos, SnowAltitude, AltitudeBlend);
    float3 DirtOrRock = SmoothLerpTex(g_TerrainDirtDiffuse, g_TerrainRockDiffuse, ModTexUV, ypos, RockAltitude, AltitudeBlend);
    float3 FlatOrSlope = SmoothLerpColor(DirtOrRock, GrassOrSnow, normalYSquared, DirtSlope, DirtSlopeBlend);
    float3 SlopeOrRock = SmoothLerpTexColor(g_TerrainRockDiffuse, FlatOrSlope, RockTexUV, normalYSquared, RockSlope, RockSlopeBlend);
    */

    [branch]
    if (normalYSquared < RockSlope)
    {
        Diffuse = g_TerrainRockDiffuse.Sample(SamplerRepeatLinear, RockTexUV).xyz;
        NormalSample = SampleNormal(g_TerrainRockNormal, RockTexUV);
    }
    else if (normalYSquared < (RockSlope + RockSlopeBlend))
    {
        float RockHeight = g_TerrainRockHeight.Sample(SamplerRepeatLinear, RockTexUV).x;
        float LerpFraction = (normalYSquared - RockSlope) / RockSlopeBlend;
        if (LerpFraction > RockHeight)
        {
            Diffuse = g_TerrainDirtDiffuse.Sample(SamplerRepeatLinear, ModTexUV).xyz;
            NormalSample = SampleNormal(g_TerrainDirtNormal, ModTexUV);
        }
        else
        {
            Diffuse = g_TerrainRockDiffuse.Sample(SamplerRepeatLinear, RockTexUV).xyz;
            NormalSample = SampleNormal(g_TerrainRockNormal, RockTexUV);
        }
    }
    else if (normalYSquared < DirtSlope)
    {
        Diffuse = g_TerrainDirtDiffuse.Sample(SamplerRepeatLinear, ModTexUV).xyz;
        NormalSample = SampleNormal(g_TerrainDirtNormal, ModTexUV);
    }
    else if (normalYSquared < (DirtSlope + DirtSlopeBlend))
    {
        float GrassHeight = g_TerrainGrassHeight.Sample(SamplerRepeatLinear, ModTexUV).x;
        float LerpFraction = 1 - ((normalYSquared - DirtSlope) / DirtSlopeBlend);
        if (GrassHeight > LerpFraction)
        {
            Diffuse = SmoothLerpTex(g_TerrainGrassDiffuse, g_TerrainSnowDiffuse, ModTexUV, ypos, SnowAltitude, AltitudeBlend);
            NormalSample = SampleNormal(g_TerrainGrassNormal, ModTexUV);
        }
        else
        {
            Diffuse = g_TerrainDirtDiffuse.Sample(SamplerRepeatLinear, ModTexUV).xyz;
            NormalSample = SampleNormal(g_TerrainDirtNormal, ModTexUV);
        }
    }
    else
    {
        Diffuse = SmoothLerpTex(g_TerrainGrassDiffuse, g_TerrainSnowDiffuse, ModTexUV, ypos, SnowAltitude, AltitudeBlend);
        NormalSample = SampleNormal(g_TerrainGrassNormal, ModTexUV);
    }

    return Diffuse;
}

#endif	//INCLUDED_COMMON_FXH
