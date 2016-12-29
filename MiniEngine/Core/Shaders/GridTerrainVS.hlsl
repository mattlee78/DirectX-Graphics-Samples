#include "GridTerrain.hlsli"

VS_OUTPUT_HI vsmain( VS_INPUT In )
{
    float2 WorldTexCoord = (In.PositionXZ * PositionToTexCoord.zz) + PositionToTexCoord.xy;

    float2 HeightmapUV = (In.PositionXZ * BlockToHeightmap.zz) + BlockToHeightmap.xy;
    float2 SurfaceUV = (In.PositionXZ * BlockToSurfacemap.zz) + BlockToSurfacemap.xy;
    float PositionY = HeightmapTexture.SampleLevel(heightSampler, HeightmapUV, 0).x;

    VS_OUTPUT_HI Out;                                                             

    Out.TexCoord01.xy = SurfaceUV.xy;
    Out.TexCoord01.zw = SurfaceUV.xy;
                                                                               
    float4 LocalPos = float4(In.PositionXZ.x, PositionY, In.PositionXZ.y, 1);
    float4 WorldPos = mul(LocalPos, mWorld);
    Out.Position = mul(WorldPos, mViewProj);
    Out.PosToCamera.xyz = vCameraPosWorld.xyz - WorldPos.xyz;
    Out.PosToCamera.w = 0;

    Out.ShadowPos0 = mul(WorldPos, matWorldToShadow0).xyz;
    Out.ShadowPos1 = mul(WorldPos, matWorldToShadow1).xyz;
    Out.ShadowPos2 = mul(WorldPos, matWorldToShadow2).xyz;

    return Out;                                                                
}                                                                              

