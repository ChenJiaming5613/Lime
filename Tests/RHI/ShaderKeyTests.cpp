// Locks the contract between the C++ side and the file names ShaderMake writes. Breaking this
// mapping would make shader loading fail at runtime with no compile time signal.

#include "RHI/ShaderKey.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Lime;

TEST_CASE("Output names strip the extension and append non-default entry points", "[RHI][ShaderKey]")
{
	SECTION("A main entry point adds no suffix")
	{
		REQUIRE(MakeShaderMakeOutputName("Triangle/Triangle.hlsl", "main") == "Triangle/Triangle");
	}

	SECTION("Any other entry point is appended")
	{
		REQUIRE(MakeShaderMakeOutputName("Triangle/Triangle.hlsl", "MainVS") == "Triangle/Triangle_MainVS");
		REQUIRE(MakeShaderMakeOutputName("ImGui/ImGui.hlsl", "MainPS") == "ImGui/ImGui_MainPS");
	}

	SECTION("Nested paths are preserved and separators normalized")
	{
		REQUIRE(MakeShaderMakeOutputName("Passes/PostProcess/Bloom.hlsl", "MainCS") == "Passes/PostProcess/Bloom_MainCS");
		REQUIRE(MakeShaderMakeOutputName("Passes\\Bloom.hlsl", "MainCS") == "Passes/Bloom_MainCS");
	}

	SECTION("Sources without an extension and dotted directories are handled")
	{
		REQUIRE(MakeShaderMakeOutputName("Triangle/Triangle", "MainVS") == "Triangle/Triangle_MainVS");
		REQUIRE(MakeShaderMakeOutputName("v1.2/Shader.hlsl", "MainVS") == "v1.2/Shader_MainVS");
	}

	SECTION("An empty entry point behaves like main")
	{
		REQUIRE(MakeShaderMakeOutputName("Triangle/Triangle.hlsl", "") == "Triangle/Triangle");
	}
}

TEST_CASE("Backends map onto the directories and extensions the build writes", "[RHI][ShaderKey]")
{
	REQUIRE(std::string(GetShaderPlatformDirectory(ERHIBackend::D3D12)) == "DXIL");
	REQUIRE(std::string(GetShaderPlatformDirectory(ERHIBackend::Vulkan)) == "SPIRV");
	REQUIRE(std::string(GetShaderFileExtension(ERHIBackend::D3D12)) == ".dxil");
	REQUIRE(std::string(GetShaderFileExtension(ERHIBackend::Vulkan)) == ".spirv");
}

TEST_CASE("Relative paths match the on-disk layout", "[RHI][ShaderKey]")
{
	FShaderKey Key;
	Key.SourcePath = "Triangle/Triangle.hlsl";
	Key.EntryPoint = "MainVS";
	Key.Type = nvrhi::ShaderType::Vertex;

	REQUIRE(MakeShaderRelativePath(Key, ERHIBackend::D3D12) == "DXIL/Triangle/Triangle_MainVS.dxil");
	REQUIRE(MakeShaderRelativePath(Key, ERHIBackend::Vulkan) == "SPIRV/Triangle/Triangle_MainVS.spirv");
}

TEST_CASE("Cache keys distinguish shaders that share an output file", "[RHI][ShaderKey]")
{
	FShaderKey VertexKey;
	VertexKey.SourcePath = "Triangle/Triangle.hlsl";
	VertexKey.EntryPoint = "MainVS";
	VertexKey.Type = nvrhi::ShaderType::Vertex;

	FShaderKey PixelKey = VertexKey;
	PixelKey.EntryPoint = "MainPS";
	PixelKey.Type = nvrhi::ShaderType::Pixel;

	REQUIRE(MakeShaderCacheKey(VertexKey) != MakeShaderCacheKey(PixelKey));

	SECTION("The stage is part of the key even for a shared entry point")
	{
		FShaderKey SameEntryDifferentStage = VertexKey;
		SameEntryDifferentStage.Type = nvrhi::ShaderType::Pixel;
		REQUIRE(MakeShaderCacheKey(VertexKey) != MakeShaderCacheKey(SameEntryDifferentStage));
	}

	SECTION("Defines participate in the key")
	{
		FShaderKey WithDefine = VertexKey;
		WithDefine.Defines.emplace_back("USE_TEXTURE", "1");
		REQUIRE(MakeShaderCacheKey(VertexKey) != MakeShaderCacheKey(WithDefine));

		FShaderKey WithOtherValue = VertexKey;
		WithOtherValue.Defines.emplace_back("USE_TEXTURE", "0");
		REQUIRE(MakeShaderCacheKey(WithDefine) != MakeShaderCacheKey(WithOtherValue));
	}

	SECTION("Identical keys compare and hash consistently")
	{
		FShaderKey Copy = VertexKey;
		REQUIRE(Copy == VertexKey);
		REQUIRE(MakeShaderCacheKey(Copy) == MakeShaderCacheKey(VertexKey));
	}
}

TEST_CASE("Backend names round trip through the command line parser", "[RHI][Types]")
{
	ERHIBackend Backend = ERHIBackend::Vulkan;

	REQUIRE(TryParseBackend("d3d12", Backend));
	REQUIRE(Backend == ERHIBackend::D3D12);
	REQUIRE(TryParseBackend("DX12", Backend));
	REQUIRE(Backend == ERHIBackend::D3D12);
	REQUIRE(TryParseBackend("vulkan", Backend));
	REQUIRE(Backend == ERHIBackend::Vulkan);
	REQUIRE(TryParseBackend("VK", Backend));
	REQUIRE(Backend == ERHIBackend::Vulkan);

	REQUIRE_FALSE(TryParseBackend("metal", Backend));
	// A failed parse must leave the caller's value untouched.
	REQUIRE(Backend == ERHIBackend::Vulkan);

	REQUIRE(std::string(ToString(ERHIBackend::D3D12)) == "D3D12");
	REQUIRE(std::string(ToString(ERHIBackend::Vulkan)) == "Vulkan");
}

TEST_CASE("Vulkan binding shifts match the nvrhi defaults", "[RHI][Types]")
{
	// The shifts are also passed to DXC by the build; a mismatch silently breaks descriptor binding.
	const nvrhi::VulkanBindingOffsets Offsets = FVulkanBindingShifts::ToNvrhi();
	const nvrhi::VulkanBindingOffsets Defaults;

	REQUIRE(Offsets.shaderResource == Defaults.shaderResource);
	REQUIRE(Offsets.sampler == Defaults.sampler);
	REQUIRE(Offsets.constantBuffer == Defaults.constantBuffer);
	REQUIRE(Offsets.unorderedAccess == Defaults.unorderedAccess);

	REQUIRE(MakeBindingLayoutDesc(nvrhi::ShaderType::All).bindingOffsets.constantBuffer == FVulkanBindingShifts::ConstantBuffer);
}
