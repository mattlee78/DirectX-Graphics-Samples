#include "GridTerrain.hlsli"

float4 pswater(PS_INPUT In) : SV_Target
{
    float3 Normal = normalize(In.Normal.xyz);
    float3 PosToCamera = normalize(In.PosToCamera.xyz);
    float3 HalfAngle = (PosToCamera + vInverseLightDirection) * 0.5;
    float HalfDotNormal = dot(HalfAngle, Normal);
    float4 Specular = pow(abs(HalfDotNormal), 16) * float4(1, 1, 1, 0);
    float4 Diffuse = float4(0.15, 0.15, 1.0, 0.75 * saturate(In.Blend.w * 5));

    float EdgeWave = In.Blend.w * 0.25;
    if (EdgeWave > 1.0)
    {
        EdgeWave = 0.0;
    }
    //Diffuse = saturate(Diffuse + float4(EdgeWave, EdgeWave, EdgeWave, 0));

    //return Specular + Diffuse;
    return Diffuse;
}
