// Dear ImGui draw list shader. Shared by the D3D12 and Vulkan backends.
// The vertex layout must match ImDrawVert exactly: float2 pos, float2 uv, uint8x4 unorm col.

cbuffer FImGuiConstants : register(b0)
{
	// row_major matches FMatrix4x4 on the CPU; see Common.hlsli for the SPIR-V naming caveat.
	row_major float4x4 Projection;
};

Texture2D Texture : register(t0);
SamplerState TextureSampler : register(s0);

struct FVertexInput
{
	float2 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
	float4 Color : COLOR0;
};

struct FVertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

FVertexOutput MainVS(FVertexInput Input)
{
	FVertexOutput Output;
	Output.Position = mul(Projection, float4(Input.Position, 0.0f, 1.0f));
	Output.Color = Input.Color;
	Output.TexCoord = Input.TexCoord;
	return Output;
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	return Input.Color * Texture.Sample(TextureSampler, Input.TexCoord);
}
