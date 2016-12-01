#include "GridTerrain.hlsli"

VS_OUTPUT vswater( VS_INPUT In )                                                
{
    float2 RawTexCoord = (In.PositionXZ * PositionToTexCoord.zz) + PositionToTexCoord.xy;
    float2 TexCoord0 = float2(dot(RawTexCoord.xy, TexCoordTransform0.xy), dot(RawTexCoord.xy, TexCoordTransform0.zw));
    float2 TexCoord1 = float2(dot(RawTexCoord.xy, TexCoordTransform1.xy), dot(RawTexCoord.xy, TexCoordTransform1.zw));

    float TerrainHeight = In.PositionY;
    float WaterHeight = vWaterConstants.x;
    float WaterDepth = WaterHeight - TerrainHeight;

    float Waviness = saturate(WaterDepth * 0.25) * 0;

    float FullWave1 = sin(RawTexCoord.x + RawTexCoord.y + vWaterConstants.y * 2.9f);
    float FullWave2 = cos(RawTexCoord.x * 0.37f + RawTexCoord.y * 2.17f + vWaterConstants.y * 4.1f);
    float FullWave = FullWave1 + FullWave2;
    float WaveAmplitude = FullWave * Waviness * 0.1f;
    float2 WaveNormalXZ = FullWave`(RawTexCoord.xy);

    VS_OUTPUT Out;                                                             

    Out.TexCoord01.xy = TexCoord0.xy;
    Out.TexCoord01.zw = TexCoord1.xy;
    Out.Blend = float4(Waviness, 0, 0, WaterDepth);
    Out.Normal.xyz = float3(WaveNormalXZ.x, 1, WaveNormalXZ.y);
    Out.Normal.w = 0;
                                                                               
    float4 LocalPos = float4(In.PositionXZ.x, WaterHeight + WaveAmplitude, In.PositionXZ.y, 1);
    float4 WorldPos = mul(LocalPos, mWorld);
    Out.Position = mul(WorldPos, mViewProj);
    Out.PosToCamera.xyz = vCameraPosWorld.xyz - WorldPos.xyz;
    Out.PosToCamera.w = 0;

    Out.ShadowPos0 = 0;

    return Out;                                                                
}                                                                              
