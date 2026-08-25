# Frame graph files

A frame graph describes which render passes run and what each one reads from the others. The Frame Graph
panel in the editor loads these, draws them and writes them back.

## Format

```json
{
  "name": "Deferred Example",
  "passes": [
    { "name": "ForwardLit", "type": "ForwardLit" }
  ],
  "edges": [
    { "from": "ShadowCaster.depth", "to": "ForwardLit.shadowDepth" },
    { "from": "DepthPrepass", "to": "ShadowCaster", "kind": "execution" }
  ],
  "graphOutputs": [
    "ToneMap.ldr"
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Shown in the panel. Lets a project keep several graphs apart. |
| `passes` | The pass instances. `name` is unique within the graph and is how edges refer to it; `type` selects which pass, and with it the inputs and outputs that pass declares. |
| `edges` | Dependencies. See below. |
| `graphOutputs` | Which outputs are the result of the whole graph. At least one is required — a graph with none produces nothing. |

## Resources are pins, not entities

There is no `resources` array. A resource **is** an output of some pass, and its identity is
`PassName.resourceName`. A texture read by three passes is one output with three edges leaving it.

Which resources a pass has comes from its type, not from the file. `ForwardLit` declares `shadowDepth`
and `sceneDepth` as inputs and `color` as an output, so those are the only names an edge may use on a
`ForwardLit` instance.

## Two kinds of edge

**Data edges** run from an output to an input, and both endpoints name a resource:

```json
{ "from": "ForwardLit.color", "to": "Bloom.input" }
```

**Execution edges** only constrain order. Both endpoints name a pass and nothing else:

```json
{ "from": "DepthPrepass", "to": "ShadowCaster", "kind": "execution" }
```

Use one when a pass has to run before another without reading anything it produces. `kind` may be
omitted on data edges; it is inferred from the endpoints naming resources.

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
