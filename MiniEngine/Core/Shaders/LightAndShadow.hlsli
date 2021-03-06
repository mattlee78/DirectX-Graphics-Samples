
Texture2D<float4> texDiffuse : register(t0);
Texture2D<float3> texSpecular : register(t1);
Texture2D<float3> texNormal : register(t2);
//Texture2D<float4> texEmissive : register(t3);
//Texture2D<float4> texLightmap : register(t4);
//Texture2D<float4> texReflection : register(t5);
Texture2D<float> texSSAO : register(t64);
Texture2D<float> texShadow : register(t65);
Texture2D<float> texShadowOuter : register(t66);

cbuffer LightShadowWorldConstants : register(b0)
{
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    uint _pad;
    float  ShadowTexelSize;
}

SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s15);

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
    float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
    specular = lerp(specular, 1, fresnel);
    diffuse = lerp(diffuse, 0, fresnel);
}

float3 ApplyAmbientLight(
    float3 diffuse,		// Diffuse albedo
    float ao,			// Pre-computed ambient-occlusion
    float3 lightColor	// Radiance of ambient light
)
{
    return ao * diffuse * lightColor;
}

float GetShadow(float3 ShadowCoord)
{
#ifdef SINGLE_SAMPLE
    float result = texShadow.SampleCmpLevelZero(ShadowSampler, ShadowCoord.xy, ShadowCoord.z);
#else
    const float Dilation = 2.0;
    float d1 = Dilation * ShadowTexelSize * 0.125;
    float d2 = Dilation * ShadowTexelSize * 0.875;
    float d3 = Dilation * ShadowTexelSize * 0.625;
    float d4 = Dilation * ShadowTexelSize * 0.375;
    float result = (
        2.0 * texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d1, -d2), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d2, -d1), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d3, -d4), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d4, -d3), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)
        ) / 10.0;
#endif
    return result * result;
}

float GetShadowSingle(float3 ShadowCoord)
{
    float result = texShadowOuter.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z);
    return result * result;
}

float CascadedShadow(float3 shadowCoordInner, float3 shadowCoordOuter)
{
    float minVal = 0.01;
    float maxVal = 1 - minVal;
    if (shadowCoordInner.x <= maxVal && shadowCoordInner.y <= maxVal && 
        shadowCoordInner.x >= minVal && shadowCoordInner.y >= minVal)
    {
        return GetShadow(shadowCoordInner);
    }
    return GetShadowSingle(shadowCoordOuter);
}

float3 ApplyDirectionalLight(
    float3 diffuseColor,	// Diffuse albedo
    float3 specularColor,	// Specular albedo
    float specularMask,		// Where is it shiny or dingy?
    float gloss,			// Specular power
    float3 normal,			// World-space normal
    float3 viewDir,			// World-space vector from eye to point
    float3 lightDir,		// World-space vector from point to light
    float3 lightColor,		// Radiance of directional light
    float3 shadowCoord,     // Shadow coordinate (Shadow map UV & light-relative Z)
    float3 shadowCoordOuter	// Outer shadow coordinate (Shadow map UV & light-relative Z)
)
{
    // normal and lightDir are assumed to be pre-normalized
    float nDotL = dot(normal, lightDir);
    if (nDotL <= 0)
        return 0;

    // viewDir is also assumed normalized
    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = max(0, dot(halfVec, normal));

    FSchlick(diffuseColor, specularColor, lightDir, halfVec);

    float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;

#ifdef NO_SHADOW_MAP
    float shadow = 1.0f;
#else
    float shadow = CascadedShadow(shadowCoord, shadowCoordOuter);
#endif

    return shadow * nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
    float norm = length(texNormal);
    texNormal /= norm;
    gloss = lerp(1, gloss, norm);
}

float3 DefaultLightAndShadowModelNormal(
    float3 diffuseAlbedo,
    float3 specularAlbedo,
    float specularMask,
    float3 ModelNormal,
    uint2 InputScreenPositionXY,
    float3 InputViewDir,
    float3 InputShadowCoord,
    float3 InputShadowCoordOuter
)
{
    float gloss = 128.0;
    AntiAliasSpecular(ModelNormal, gloss);

    float ao = texSSAO[InputScreenPositionXY];
    float3 ambientContribution = ApplyAmbientLight(diffuseAlbedo, ao, AmbientColor);

    float3 viewDir = normalize(InputViewDir);
    float3 sunlightContribution = ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, ModelNormal, viewDir, SunDirection, SunColor, InputShadowCoord, InputShadowCoordOuter);

    return ambientContribution + sunlightContribution;
    //return ambientContribution;
    //return sunlightContribution;
}

float3 DefaultLightAndShadow(
    float3 diffuseAlbedo,
    float3 specularAlbedo,
    float specularMask,
    float3 normal,
    uint2 InputScreenPositionXY,
    float3 InputViewDir,
    float3 InputTangent,
    float3 InputBitangent,
    float3 InputNormal,
    float3 InputShadowCoord,
    float3 InputShadowCoordOuter
)
{
    float gloss = 128.0;
    AntiAliasSpecular(normal, gloss);
    float3x3 tbn = float3x3(normalize(InputTangent), normalize(InputBitangent), normalize(InputNormal));
    normal = normalize(mul(normal, tbn));

    float ao = texSSAO[InputScreenPositionXY];
    float3 ambientContribution = ApplyAmbientLight(diffuseAlbedo, ao, AmbientColor);

    float3 viewDir = normalize(InputViewDir);
    float3 sunlightContribution = ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, InputShadowCoord, InputShadowCoordOuter);

    return ambientContribution + sunlightContribution;
}

float4 DefaultMaterialLightAndShadow(
    float2 InputTexCoord0,
    uint2 InputScreenPositionXY,
    float3 InputViewDir,
    float3 InputTangent,
    float3 InputBitangent,
    float3 InputNormal,
    float3 InputShadowCoord,
    float3 InputShadowCoordOuter
)
{
	float4 diffuseAll = texDiffuse.Sample(sampler0, InputTexCoord0);
	float3 diffuseAlbedo = diffuseAll.rgb;
    float3 specularAlbedo = float3(0.56, 0.56, 0.56);
    float specularMask = texSpecular.Sample(sampler0, InputTexCoord0).g;
    float3 normal = texNormal.Sample(sampler0, InputTexCoord0) * 2.0 - 1.0;

    float3 LitResult = DefaultLightAndShadow(diffuseAlbedo, specularAlbedo, specularMask, normal, InputScreenPositionXY, InputViewDir, InputTangent, InputBitangent, InputNormal, InputShadowCoord, InputShadowCoordOuter);
	return float4(LitResult, diffuseAll.a);
}

float DefaultMaterialAlphaOnly(float2 InputTexCoord0)
{
    float4 diffuseAlbedo = texDiffuse.Sample(sampler0, InputTexCoord0);
    return diffuseAlbedo.a;
}
