// Physically based forward shading, in the formulation Unreal Engine uses. Shared by the D3D12 and Vulkan
// backends.
//
// The BRDF itself lives in Include/BRDF.hlsli, so a later deferred path evaluates the same one. This file is
// only the plumbing: sample the maps, build the surface, run the lights.
//
// Vertex layout must match FStaticMeshVertex: float3 position, float3 normal, float4 tangent, float2
// texcoord.
//
// Every matrix is declared row_major explicitly rather than relying on the -Zpr compiler flag: DXC's
// SPIR-V backend ignores that flag, so a shader that depends on it works on D3D12 and silently
// transposes on Vulkan.

#include "BRDF.hlsli"

// Per frame: the same for every draw in the pass.
cbuffer FForwardFrameConstants : register(b0)
{
	row_major float4x4 ViewProjection;
	float3 CameraPosition;
	float CameraPadding;
	// Direction the light travels, pointing away from the source. Normalized on the CPU so the pixel
	// shader does not renormalize per pixel.
	float3 LightDirection;
	float LightIntensity;
	float3 LightColor;
	float LightColorPadding;
	// Stands in for image based lighting. See LimeEvaluateAmbient.
	float3 AmbientColor;
	float AmbientPadding;
	// World to the shadow caster's clip space. Must be the matrix that pass rendered with, or the lookup
	// samples the wrong texel.
	row_major float4x4 LightViewProjection;
	// 0 when no shadow map is connected, in which case the lookup is skipped entirely and everything is
	// lit. The graph makes that input optional, so this is a supported configuration.
	float ShadowStrength;
	float ShadowPadding[3];
};

// Per draw.
cbuffer FForwardDrawConstants : register(b1)
{
	row_major float4x4 World;
	// Inverse transpose of World, so normals stay perpendicular to the surface under a non uniform
	// scale. Passing World here instead would tilt them and light the model incorrectly.
	row_major float4x4 NormalMatrix;
	float4 BaseColorFactor;
	float3 EmissiveFactor;
	// Alpha below this is discarded. Zero disables the test, which is the common case.
	float AlphaCutoff;
	float MetallicFactor;
	float RoughnessFactor;
	// Scales the mapped normal's tangential components, from the material's normalTexture.scale. 1 leaves
	// the map as authored; 0 flattens it back to the vertex normal, which is also how a material with no
	// normal map is expressed.
	float NormalScale;
	// From the material's occlusionTexture.strength. 0 ignores the map entirely.
	float OcclusionStrength;
};

Texture2D BaseColorTexture : register(t0);
SamplerState MaterialSampler : register(s0);

// Always bound, because a shader cannot have an optional resource: when no shadow caster is connected the
// pass binds a dummy and sets ShadowStrength to 0, so nothing here reads it.
Texture2D ShadowDepthTexture : register(t1);
// A comparison sampler rather than a plain one. SampleCmpLevelZero tests the depth and filters the
// results, which gives four-tap smoothing from one instruction; filtering the depths themselves and then
// comparing would be wrong, since an average depth is not the average of the comparisons.
SamplerComparisonState ShadowSampler : register(s1);

// The remaining material maps. A material lacking any of these is bound a 1x1 texture holding that slot's
// neutral value, so there is no branch and no second pipeline variant.
Texture2D NormalTexture : register(t2);
Texture2D MetallicRoughnessTexture : register(t3);
Texture2D EmissiveTexture : register(t4);
Texture2D OcclusionTexture : register(t5);

struct FVertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	// W is the bitangent's handedness, +1 or -1, not a homogeneous coordinate.
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

struct FVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD1;
	float3 Normal : NORMAL;
	// The tangent frame, already in world space and with the handedness resolved.
	//
	// The bitangent is interpolated rather than rebuilt in the pixel shader from cross(N, T) * w. Rebuilding
	// is the usual shortcut and it is wrong whenever the world matrix mirrors, which a negative node scale
	// does: a cross product picks up the determinant of the transform applied to its operands, so the
	// rebuilt vector would point the wrong way on exactly those meshes. Building it in object space, where
	// the handedness sign was authored, and transforming it like any other tangential vector avoids the
	// question entirely for the cost of one interpolator.
	float3 Tangent : TANGENT;
	float3 Bitangent : BINORMAL;
	float2 TexCoord : TEXCOORD0;
};

// How much of the light reaches this point: 1 lit, 0 fully shadowed.
float SampleShadow(float3 WorldPosition)
{
	if (ShadowStrength <= 0.0f)
	{
		return 1.0f;
	}

	const float4 LightClip = mul(LightViewProjection, float4(WorldPosition, 1.0f));
	if (LightClip.w <= 0.0f)
	{
		// Behind the light's near plane, so it was never rendered into the map.
		return 1.0f;
	}

	const float3 Projected = LightClip.xyz / LightClip.w;

	// Clip space is [-1, 1] in x and y with y upwards; texture coordinates are [0, 1] with y downwards.
	float2 ShadowUV = Projected.xy * float2(0.5f, -0.5f) + 0.5f;

	// Outside the map means outside the fitted frustum, which is geometry the caster never saw. Treated as
	// lit rather than shadowed, so a scene larger than the frustum does not gain a hard shadow edge at the
	// boundary.
	if (any(ShadowUV < 0.0f) || any(ShadowUV > 1.0f) || Projected.z > 1.0f)
	{
		return 1.0f;
	}

	const float Visibility = ShadowDepthTexture.SampleCmpLevelZero(ShadowSampler, ShadowUV, Projected.z);

	// Scaled rather than used directly, so the strength can soften the shadow instead of only switching it.
	return lerp(1.0f, Visibility, ShadowStrength);
}

// The shading normal, after applying the tangent space map.
//
// Returns the vertex normal unchanged when the material has no map: the pass sets NormalScale to 0 in that
// case, which zeroes the tangential terms exactly and leaves only the Z one. That is cheaper than a branch
// and, unlike relying on the stand-in texture's value, it is exact: 0.5 is not representable in 8 bit
// UNORM, so the flat normal decodes to 0.004 rather than 0 and would tilt every unmapped surface slightly.
float3 ApplyNormalMap(FVertexOutput Input, float3 VertexNormal)
{
	const float3 Sampled = NormalTexture.Sample(MaterialSampler, Input.TexCoord).xyz * 2.0f - 1.0f;

	// Only X and Y are scaled, per the glTF definition of normalTexture.scale. Scaling Z as well would
	// change the strength by an amount that depends on how steep the map already is.
	const float3 TangentNormal = float3(Sampled.xy * NormalScale, Sampled.z);

	// Re-orthogonalised against the normal rather than used as interpolated. Interpolating three vectors
	// across a triangle does not preserve the right angles between them, and the error grows with how
	// heavily the normals were smoothed. The normal is the one held fixed because it is what the diffuse and
	// specular terms are built on.
	float3 Tangent = normalize(Input.Tangent);
	float3 Bitangent = normalize(Input.Bitangent);
	Tangent = normalize(Tangent - VertexNormal * dot(VertexNormal, Tangent));
	Bitangent = normalize(Bitangent - VertexNormal * dot(VertexNormal, Bitangent));

	// No negation of Y. glTF defines the bitangent as cross(normal, tangent) * w and the map's green channel
	// along it, and the importer derives its tangents from the same UV gradient that definition refers to,
	// so the two already agree. Flipping Y here is the fix for content authored against the other
	// convention, and applying it to glTF content instead turns every bump into a dent.
	return normalize(Tangent * TangentNormal.x + Bitangent * TangentNormal.y + VertexNormal * TangentNormal.z);
}

FVertexOutput MainVS(FVertexInput Input)
{
	FVertexOutput Output;

	const float4 WorldPosition = mul(World, float4(Input.Position, 1.0f));
	Output.WorldPosition = WorldPosition.xyz;
	Output.Position = mul(ViewProjection, WorldPosition);

	// Not normalized here: interpolation across the triangle denormalizes it anyway, so it is done once
	// in the pixel shader instead.
	Output.Normal = mul(NormalMatrix, float4(Input.Normal, 0.0f)).xyz;

	// World rather than NormalMatrix, and the difference matters under a non uniform scale. A tangent lies
	// along the surface and transforms like a direction between two points on it, which is what World does.
	// A normal is perpendicular to the surface and transforms by the inverse transpose instead. Using
	// NormalMatrix here would shear the frame away from the texture's U direction on any scaled node.
	Output.Tangent = mul(World, float4(Input.Tangent.xyz, 0.0f)).xyz;

	// Built in object space, where W was authored, then transformed like the tangent. See FVertexOutput.
	const float3 ObjectBitangent = cross(Input.Normal, Input.Tangent.xyz) * Input.Tangent.w;
	Output.Bitangent = mul(World, float4(ObjectBitangent, 0.0f)).xyz;

	Output.TexCoord = Input.TexCoord;
	return Output;
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	const float4 BaseColor = BaseColorFactor * BaseColorTexture.Sample(MaterialSampler, Input.TexCoord);

	// Cutout materials. With a cutoff of zero this never discards, so the common path costs one
	// comparison. Done before any shading, so a discarded pixel costs nothing beyond this sample.
	if (AlphaCutoff > 0.0f && BaseColor.a < AlphaCutoff)
	{
		discard;
	}

	const float3 ViewDirection = normalize(CameraPosition - Input.WorldPosition);

	// Mapped in the frame as authored, before the back face correction below. Correcting first would flip
	// the frame's handedness and mirror the map's green channel on those faces.
	float3 Normal = ApplyNormalMap(Input, normalize(Input.Normal));

	// Back faces present normals pointing away from the viewer, because culling is disabled: glTF winding
	// flips with a negative node scale. Flipping the normal keeps those faces lit instead of black.
	if (dot(Normal, ViewDirection) < 0.0f)
	{
		Normal = -Normal;
	}

	// glTF packs this map as G for roughness and B for metalness, leaving R and A unused. The order is easy
	// to get backwards, and doing so produces an image that still looks plausible: rough metal and smooth
	// dielectric are both common, so nothing obviously breaks.
	const float2 MetallicRoughness = MetallicRoughnessTexture.Sample(MaterialSampler, Input.TexCoord).bg;

	FLimeSurface Surface;
	Surface.BaseColor = BaseColor.rgb;
	Surface.Metallic = saturate(MetallicRoughness.x * MetallicFactor);
	Surface.Roughness = saturate(MetallicRoughness.y * RoughnessFactor);
	Surface.Normal = Normal;

	// Occlusion sits in R, which is what lets one texture serve all three: an ORM map packs occlusion,
	// roughness and metalness into the three channels, and Khronos assets ship exactly that.
	const float SampledOcclusion = OcclusionTexture.Sample(MaterialSampler, Input.TexCoord).r;
	// Interpolated from 1 rather than multiplied, per the glTF definition of occlusionTexture.strength: at
	// strength 0 the map has no effect at all, which multiplying could not express.
	Surface.Occlusion = lerp(1.0f, SampledOcclusion, OcclusionStrength);

	// LightDirection travels away from the source, so the vector towards the light is its negation.
	const float3 ToLight = -LightDirection;

	// One directional light. Radiance rather than an arbitrary scale: the BRDF returns a ratio, so the light
	// has to supply the actual incoming energy.
	const float3 Radiance = LightColor * LightIntensity;
	const float3 Direct = LimeEvaluateBRDF(Surface, ViewDirection, ToLight) * Radiance;

	// Applied to the direct term only, for the same reason occlusion applies only to ambient: each accounts
	// for a different part of the incoming light, and applying either to both double counts the darkening.
	const float Visibility = SampleShadow(Input.WorldPosition);

	const float3 Ambient = LimeEvaluateAmbient(Surface, AmbientColor, ViewDirection);

	// Emission is added last and is affected by neither shadowing nor occlusion: a surface that emits light
	// does so regardless of what reaches it.
	const float3 Emissive = EmissiveFactor * EmissiveTexture.Sample(MaterialSampler, Input.TexCoord).rgb;

	return float4(Direct * Visibility + Ambient + Emissive, BaseColor.a);
}
