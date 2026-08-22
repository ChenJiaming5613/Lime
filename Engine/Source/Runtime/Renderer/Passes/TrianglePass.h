// Draws a single rotating triangle. Serves as the cross backend smoke test.

#pragma once

#include "Renderer/RenderTypes.h"

namespace Lime
{
	struct FTriangleSettings
	{
		float RotationSpeed = 1.0f;
		FVector4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float FovDegrees = 60.0f;
		bool bPaused = false;
	};

	class FTrianglePass final : public IRenderPass
	{
	public:
		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		void Render(const FFrameContext& Context) override;
		void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) override;

		// Advances the rotation; the application decides how the angle evolves.
		void Update(float DeltaSeconds);

		FTriangleSettings& GetSettings() { return Settings; }
		const FTriangleSettings& GetSettings() const { return Settings; }
		float GetRotationRadians() const { return RotationRadians; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;

		FTriangleSettings Settings;
		float RotationRadians = 0.0f;
	};
} // namespace Lime
