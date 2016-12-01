#include "GridTerrain.hlsli"

VS_OUTPUT_HI vsmain( VS_INPUT In )
{
    float2 RawTexCoord = (In.PositionXZ * PositionToTexCoord.zz) + PositionToTexCoord.xy;
    float2 TexCoord0 = float2(dot(RawTexCoord.xy, TexCoordTransform0.xy), dot(RawTexCoord.xy, TexCoordTransform0.zw));
    float2 TexCoord1 = float2(dot(RawTexCoord.xy, TexCoordTransform1.xy), dot(RawTexCoord.xy, TexCoordTransform1.zw));

    VS_OUTPUT_HI Out;                                                             

    Out.TexCoord01.xy = TexCoord0.xy;
    Out.TexCoord01.zw = TexCoord1.xy;
    Out.Blend = In.Blend * float4(1, 1, 255, 255);
    Out.Normal.xyz = (In.Normal * 2) - 1;
    Out.Normal.w = 0;
                                                                               
    float4 LocalPos = float4(In.PositionXZ.x, In.PositionY, In.PositionXZ.y, 1);
    float4 WorldPos = mul(LocalPos, mWorld);
    Out.Position = mul(WorldPos, mViewProj);
    Out.PosToCamera.xyz = vCameraPosWorld.xyz - WorldPos.xyz;
    Out.PosToCamera.w = 0;

    Out.ShadowPos0 = mul(WorldPos, matWorldToShadow0).xyz;
    Out.ShadowPos1 = mul(WorldPos, matWorldToShadow1).xyz;
    Out.ShadowPos2 = mul(WorldPos, matWorldToShadow2).xyz;

    return Out;                                                                
}                                                                              

