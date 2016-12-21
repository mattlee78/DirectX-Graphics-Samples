#include "GridTerrain.hlsli"

float4 psmain( PS_INPUT_HI In ) : SV_Target                                      
{                                    
    float3 Normal = float3(0, 1, 0);
    //float ShadowIntensity = CascadeShadowHi(In.ShadowPos0, In.ShadowPos1, In.ShadowPos2, vShadowSize0);
    float ShadowIntensity = 1;
    //float4 Sample0 = tex2D.Sample(linearSampler, float3(In.TexCoord01.xy, In.Blend.z));
    //float4 Sample1 = tex2D.Sample(linearSampler, float3(In.TexCoord01.zw, In.Blend.w));
    //float4 Diffuse = lerp(Sample1, Sample0, In.Blend.x);
    float4 Diffuse = DiffuseTexture.Sample(linearSampler, In.TexCoord01.xy);

    float DirLightIntensity = saturate(dot(Normal.xyz, vInverseLightDirection)) * ShadowIntensity;
    float3 DirLightColor = vDirectionalLightColor * DirLightIntensity;
    float3 LitDiffuse = Diffuse.xyz * (DirLightColor + vAmbientLightColor);
    //LitDiffuse *= BlockColor;
    return float4(LitDiffuse, 1);
}                                                                              
