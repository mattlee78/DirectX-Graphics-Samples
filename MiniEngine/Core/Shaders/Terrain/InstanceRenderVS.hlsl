#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

void InstanceRenderVS(InstancePlacementVertex Placement, InstanceMeshVertex Mesh, out InstanceMeshOutputVertex OutMesh)
{
    const float3 ScaledPosition = Mesh.PositionXYZ * Placement.PositionXYZScale.w;
    const float3 RotatedPosition = QuaternionTransformVector(Placement.OrientationQuaternion, ScaledPosition);
    const float3 LocalPosition = RotatedPosition + Placement.PositionXYZScale.xyz;

    OutMesh.Position = mul(float4(LocalPosition, 1), g_WorldViewProj);
    OutMesh.Normal = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Normal);
    OutMesh.Tangent = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Tangent);
    OutMesh.Binormal = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Binormal);
    OutMesh.TexCoord = (Mesh.TexCoord * Placement.UVRect.zw) + Placement.UVRect.xy;
}
