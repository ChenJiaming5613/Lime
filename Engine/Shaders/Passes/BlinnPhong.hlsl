// Blinn-Phong shading for imported meshes. Shared by the D3D12 and Vulkan backends.
//
// Vertex layout must match FStaticMeshVertex: float3 position, float3 normal, float2 texcoord.
//
// Every matrix is declared row_major explicitly rather than relying on the -Zpr compiler flag: DXC's
// SPIR-V backend ignores that flag, so a shader that depends on it works on D3D12 and silently
// transposes on Vulkan.

// Per frame: the same for every draw in the pass.
cbuffer FSceneFrameConstants : register(b0)
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
	// Flat term standing in for bounced light. Without it, surfaces facing away from the single light
	// would be pure black and the model would read as a silhouette.
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
cbuffer FSceneDrawConstants : register(b1)
{
	row_major float4x4 World;
	// Inverse transpose of World, so normals stay perpendicular to the surface under a non uniform
	// scale. Passing World here instead would tilt them and light the model incorrectly.
	row_major float4x4 NormalMatrix;
	float4 BaseColorFactor;
	// Alpha below this is discarded. Zero disables the test, which is the common case.
	float AlphaCutoff;
	float SpecularPower;
	float2 DrawPadding;
};

Texture2D BaseColorTexture : register(t0);
SamplerState BaseColorSampler : register(s0);

// Always bound, because a shader cannot have an optional resource: when no shadow caster is connected the
// pass binds a dummy and sets ShadowStrength to 0, so nothing here reads it.
Texture2D ShadowDepthTexture : register(t1);
// A comparison sampler rather than a plain one. SampleCmpLevelZero tests the depth and filters the
// results, which gives four-tap smoothing from one instruction; filtering the depths themselves and then
// comparing would be wrong, since an average depth is not the average of the comparisons.
SamplerComparisonState ShadowSampler : register(s1);

struct FVertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD0;
};

struct FVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD1;
	float3 Normal : NORMAL;
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

FVertexOutput MainVS(FVertexInput Input)
{
	FVertexOutput Output;

	const float4 WorldPosition = mul(World, float4(Input.Position, 1.0f));
	Output.WorldPosition = WorldPosition.xyz;
	Output.Position = mul(ViewProjection, WorldPosition);

	// Not normalized here: interpolation across the triangle denormalizes it anyway, so it is done once
	// in the pixel shader instead.
	Output.Normal = mul(NormalMatrix, float4(Input.Normal, 0.0f)).xyz;
	Output.TexCoord = Input.TexCoord;
	return Output;
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	float4 BaseColor = BaseColorFactor * BaseColorTexture.Sample(BaseColorSampler, Input.TexCoord);

	// Cutout materials. With a cutoff of zero this never discards, so the common path costs one
	// comparison.
	if (AlphaCutoff > 0.0f && BaseColor.a < AlphaCutoff)
	{
		discard;
	}

	float3 Normal = normalize(Input.Normal);
	const float3 ViewDirection = normalize(CameraPosition - Input.WorldPosition);

	// Back faces present normals pointing away from the viewer, because culling is disabled: glTF winding
	// flips with a negative node scale. Flipping the normal keeps those faces lit instead of black.
	if (dot(Normal, ViewDirection) < 0.0f)
	{
		Normal = -Normal;
	}

	// LightDirection travels away from the source, so the vector towards the light is its negation.
	const float3 ToLight = -LightDirection;
	const float NdotL = saturate(dot(Normal, ToLight));

	// Blinn-Phong: the half vector between the light and the view, rather than a reflected vector. It is
	// cheaper and avoids the harsh cutoff plain Phong shows at grazing angles.
	const float3 HalfVector = normalize(ToLight + ViewDirection);
	const float Specular = NdotL > 0.0f ? pow(saturate(dot(Normal, HalfVector)), SpecularPower) : 0.0f;

	const float3 Diffuse = BaseColor.rgb * LightColor * (NdotL * LightIntensity);
	// White rather than tinted by the base colour, which is how a dielectric highlight behaves.
	const float3 SpecularTerm = LightColor * (Specular * LightIntensity * 0.25f);
	const float3 Ambient = BaseColor.rgb * AmbientColor;

	// Applied to the direct terms only. Ambient stands in for light arriving from everywhere, which a
	// single shadow map says nothing about, so darkening it too would make shadowed areas read as holes.
	const float Visibility = SampleShadow(Input.WorldPosition);

	return float4(Ambient + (Diffuse + SpecularTerm) * Visibility, BaseColor.a);
}
