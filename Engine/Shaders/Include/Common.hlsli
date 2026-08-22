// Shared declarations for LimeEngine shaders.
//
// Register spaces map onto Vulkan bindings through nvrhi::VulkanBindingOffsets, mirrored by the
// register shifts passed to DXC (see LIME_VK_SHIFT_* in the root CMakeLists.txt):
//   t# -> +0    s# -> +128    b# -> +256    u# -> +384
// Constant buffer matrices are declared row_major so they match FMatrix4x4 on the CPU without a
// transpose. No global packing flag is used: DXC's SPIR-V backend ignores -Zpr, and DXC maps HLSL
// row_major onto the SPIR-V ColMajor decoration, so relying on the qualifier keeps both backends
// consistent.

#ifndef LIME_COMMON_HLSLI
#define LIME_COMMON_HLSLI

static const float LimePi = 3.14159265358979323846f;

float3 LimeSrgbToLinear(float3 Color)
{
	return select(Color <= 0.04045f, Color / 12.92f, pow(abs(Color + 0.055f) / 1.055f, 2.4f));
}

float3 LimeLinearToSrgb(float3 Color)
{
	return select(Color <= 0.0031308f, Color * 12.92f, 1.055f * pow(abs(Color), 1.0f / 2.4f) - 0.055f);
}

#endif // LIME_COMMON_HLSLI
