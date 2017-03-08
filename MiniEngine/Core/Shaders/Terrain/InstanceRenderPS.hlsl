#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

float4 InstanceRenderPS(InstanceMeshOutputVertex Vertex) : SV_TARGET
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
