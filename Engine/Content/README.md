# Built-in engine content

Assets shipped with the engine itself, resolved relative to the executable at runtime
(`Content/` next to the binary) with a fallback to this source directory during development.

## Layout

| Directory | Contents |
| --- | --- |
| `Textures/` | Built-in textures such as fallback albedo, normal and checkerboard maps |

## Conventions

- PascalCase file names, e.g. `DefaultWhite.png`, `CheckerBoard.png`
- Keep assets small; anything large belongs to a project, not the engine
- Trivial fallbacks (white, black, flat normal) are generated procedurally in code instead of
  being stored here
