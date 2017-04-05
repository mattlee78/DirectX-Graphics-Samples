#include "TerrainTessellation.hlsli"

VS_CONTROL_POINT_OUTPUT WaterPassThroughVS(AppVertex input)
{
    VS_CONTROL_POINT_OUTPUT output;
    int2 intUV;
    ReconstructPosition(input, output.vPosition, intUV);

    float z = g_WaterConstants.x;
    output.vPosition.y += z;
    output.vWorldXZ = output.vPosition.xz;
    output.adjacency = input.adjacency;

    return output;
}
