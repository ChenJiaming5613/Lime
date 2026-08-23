# LimeScene: the entity component scene and its GPU resources.
#
# Depends on LimeAsset for the imported data and on LimeRHI for the device types it uploads through, but
# deliberately not on LimeRenderer: the passes that draw a scene live in LimeRenderer, so a dependency in
# this direction would close a cycle.

lime_add_module(
	NAME LimeScene
	SOURCES
		Components.h
		Scene.h
		Scene.cpp
		SceneBuilder.h
		SceneBuilder.cpp
		SceneGpuResources.h
		SceneGpuResources.cpp
	PUBLIC_DEPS
		LimeAsset
		LimeRHI
		entt
	FOLDER "Engine/Runtime"
)
