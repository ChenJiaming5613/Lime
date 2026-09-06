// Depth-only pass for shadow map generation. Shared by the D3D12 and Vulkan backends.
//
// No pixel shader: the depth buffer is the entire output, so a pixel shader would only cost time. Alpha
// cutout materials would need one to discard, which is a limitation worth taking for now rather than
// paying the cost on every shadow caster.
//
// Vertex layout must match FStaticMeshVertex, because the same meshes are drawn here and in the lit pass:
// float3 position, float3 normal, float4 tangent, float2 texcoord. Everything but the position is unused
// but must be declared, since the input layout describes the whole vertex.
//
// Every matrix is declared row_major explicitly rather than relying on the -Zpr compiler flag: DXC's
// SPIR-V backend ignores that flag, so a shader that depends on it works on D3D12 and silently
// transposes on Vulkan.

cbuffer FShadowFrameConstants : register(b0)
{
	// World to the light's clip space. One matrix rather than separate view and projection, because
	// nothing here needs them apart.
	row_major float4x4 LightViewProjection;
};

cbuffer FShadowDrawConstants : register(b1)
{
	row_major float4x4 World;
};

struct FVertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

float4 MainVS(FVertexInput Input) : SV_Position
{
	const float4 WorldPosition = mul(World, float4(Input.Position, 1.0f));
	return mul(LightViewProjection, WorldPosition);
}
