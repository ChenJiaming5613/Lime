# Render graph files

A render graph describes which render passes run and what each one reads from the others. This is what
drives the engine's rendering: the graph named by `scene.renderGraph` in ProjectSettings.json is loaded at
startup, compiled, and executed every frame. The Render Graph panel in the editor loads these, draws them
and writes them back.

## Format

```json
{
  "name": "Default",
  "passes": [
    { "name": "ShadowCaster", "type": "ShadowCaster" },
    { "name": "ForwardLit", "type": "BlinnPhongForwardLit" },
    { "name": "PostProcess", "type": "PostProcess" }
  ],
  "edges": [
    { "from": "ShadowCaster.shadowDepth", "to": "ForwardLit.shadowDepth" },
    { "from": "ForwardLit.color", "to": "PostProcess.sceneColor" }
  ],
  "graphOutputs": [
    "PostProcess.color"
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Shown in the panel. Lets a project keep several graphs apart. |
| `passes` | The pass instances. `name` is unique within the graph and is how edges refer to it; `type` selects which pass, and with it the inputs and outputs that pass declares. |
| `edges` | Dependencies. See below. |
| `graphOutputs` | Which outputs are the result of the whole graph. At least one is required — a graph with none produces nothing. The first is what the viewport shows. |

## Pass types come from the engine

`type` names a registered render pass, and the inputs and outputs it offers are whatever that pass
declares in code. Nothing in this file can add a field a pass does not have.

The passes the engine provides:

| Type | Inputs | Outputs |
| --- | --- | --- |
| `ShadowCaster` | — | `shadowDepth` (D32, 2048×2048) |
| `BlinnPhongForwardLit` | `shadowDepth` (optional) | `color`, `depth` (D32) |
| `PostProcess` | `sceneColor` | `color` |

An optional input may be left unconnected and the pass degrades: `BlinnPhongForwardLit` without a shadow
map simply draws everything lit.

## Resources are pins, not entities

There is no `resources` array. A resource **is** an output of some pass, and its identity is
`PassName.resourceName`. A texture read by three passes is one output with three edges leaving it.

Sizes and formats are mostly decided by the graph rather than written here. A pass that does not pin them
gets the viewport's size and a colour format; one that does — `ShadowCaster` is square and fixed, depth is
always D32 — keeps what it asked for. Connecting two ends that pin *different* sizes or formats is an
error, because there is no single texture that satisfies both.

## Edges

An edge runs from an output to an input, and both endpoints name a resource as `Pass.resource`:

```json
{ "from": "ForwardLit.color", "to": "PostProcess.sceneColor" }
```

Both parts are required. An endpoint that names only a pass is rejected with a warning, and the edge is
skipped.

An input takes one edge: two passes writing into the same input has no defined meaning here, so the
second is rejected rather than silently replacing the first.

## What compiling checks

Loading only proves the file parsed. Compiling is what decides whether the graph can run:

- **Cycles** — a graph where a pass depends on itself, directly or through others, has no valid order.
- **Unsatisfied inputs** — a required input with no edge into it.
- **Conflicting specifications** — the two ends of an edge pinning different formats or sizes.
- **No graph output** — the graph would produce nothing.
- **Unreachable passes** — a pass that cannot reach any graph output is dropped rather than run, since
  nothing consumes what it writes. This is a note, not an error.

If compiling fails, the engine renders **only the editor UI**: the viewport stays black and every reason
is written to the log and listed in the panel. That is deliberate — a black viewport with an explanation
beats a half-drawn frame with none.

## Changes need a restart

The panel edits and saves files. It does not rebuild the running graph, so a saved change takes effect the
next time the engine starts. Recompiling live would mean reallocating every texture and rebuilding every
pipeline mid-frame, which is not worth the complexity yet.

## No layout

These files contain no positions, no zoom and no window state. The panel computes the layout with a
layered (Sugiyama) algorithm every time it loads a graph.

That is deliberate: a stored layout and a computed one would disagree the moment either changed, and
there would be no way to tell which was right. It also means the layout is a function of the graph alone,
so the same file always looks the same — reordering `passes` in the file does not move anything, because
ties are broken by name rather than by position in the array.

## Editing by hand

These files are meant to be readable and writable without the editor. Identity is by name throughout, so
there are no numeric ids to keep in sync.

A file with one bad element still opens: an edge naming a pass that does not exist, or a resource the
pass type does not declare, is dropped and reported in the panel's issue list. Only a document that is
not valid JSON, or one without a `passes` array, fails to load outright.

These files contain no positions, no zoom and no window state. The panel computes the layout with a
layered (Sugiyama) algorithm every time it loads a graph.

That is deliberate: a stored layout and a computed one would disagree the moment either changed, and
there would be no way to tell which was right. It also means the layout is a function of the graph alone,
so the same file always looks the same — reordering `passes` in the file does not move anything, because
ties are broken by name rather than by position in the array.

## Editing by hand

These files are meant to be readable and writable without the editor. Identity is by name throughout, so
there are no numeric ids to keep in sync.

A file with one bad element still opens: an edge naming a pass that does not exist, or a resource the
pass type does not declare, is dropped and reported in the panel's issue list. Only a document that is
not valid JSON, or one without a `passes` array, fails to load outright.
