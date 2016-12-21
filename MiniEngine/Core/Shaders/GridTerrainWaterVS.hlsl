#include "GridTerrain.hlsli"

VS_OUTPUT vswater( VS_INPUT In )                                                
{
    float2 HeightmapUV = (In.PositionXZ * BlockToHeightmap.zz) + BlockToHeightmap.xy;
    float2 SurfaceUV = (In.PositionXZ * BlockToSurfacemap.zz) + BlockToSurfacemap.xy;
    float PositionY = HeightmapTexture.SampleLevel(linearSampler, HeightmapUV, 0).x;

    float TerrainHeight = PositionY;
    float WaterHeight = vWaterConstants.x;
    float WaterDepth = WaterHeight - TerrainHeight;

    float Waviness = saturate(WaterDepth * 0.25) * 0;

    float2 WorldTexCoord = (In.PositionXZ * PositionToTexCoord.zz) + PositionToTexCoord.xy;
    float FullWave1 = sin(WorldTexCoord.x + WorldTexCoord.y + vWaterConstants.y * 2.9f);
    float FullWave2 = cos(WorldTexCoord.x * 0.37f + WorldTexCoord.y * 2.17f + vWaterConstants.y * 4.1f);
    float FullWave = FullWave1 + FullWave2;
    float WaveAmplitude = FullWave * Waviness * 0.1f;
    float2 WaveNormalXZ = FullWave`(WorldTexCoord.xy);

    VS_OUTPUT Out;                                                             

    Out.TexCoord01.xy = SurfaceUV.xy;
    Out.TexCoord01.zw = SurfaceUV.xy;
                                                                               
    float4 LocalPos = float4(In.PositionXZ.x, WaterHeight + WaveAmplitude, In.PositionXZ.y, 1);
    float4 WorldPos = mul(LocalPos, mWorld);
    Out.Position = mul(WorldPos, mViewProj);
    Out.PosToCamera.xyz = vCameraPosWorld.xyz - WorldPos.xyz;
    Out.PosToCamera.w = 0;

    Out.ShadowPos0 = 0;

    return Out;                                                                
}                                                                              
