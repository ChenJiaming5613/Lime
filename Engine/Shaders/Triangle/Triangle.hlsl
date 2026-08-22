// Rotating triangle. Vertex colours are interpolated across the face.

#include "Common.hlsli"

cbuffer FTriangleConstants : register(b0)
{
	// row_major matches the row major FMatrix4x4 on the CPU. Note that DXC maps HLSL row_major onto
	// the SPIR-V ColMajor decoration; the terminology is inverted between the two but the memory
	// layout is the same, so no CPU side transpose is needed on either backend.
	row_major float4x4 WorldViewProjection;
	float4 Tint;
};

struct FVertexInput
{
	float3 Position : POSITION;
	float4 Color : COLOR;
};

struct FVertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
};

FVertexOutput MainVS(FVertexInput Input)
{
	FVertexOutput Output;
	Output.Position = mul(WorldViewProjection, float4(Input.Position, 1.0f));
	Output.Color = Input.Color * Tint;
	return Output;
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	return Input.Color;
}
