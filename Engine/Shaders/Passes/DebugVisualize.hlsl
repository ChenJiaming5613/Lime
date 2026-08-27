// Debug visualiser: turns any texture the graph produced into something viewable.
//
// Exists because most render targets cannot be shown directly. A perspective depth buffer is the clearest
// case: it is non-linear, so nearly its whole range sits within a few values of 1 and displaying it raw
// gives a white rectangle. Single channel data such as roughness or occlusion has the opposite problem: it
// lands in the red channel and shows as a red tint rather than as the greyscale it represents.
//
// Shares the full screen triangle trick with PostProcess.hlsl: three vertices generated from the vertex
// id, so this pass owns no geometry and needs no vertex buffer.
//
// Kept to plain ASCII, like every other shader here. dxc reads these as narrow text and fails with
// ERROR_NO_UNICODE_TRANSLATION on anything else, which surfaces only as an opaque error code.

cbuffer FDebugVisualizeConstants : register(b0)
{
	// One per channel, 1 to show it and 0 to suppress it. Matches FDebugVisualizerSettings.
	//
	// First in the buffer because a float4 cannot straddle a 16 byte register boundary; placing it after
	// the scalars would make HLSL pad ahead of it and silently disagree with the C++ struct.
	float4 ChannelMask;
	// Non-zero when the source holds non-linear perspective depth and should be linearised first.
	// Decided from the connected resource's format rather than by hand, so connecting a depth target is
	// enough to get a sensible image.
	int LinearizeDepth;
	// Camera clip planes, needed to undo the perspective depth curve. Ignored when LinearizeDepth is 0.
	float NearPlane;
	float FarPlane;
	// The input range mapped onto 0..1 on the way out. Widening or narrowing it is what makes low contrast
	// data readable; the default passes values through unchanged.
	float RangeMin;
	float RangeMax;
	float3 Padding;
};

Texture2D SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

struct FVertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

FVertexOutput MainVS(uint VertexId : SV_VertexID)
{
	FVertexOutput Output;

	const float2 Position = float2((VertexId << 1) & 2, VertexId & 2);
	Output.TexCoord = Position;

	// Y is flipped because clip space runs upwards while texture coordinates run downwards.
	Output.Position = float4(Position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return Output;
}

// Recovers view space distance from a depth value written by a left handed perspective projection with a
// 0..1 range, then rescales it to 0..1 across the clip range.
//
// Derived from the projection rather than approximated: with m22 = f/(f-n) and m23 = -n*f/(f-n), the stored
// value is z' = (z*m22 + m23)/z, which inverts to z = n*f/(f - z'*(f-n)).
float LinearizeDepthValue(float DeviceDepth)
{
	const float Range = FarPlane - NearPlane;
	if (Range <= 0.0f)
	{
		return DeviceDepth;
	}

	// At the far plane the denominator reaches zero, which would produce an infinity that then shows as a
	// black pixel: the opposite of the white it should be.
	const float Denominator = FarPlane - DeviceDepth * Range;
	if (abs(Denominator) < 1.0e-6f)
	{
		return 1.0f;
	}

	const float ViewDepth = NearPlane * FarPlane / Denominator;
	return saturate((ViewDepth - NearPlane) / Range);
}

float4 MainPS(FVertexOutput Input) : SV_Target0
{
	const float4 Source = SourceTexture.Sample(SourceSampler, Input.TexCoord);

	float3 Color;
	if (LinearizeDepth != 0)
	{
		// Depth is handled before the mask, and ignores it, because a depth resource has one meaningful
		// channel: green and blue read as 0, and 0 linearises to the near plane rather than to black.
		// Running them through the mask is what made an unconfigured depth view come out pure red.
		Color = LinearizeDepthValue(Source.r).xxx;
	}
	else if (dot(ChannelMask, 1.0f) < 1.5f)
	{
		// Exactly one channel (or none), so the value is broadcast to grey rather than left in its own
		// slot. Showing a lone channel in place would tint the whole image -- red for roughness, green for
		// whatever sits in G -- and a tint is far harder to read a magnitude from than a greyscale ramp.
		//
		// dot picks whichever channel is enabled without branching per channel, and yields 0 when the mask
		// is empty, which shows as black. That is the honest result of turning everything off.
		Color = dot(Source, ChannelMask).xxx;
	}
	else
	{
		// More than one channel, so each keeps its own slot and the suppressed ones read as zero. This is
		// the case for comparing channels against each other rather than reading a value off one.
		//
		// Alpha has no slot of its own here. It is only displayable on its own, where the branch above
		// broadcasts it, so it is deliberately dropped rather than blended into the colour.
		Color = Source.rgb * ChannelMask.rgb;
	}

	// Guarded against an inverted or empty range, which would divide by zero and fill the screen with NaNs.
	// Treated as "no remap" rather than as an error, so the range stays safe to drag in the inspector.
	const float Span = RangeMax - RangeMin;
	if (abs(Span) > 1.0e-6f)
	{
		Color = saturate((Color - RangeMin) / Span);
	}

	// Opaque: this is what the editor samples, and a carried through alpha would make the viewport blend
	// with whatever is behind it.
	return float4(Color, 1.0f);
}
