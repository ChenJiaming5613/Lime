# LimeAsset: asset import producing plain CPU data.
#
# Deliberately has no GPU dependency, which is what lets the glTF importer be covered by unit tests on
# a machine without a graphics device. tinygltf is PRIVATE so its headers, and the stb implementation
# they carry, do not leak into every consumer of this module.

lime_add_module(
	NAME LimeAsset
	SOURCES
		AssetTypes.h
		AssetTypes.cpp
		AsyncGltfLoader.h
		AsyncGltfLoader.cpp
		DdsLoader.h
		DdsLoader.cpp
		GltfImporter.h
		GltfImporter.cpp
	PUBLIC_DEPS
		LimeCore
	PRIVATE_DEPS
		tinygltf
	FOLDER "Engine/Runtime"
)
