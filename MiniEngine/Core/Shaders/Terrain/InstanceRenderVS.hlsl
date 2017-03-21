#include "TerrainTessellation.hlsli"
#include "InstanceRendering.hlsli"

void InstanceRenderVS(InstancePlacementVertex Placement, InstanceMeshVertex Mesh, out InstanceMeshOutputVertex OutMesh)
{
    float3 MeshXYZ = Mesh.PositionXYZ;
    float3 ScaledPosition = MeshXYZ * Placement.PositionXYZScale.w;
    float3 RotatedPosition = QuaternionTransformVector(Placement.OrientationQuaternion, ScaledPosition);
    float3 LocalPosition = RotatedPosition + Placement.PositionXYZScale.xyz;
    if (Mesh.TexCoord.y == 0)
    {
        LocalPosition.xz += Placement.Params.xy;
    }

    OutMesh.Position = mul(float4(LocalPosition, 1), g_WorldViewProj);
    OutMesh.Normal = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Normal);
    OutMesh.Tangent = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Tangent);
    OutMesh.Binormal = QuaternionTransformVector(Placement.OrientationQuaternion, Mesh.Binormal);
    OutMesh.TexCoord = (Mesh.TexCoord * Placement.UVRect.zw) + Placement.UVRect.xy;
}
