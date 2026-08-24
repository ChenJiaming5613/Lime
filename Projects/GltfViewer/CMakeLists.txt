# GltfViewer. Deliberately has no C++ and no shaders of its own.
#
# Everything it needs comes from the engine: main, the built-in Blinn-Phong forward pass and its shader.
# This project exists to prove that a project needs no code at all, so adding any here would defeat its
# purpose. The scene it renders is named in ProjectSettings.json.

lime_add_project()
