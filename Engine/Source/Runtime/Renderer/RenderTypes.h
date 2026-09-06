// Shared renderer types.
//
// The pass abstraction itself lives in LimeRenderGraph, since the graph is what executes it. This header
// re-exports it so the many passes that include "Renderer/RenderTypes.h" keep compiling, and adds the
// vertex layouts, which are the renderer's own: the graph has no opinion on what a pass draws.

#pragma once

#include "Core/Math/Vector.h"
#include "RHI/RHITypes.h"
#include "RenderGraph/RenderGraphPass.h"

namespace Lime
{
	// Vertex layout used by the simple pipelines; matches POSITION/COLOR semantics in HLSL.
	struct FSimpleVertex
	{
		FVector3 Position;
		FVector4 Color;
	};

	// Vertex layout for imported meshes. Separate from FSimpleVertex rather than an extension of it,
	// because the two feed different pipelines and widening the simple one would change the vertex
	// stride of every existing pass.
	//
	// Only the attributes the basic lit shading needs: no tangents, since normal mapping is out of
	// scope and a tangent that is never read would waste bandwidth on every vertex.
	struct FStaticMeshVertex
	{
		FVector3 Position;
		FVector3 Normal;
		FVector2 TexCoord;
	};
} // namespace Lime
