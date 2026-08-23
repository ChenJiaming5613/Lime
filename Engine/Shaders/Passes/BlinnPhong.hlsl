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

	return float4(Ambient + Diffuse + SpecularTerm, BaseColor.a);
}
