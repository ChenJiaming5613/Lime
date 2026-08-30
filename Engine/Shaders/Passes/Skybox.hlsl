// Skybox: composites an equirectangular environment map behind the scene.
//
// The pass renders a cube whose vertex positions are the sampling directions, then composites the scene
// over it. The scene colour and depth arrive as graph inputs (forward lit's colour and depth): where the
// depth says the scene wrote geometry the scene colour wins, everywhere else the skybox shows. The scene
// depth is optional - without it the whole frame is treated as background and the skybox fills it.
//
// Kept to plain ASCII, like every other shader here: dxc reads these as narrow text and fails on anything
// else.

#define PI 3.14159265358979

cbuffer FSkyboxConstants : register(b0)
{
	row_major float4x4 Projection;
	row_major float4x4 View;
	// Screen size in pixels, so the pixel position can be turned into a [0, 1] texture coordinate for the
	// scene colour and depth.
	float2 ViewportSize;
	// 1 when both the scene colour and depth are connected, in which case the skybox is composited behind
	// them; 0 otherwise, in which case the skybox fills the frame.
	float bComposite;
	// Multiplies the skybox radiance; 1 leaves it unchanged.
	float Exposure;
};

Texture2D EnvironmentTexture : register(t0);
SamplerState EnvironmentSampler : register(s0);
Texture2D SceneColorTexture : register(t1);
SamplerState SceneColorSampler : register(s1);
Texture2D SceneDepthTexture : register(t2);
SamplerState SceneDepthSampler : register(s2);

struct FVertexInput
{
	float3 Position : POSITION;
};

struct FVertexOutput
{
	float4 Position : SV_Position;
	float3 LocalPos : TEXCOORD0;
};

FVertexOutput MainVS(FVertexInput Input)
{
	FVertexOutput Output;

	// The cube's vertex is the direction this vertex looks along in world space.
	Output.LocalPos = Input.Position;

	// Keep only the rotation: zeroing the translation column keeps the skybox centred on the camera.
	float4x4 RotView = View;
	RotView[0][3] = 0.0f;
	RotView[1][3] = 0.0f;
	RotView[2][3] = 0.0f;

	Output.Position = mul(Projection, mul(RotView, float4(Input.Position, 1.0f)));
	return Output;
}

// Longitude/latitude mapping of a world space direction, with the camera's forward (-Z in this left
// handed engine) centred at u = 0.5.
float2 DirectionToEquirect(float3 Direction)
{
	return float2(atan2(Direction.x, -Direction.z) / (2.0f * PI) + 0.5f,
	              0.5f - asin(clamp(Direction.y, -1.0f, 1.0f)) / PI);
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	const float3 SkyboxColor = EnvironmentTexture.Sample(EnvironmentSampler, DirectionToEquirect(normalize(Input.LocalPos))).rgb * Exposure;

	float3 Color = SkyboxColor;
	if (bComposite > 0.5f)
	{
		// SV_Position is the pixel position, so dividing by the viewport size gives the same UV the scene
		// was rendered with.
		const float2 ScreenUV = Input.Position.xy / ViewportSize;

		// A depth of 1 is the far plane, which is what the scene target clears to: it marks pixels the
		// scene did not cover. Anything strictly less means geometry, so the scene colour wins there.
		// Compared against 1.0 exactly, not a looser threshold, because the clear value is exactly 1.0 and
		// any covered pixel is strictly less; a looser threshold would eat distant geometry.
		const float SceneDepth = SceneDepthTexture.Sample(SceneDepthSampler, ScreenUV).r;
		if (SceneDepth < 1.0f)
		{
			Color = SceneColorTexture.Sample(SceneColorSampler, ScreenUV).rgb;
		}
	}

	// Opaque: this is what the editor samples, and a carried through alpha would blend with whatever is
	// behind the viewport.
	return float4(Color, 1.0f);
}
