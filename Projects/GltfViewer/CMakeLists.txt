# GltfViewer. Deliberately has no C++ and no shaders of its own.
#
# Everything it needs comes from the engine: main, the built-in glTF scene pass and its shader. This
# project exists to prove that a new project needs nothing but ProjectSettings.json, so adding code
# here would defeat its purpose.

lime_add_project()
