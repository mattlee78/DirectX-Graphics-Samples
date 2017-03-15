#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

Texture2D g_InstanceDiffuse      : register(t16);
Texture2D g_InstanceSpecular     : register(t17);
Texture2D g_InstanceNormal       : register(t18);

float4 InstanceRenderPS(InstanceMeshOutputVertex Vertex) : SV_TARGET
{
    //return float4(Vertex.TexCoord, 0, 1);
    return g_InstanceDiffuse.Sample(SamplerClampLinear, Vertex.TexCoord);
}
