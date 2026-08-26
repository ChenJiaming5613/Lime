// Full screen post process: exposure and gamma. Shared by the D3D12 and Vulkan backends.
//
// Draws a single triangle covering the screen rather than a quad. Three vertices instead of six, no
// diagonal seam where two triangles meet, and no vertex buffer at all: the positions are generated from
// the vertex id, so this pass owns no geometry.

cbuffer FPostProcessConstants : register(b0)
{
	// Multiplies the scene colour before the gamma curve. 1 leaves it unchanged.
	float Exposure;
	// The curve applied on the way out. 2.2 is the usual sRGB approximation; 1 disables it.
	float Gamma;
	float2 Padding;
};

Texture2D SceneColorTexture : register(t0);
SamplerState SceneColorSampler : register(s0);

struct FVertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

FVertexOutput MainVS(uint VertexId : SV_VertexID)
{
	FVertexOutput Output;

	// A triangle twice the size of the screen, positioned so its inscribed region is exactly the viewport.
	// Interpolating texture coordinates over it gives 0..1 across the visible area with no seam.
	const float2 Position = float2((VertexId << 1) & 2, VertexId & 2);
	Output.TexCoord = Position;

	// Y is flipped because clip space runs upwards while texture coordinates run downwards.
	Output.Position = float4(Position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return Output;
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	float3 Color = SceneColorTexture.Sample(SceneColorSampler, Input.TexCoord).rgb;

	Color *= Exposure;

	// Guarded so a gamma of zero cannot produce infinities. Values at or below zero mean "no curve"
	// rather than an error, which keeps the setting safe to drag to its minimum in the inspector.
	if (Gamma > 0.0f)
	{
		Color = pow(saturate(Color), 1.0f / Gamma);
	}

	// Opaque: this is the last thing written to the target the editor samples, and a carried through alpha
	// would make the viewport blend with whatever is behind it.
	return float4(Color, 1.0f);
}
