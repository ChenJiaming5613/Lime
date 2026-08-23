// Turns imported glTF data into scene entities.
//
// A free function rather than a class: it holds no state between calls, and the result is entirely
// determined by the input, which makes it straightforward to test.

#pragma once

#include "Asset/AssetTypes.h"

namespace Lime
{
	class FScene;

	struct FSceneBuildResult
	{
		uint32 EntityCount = 0;
		uint32 MeshEntityCount = 0;
		// True when the glTF carried no light of its own and a default one was added.
		bool bAddedDefaultLight = false;
	};

	// Replaces the scene's contents with the imported data.
	//
	// The node array is flat with children referenced by index; this rebuilds that as parented entities.
	// Malformed input has already been rejected by the importer, so indices are trusted to be in range,
	// but a node reachable from two parents is still handled: the second attempt is refused by
	// FScene::SetParent rather than corrupting the tree.
	FSceneBuildResult BuildScene(FScene& Scene, const FGltfSceneData& Data);
} // namespace Lime
