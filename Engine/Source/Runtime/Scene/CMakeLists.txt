# LimeScene: the entity component scene, its GPU resources and the passes that draw it.
#
# Depends on LimeAsset for the imported data and on LimeRenderer for the device and pass interfaces. The
# scene pass lives here rather than in LimeRenderer, which would otherwise have to depend on this module
# and close a cycle.

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
		LimeCamera
		LimeRenderer
		entt
	FOLDER "Engine/Runtime"
)
