# R4nkine design

R4nkine is a 3D rendering library in the spirit of IRIS Performer: a scene
graph made for speed, loaders for many asset formats, and a viewer to show
them. It is written in C++17 for today's GPUs.

The name comes from the Rankine scale, the absolute version of the
Fahrenheit scale. Fahrenheit was the scene graph that SGI, Microsoft and HP
announced in 1997 and never shipped. R4nkine is a homage to the old OpenGL
demos.

## Goals

1. **Fast**: cull, sort by state, batch, instance. The CPU work per object
   is small and the work per triangle is zero.
2. **Any asset**: every loader produces one neutral format
   (`r4::assets::Model`). A game can replace an old asset with a modern one
   without its code changing.
3. **A living world**: nodes can be added and removed at run time (forests,
   villages, cities on a terrain), and the bounds stay right without manual
   updates.
4. **Several backends**: nothing above `r4::gpu` calls a graphics API.
5. **Embeddable**: it can live inside a game that still draws part of its
   frame in fixed-pipeline OpenGL (libRealSpace).

## Layers

```
apps      r4fly (the viewer, heir of perfly), demos
render    cull → draw list → sort → batch/instance → passes; materials
scene     Node, Group, Geode, Switch, LOD (+ Instancer, Billboard to come)
terrain   (to come) blocks, LOD, seams, placement rules
assets    Model, Mesh, Material, Image; loaders: OBJ/MTL, SGI, PPM
          (to come: glTF 2.0, OpenFlight, pfb, dwb, RealSpace IFF, PNG...)
gpu       Device: buffers, textures, shaders, pipelines, draws
          backends: OpenGL 3.3 (done), SDL3 GPU / Vulkan (to come)
core      math: Vec, Mat4 (column-major), Box, Sphere, Frustum
```

## Conventions

- **Space**: Z-up, as in Performer and flight simulators. X is right, Y is
  forward, Z is up, and units are meters. The loaders for Y-up formats (OBJ,
  glTF) rotate their root node.
- **Matrices**: column-major, `p' = M * p`, with OpenGL clip space for the
  GL backend.
- **Images**: rows start at the bottom (OpenGL order), 8 bits per channel.
- **Ownership**: scene nodes are `shared_ptr`. GPU meshes and materials are
  `shared_ptr` that free their buffers when the last user goes away.

## The GPU interface (`r4::gpu::Device`)

It follows the model of the explicit APIs (Vulkan, Metal, D3D12), so that
every backend can implement it:

- **Resources are opaque handles**: `BufferHandle`, `TextureHandle`,
  `ShaderHandle`, `PipelineHandle`.
- **States are baked into pipelines** made in advance: the vertex layout,
  blending, depth, culling, polygon mode and offset.
- **Uniforms live in uniform buffers** bound by slot and range. A frame's
  object matrices go into one buffer, and each draw binds its range.
- **Shaders**: the GL backend takes GLSL 330. Uniform blocks and samplers
  are bound to slots by name in `ShaderDesc`, because GLSL 330 has no
  `binding` qualifier. The other backends will take the same sources
  compiled to SPIR-V and cross-compiled.
- **Host interop**: `beginHostInterop()` and `endHostInterop()` save and
  restore the GL state of a host that draws with the same context. This is
  how R4nkine can draw inside libRealSpace, between its fixed-pipeline
  passes.

### The OpenGL 3.3 backend

- **Function loading**: the functions are loaded at run time through the
  window system's loader (`SDL_GL_GetProcAddress`...). The library needs no
  GL header and no loader library.
- **One VAO per pipeline**: without separate vertex formats (GL 4.3),
  attributes are pointed again only when the buffers bound to a VAO change.
- **State cache**: every state is cached, so redundant calls are skipped.
- **Profiles**: it works in a core context and in a compatibility context.

## Rendering a frame

1. **Cull**: the graph is traversed with the world matrices. Each node's
   bounding sphere is tested against the frustum. A node fully inside is not
   tested further, and a node outside is skipped with its subtree. A Switch
   visits its active children; a LOD visits the child for the eye distance.
   Nodes whose mask does not match the camera mask are skipped.
2. **Draw list**: each visible drawable becomes an item holding its mesh,
   material, world matrix, depth and pipeline.
3. **Sort**: opaque items are sorted by pipeline, then material, then mesh,
   to keep state changes few. Transparent items are sorted from back to
   front.
4. **Upload**: the object blocks (model, model-view and normal matrices)
   go into one uniform buffer, uploaded once per frame.
5. **Draw**: the device skips bindings that did not change.

**Bounds**: they are lazy. A change to a node (matrix, children,
drawables) marks it and its ancestors dirty, and the next `bounds()` call
recomputes only what was marked. The world can therefore change at run
time, nodes added or removed, without full updates.

## Materials (lighting models)

- `Unlit`: the color as is.
- `Phong`: per-pixel Blinn-Phong with one directional light, ambient light,
  emission and linear fog. Faces are lit on both sides.
- `PaletteRamp` (to come): lighting along the ramps of a fixed 256-color
  palette, through a lookup texture (colour index × intensity). This is the
  look of Strike Commander and of the other 8-bit games.

## Roadmap

| Step | Content | State |
| --- | --- | --- |
| 1 | the foundation: math, `r4::gpu` and the GL 3.3 backend, Model, OBJ/MTL, SGI and PPM loaders, scene graph with culling and LOD/Switch, Phong renderer, r4fly, tests | **done** |
| 2 | instancing in the renderer (equal mesh and material in one draw), `Instancer` node for thousands of copies (forests), Billboard, PNG/JPEG via stb_image, glTF 2.0 | |
| 3 | the old formats: OpenFlight, pfb, dwb, 5551E (ported from legacy_opengl_demo); all the demos in r4fly | |
| 4 | RealSpace: IFF/RSEntity loader, `PaletteRamp` material, block terrain with LOD and seams, placement rules (forests, villages) | |
| 5 | libRealSpace integration: a renderer derived from `SCRenderer` that delegates to R4nkine, with host interop | |
| 6 | a second backend (SDL3 GPU: Vulkan, Metal, D3D12), shaders compiled to SPIR-V | |
| later | threaded culling (APP/CULL/DRAW as in Performer), shadows, an ImGui panel in r4fly | |

## Integration in libRealSpace

`SCRenderer` already has virtual methods and a `setInstance()` hook, so a
`R4Renderer : public SCRenderer` can replace it without the game changing:

- **What it overrides**: only the high-level entry points
  (`renderWorldSolid`, the `drawModel` overloads, `drawModelWithChilds`,
  `renderMissionObjects` and `renderObjects`). The immediate-mode helpers
  are not used any more.
- **GL context**: the game keeps its compatibility context. R4nkine draws
  between `beginHostInterop()` and `endHostInterop()`, so the cockpit, HUD
  and 2D screens still drawn in fixed-pipeline GL keep working.
- **Draw order**: first, `drawModel` draws immediately with the precompiled
  meshes, which keeps the game's order. A queued draw list with sorting and
  instancing comes after, flushed at sync points such as before the cockpit.
- **Objects added to the map**: they live in R4nkine's graph (forests,
  villages) and are drawn with the terrain. The game can query them for
  collisions through an intersection call.
