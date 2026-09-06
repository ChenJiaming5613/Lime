// Physically based BRDF terms, in the formulation Unreal Engine uses.
//
// Separate from any one pass because a shading model is not the property of a pass: a later deferred or
// visibility buffer path has to evaluate the same BRDF or the two would disagree about what a material
// looks like. Keeping the terms here means the shading model is defined once.
//
// The microfacet specular BRDF is
//
//   f = D(h) * G(l, v, h) * F(v, h) / (4 * NdotL * NdotV)
//
// with D the normal distribution, G the geometric shadowing and F the Fresnel reflectance. The 4 * NdotL *
// NdotV denominator is folded into the visibility term rather than divided out separately, which is what
// "Vis" means throughout: Vis = G / (4 * NdotL * NdotV). Folding it in is not just an optimisation, it also
// cancels terms that would otherwise divide by zero at grazing angles.
//
// Roughness is used as alpha = Roughness^2 everywhere, the "disney remapping". That reparameterisation is
// what makes a roughness slider feel linear to author; using roughness directly puts almost all of the
// visible change into the bottom of the range.

#ifndef LIME_BRDF_HLSLI
#define LIME_BRDF_HLSLI

#include "Common.hlsli"

// Lowest roughness the BRDF is evaluated at.
//
// A perfect mirror is a delta function: D goes to infinity over a vanishing solid angle, which no amount of
// float precision represents. The result is single pixel specular fireflies that survive tone mapping and
// flicker as the camera moves. Clamping trades a physically unreachable mirror for a stable image, which is
// what UE does for the same reason.
static const float LimeMinRoughness = 0.02f;

// Reflectance of a dielectric at normal incidence.
//
// 0.04 is the value glTF specifies and matches most non-metals: water is 0.02, common plastics sit near
// 0.05. Metals do not use this at all, taking their F0 from the base colour instead, which is why the two
// are mixed by metalness rather than chosen between.
static const float LimeDielectricF0 = 0.04f;

// GGX / Trowbridge-Reitz normal distribution.
//
// Chosen over Blinn or Beckmann for its tail: GGX falls off slowly, which is what reproduces the wide dim
// halo real rough surfaces show around a highlight. The sharper distributions cut off early and read as
// plastic.
float LimeD_GGX(float Alpha, float NdotH)
{
	const float Alpha2 = Alpha * Alpha;
	// Rearranged so the subtraction happens between values of similar magnitude. Written as
	// (NdotH * NdotH) * (Alpha2 - 1) + 1 it loses precision badly for small Alpha, where Alpha2 - 1 is close
	// to -1 and the product nearly cancels the added 1.
	const float Denominator = (NdotH * Alpha2 - NdotH) * NdotH + 1.0f;
	return Alpha2 / max(LimePi * Denominator * Denominator, 1.0e-7f);
}

// Smith visibility, height correlated, in UE's approximate form.
//
// The exact height correlated Smith term needs two square roots. This approximation replaces them with a
// pair of linear interpolations, which is accurate to well under a quantisation step over the whole
// roughness range and is what UE ships as Vis_SmithJointApprox.
//
// Height correlated rather than separable: the separable form treats masking and shadowing as independent,
// which they are not on a rough surface, and visibly over-darkens grazing angles.
float LimeVis_SmithJointApprox(float Alpha, float NdotV, float NdotL)
{
	const float VisSmithV = NdotL * (NdotV * (1.0f - Alpha) + Alpha);
	const float VisSmithL = NdotV * (NdotL * (1.0f - Alpha) + Alpha);
	return 0.5f * rcp(max(VisSmithV + VisSmithL, 1.0e-7f));
}

// Schlick's Fresnel approximation.
//
// Exact Fresnel needs the complex index of refraction per wavelength, which no real time renderer carries.
// Schlick reproduces the shape from F0 alone and is indistinguishable in practice except on a few metals at
// glancing angles.
float3 LimeF_Schlick(float3 F0, float VdotH)
{
	// pow(1 - VdotH, 5) written as three multiplies. The intrinsic would compute exp2(5 * log2(x)), which is
	// both slower and wrong at x = 0, where log2 is undefined.
	const float OneMinus = saturate(1.0f - VdotH);
	const float OneMinus2 = OneMinus * OneMinus;
	const float Fc = OneMinus2 * OneMinus2 * OneMinus;
	return F0 + (1.0f - F0) * Fc;
}

// Lambert diffuse, the divide by pi included.
//
// UE's default. Burley and Oren-Nayar are available there as options, but they cost more and the difference
// only shows on strongly retroreflective materials like unfinished cloth.
float3 LimeDiffuse_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * (1.0f / LimePi);
}

// The material properties shading needs, after the maps have been sampled and the factors applied.
struct FLimeSurface
{
	// Linear, with the base colour factor already multiplied in.
	float3 BaseColor;
	float Metallic;
	// Perceptual roughness, before the alpha = Roughness^2 remapping.
	float Roughness;
	// 1 lets all ambient through, 0 blocks it.
	float Occlusion;
	// World space, after normal mapping.
	float3 Normal;
};

// The share of incoming light that scatters diffusely.
//
// Metals have none: their free electrons absorb what is not reflected, so the base colour describes their
// specular tint instead. Interpolating rather than branching keeps a partially metallic texel, which
// authored metalness maps produce along every transition.
float3 LimeGetDiffuseColor(FLimeSurface Surface)
{
	return Surface.BaseColor * (1.0f - Surface.Metallic);
}

// Reflectance at normal incidence.
//
// Dielectrics get the flat 0.04; metals take the base colour, which is what makes gold's highlight yellow
// rather than white.
float3 LimeGetF0(FLimeSurface Surface)
{
	return lerp(float3(LimeDielectricF0, LimeDielectricF0, LimeDielectricF0), Surface.BaseColor, Surface.Metallic);
}

// One light's contribution, diffuse and specular together, excluding shadowing and the light's colour.
//
// Returns radiance already multiplied by NdotL, so the caller does not have to remember to. Forgetting that
// cosine is a common error and it makes every surface look uniformly lit regardless of its orientation.
float3 LimeEvaluateBRDF(FLimeSurface Surface, float3 ViewDirection, float3 ToLight)
{
	const float3 Normal = Surface.Normal;
	const float NdotL = saturate(dot(Normal, ToLight));

	// Nothing to compute for a surface facing away. Also guards the division inside the visibility term,
	// which is only well behaved for positive NdotL.
	if (NdotL <= 0.0f)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}

	// Clamped away from zero rather than saturated: a normal mapped texel can face slightly away from the
	// viewer while still being lit, and letting NdotV reach zero there makes the visibility term explode.
	const float NdotV = max(dot(Normal, ViewDirection), 1.0e-4f);

	const float3 HalfVector = normalize(ToLight + ViewDirection);
	const float NdotH = saturate(dot(Normal, HalfVector));
	const float VdotH = saturate(dot(ViewDirection, HalfVector));

	const float Roughness = max(Surface.Roughness, LimeMinRoughness);
	const float Alpha = Roughness * Roughness;

	const float D = LimeD_GGX(Alpha, NdotH);
	const float Vis = LimeVis_SmithJointApprox(Alpha, NdotV, NdotL);
	const float3 F = LimeF_Schlick(LimeGetF0(Surface), VdotH);

	const float3 Specular = D * Vis * F;

	// Scaled by 1 - F, so the energy the specular lobe reflects is not also scattered diffusely. Omitting it
	// makes metals and grazing angles brighter than the light arriving on them, which reads as a glow.
	const float3 Diffuse = LimeDiffuse_Lambert(LimeGetDiffuseColor(Surface)) * (1.0f - F);

	return (Diffuse + Specular) * NdotL;
}

// A crude stand-in for image based lighting.
//
// Real IBL needs a prefiltered environment map and the split sum approximation's BRDF lookup table, neither
// of which this renderer has yet. A flat ambient colour is not physically meaningful, but the alternative is
// leaving everything facing away from the single light at black, which reads as a silhouette rather than as
// a shape.
//
// Written so that replacing it later is local: the diffuse and specular halves are already separated the way
// an IBL implementation needs, so only the two sources change.
float3 LimeEvaluateAmbient(FLimeSurface Surface, float3 AmbientColor, float3 ViewDirection)
{
	const float NdotV = saturate(dot(Surface.Normal, ViewDirection));

	// Fresnel against the normal rather than a half vector, since ambient arrives from every direction and
	// there is no single one to build a half vector from. This is the term that keeps the edges of a smooth
	// object bright, which is most of what makes it read as a solid rather than a flat shape.
	const float3 F = LimeF_Schlick(LimeGetF0(Surface), NdotV);

	// Roughness attenuates the ambient specular, standing in for a prefiltered mip: a rough surface spreads
	// the same energy over a wider lobe, so less of it reaches the eye from any one direction.
	const float3 AmbientSpecular = AmbientColor * F * (1.0f - Surface.Roughness);
	const float3 AmbientDiffuse = AmbientColor * LimeGetDiffuseColor(Surface) * (1.0f - F);

	// Occlusion applies to ambient alone. It describes how much of the surrounding hemisphere is blocked,
	// which says nothing about the direct light: that is the shadow map's job, and darkening it here as well
	// would make creases read as holes.
	return (AmbientDiffuse + AmbientSpecular) * Surface.Occlusion;
}

#endif // LIME_BRDF_HLSLI
