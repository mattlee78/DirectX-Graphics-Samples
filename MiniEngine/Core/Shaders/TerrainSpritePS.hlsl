#include "TerrainSpriteRS.hlsli"

Texture2D BlitTex : register( t0 );
SamplerState LinearSampler : register( s0 );

struct PS_INPUT
{
    float4 Pos : SV_POSITION;	     // Clip space position
    float3 TexHeight : TEXCOORD0;    // Tex coord and absolute height offset
};

[RootSignature(TerrainSprite_RootSig)]
void main( PS_INPUT Input, out float4 Heightmap : SV_Target0, out float4 Materialmap : SV_Target1 )
{
    Heightmap.xyzw = Input.TexHeight.z;
    Materialmap.xyzw = Input.TexHeight.xyyy;
}
