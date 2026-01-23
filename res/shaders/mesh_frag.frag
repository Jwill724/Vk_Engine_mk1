#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inWorldPos;

layout (location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;

// Normal mapping and pbr code is from newer more advanced project of mine 1/23/26.

// Normal mapping helpers
mat3 buildTBN_FromDerivatives(vec3 normalWS, vec3 worldPos, vec2 uv)
{
	vec3 dpdx = dFdx(worldPos);
	vec3 dpdy = dFdy(worldPos);

	vec2 dUVdx = dFdx(uv);
	vec2 dUVdy = dFdy(uv);

	vec3 tangentWS = dpdx * dUVdy.y - dpdy * dUVdx.y;

	float tangentLen2 = dot(tangentWS, tangentWS);

	// fallback frame
	vec3 fallbackTangent = normalize(
		abs(normalWS.y) < 0.999 ? cross(normalWS, vec3(0.0, 1.0, 0.0))
								: cross(normalWS, vec3(1.0, 0.0, 0.0))
	);
	vec3 fallbackBitangent = cross(normalWS, fallbackTangent);

	// stabilize / orthonormalize
	vec3 safeTangent = tangentWS - normalWS * dot(normalWS, tangentWS);
	float safeLen2 = dot(safeTangent, safeTangent);

	// Blend factor: 0 = fallback, 1 = derivative TBN
	float useDeriv = smoothstep(1e-12, 1e-8, min(tangentLen2, safeLen2));

	vec3 finalTangent = normalize(mix(fallbackTangent, safeTangent, useDeriv));
	vec3 finalBitangent = cross(normalWS, finalTangent);

	return mat3(finalTangent, finalBitangent, normalWS);
}
vec3 computeNormalMappedWS(vec3 geometricNormalWS, vec3 worldPos, vec2 uv, vec3 normalTex, float normalScale)
{
	mat3 tbn = buildTBN_FromDerivatives(geometricNormalWS, worldPos, uv);

	vec3 normalTS = normalTex * 2.0 - 1.0;

	normalTS.xy *= normalScale;
	normalTS = normalize(normalTS);

	return normalize(tbn * normalTS);
}

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float linearRough(float r) { return max(r * r, 0.001); }

// Specular AA
// Reduce sparkling/aliasing of specular highlights caused by
// high-frequency normal variation
float SpecularAA(float roughness, vec3 N)
{
	vec3 dndx = dFdx(N);
	vec3 dndy = dFdy(N);
	float variance = max(dot(dndx,dndx), dot(dndy,dndy));
	float r2 = roughness * roughness + variance;
	return sqrt(saturate(r2));
}

// Disney/Burley diffuse (what frostbite uses)
vec3 DisneyDiffuse(vec3 albedo, float linearRoughness, float NdotV, float NdotL, float LdotH)
{
	linearRoughness = clamp(linearRoughness, 0.0, 1.0);
	float energyBias   = mix(0.0, 0.5,  linearRoughness);
	float energyFactor = mix(1.0, 1.0 / 1.51, linearRoughness);
	float F_D90        = energyBias + 2.0 * LdotH * LdotH * linearRoughness;

	float F_L = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotL, 0.0, 1.0), 5.0);
	float F_V = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotV, 0.0, 1.0), 5.0);

	return albedo * (F_L * F_V * energyFactor) * (1.0/PI);
}

// Height-correlated Smith GGX visibility (Frostbite/UE style)
// Returns G2 / (4 * NdotV * NdotL), i.e. the "V" factor you multiply by D and F.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	float a = linearRough(roughness);
	float a2 = a * a;

	float gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
	float gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
	return 0.5 / max(gv + gl, 1e-6);
}

// Schlick Fresnel (Unreal fast pow variant)
float FRESNEL_POWER_UNREAL(vec3 V, vec3 H) {
	float vdh = dot(V,H);
	return (-5.55473 * vdh - 6.98316) * vdh;
}
vec3 F_SCHLICK(vec3 V, vec3 H, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(2.0, FRESNEL_POWER_UNREAL(V,H));
}

// GGX (Trowbridge-Reitz) NDF
float D_GGX(vec3 N, vec3 H, float roughness)
{
	float a = linearRough(roughness);
	float a2 = a * a;
	float NdotH  = saturate(dot(N,H));
	float NdotH2 = NdotH*NdotH;
	float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
	return a2 / max(PI * denom * denom, 1e-6);
}

// full microfacet spec term for direct lights
vec3 BRDF_Specular(float NdotV, float NdotL, vec3 N, vec3 V, vec3 H, vec3 F0, float roughness)
{
	float D = D_GGX(N, H, roughness);
	float Vv = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
	vec3 F = F_SCHLICK(V, H, F0);
	return (D * Vv) * F; // already includes the 1/(4 NdotV NdotL) via V
}

void main()
{
	vec3 albedo = inColor * texture(colorTex, inUV).rgb * materialData.colorFactors.xyz;
	float rough = texture(metalRoughTex, inUV).g * materialData.metal_rough_factors.y;
	float metal = texture(metalRoughTex, inUV).b * materialData.metal_rough_factors.x;

	rough = clamp(rough, 0.04, 1.0);
	metal = clamp(metal, 0.0, 1.0);

	vec3 N = normalize(inNormal);

	float normalStrength = materialData.normalScale;

	float dist = length(sceneData.cameraPos.xyz - inWorldPos.xyz);
	const float fadeStart = 1.0;
	const float fadeEnd = 8.0;
	float fade = 1.0 - smoothstep(fadeStart, fadeEnd, dist);
	normalStrength *= fade;

	vec2 uvDx = dFdx(inUV);
	vec2 uvDy = dFdy(inUV);

	vec3 normal = textureGrad(normalTex, inUV, uvDx, uvDy).xyz;

	N = computeNormalMappedWS(
		N,
		inWorldPos.xyz,
		inUV,
		normal,
		normalStrength
	);

	vec3 lightColor = sceneData.sunlightColor.rgb;

	vec3 V = normalize(sceneData.cameraPos.xyz - inWorldPos.xyz);
	vec3 L = normalize(sceneData.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	// Disney/Frostbite direct lighting
	rough = SpecularAA(rough, N);
	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);
	vec3 direct = (diff + spec) * lightColor * NdotL;

	// Final lighting
	vec3 ambient = albedo * sceneData.ambientColor.rgb;
	vec3 finalColor = direct + ambient;
	outFragColor = vec4(finalColor, 1.0);
}