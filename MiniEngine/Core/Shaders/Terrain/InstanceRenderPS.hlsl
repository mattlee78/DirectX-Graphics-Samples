#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

float4 InstanceRenderPS(InstanceMeshOutputVertex Vertex) : SV_TARGET
{
	return float4(Vertex.TexCoord.xy, 0, 1);
}
