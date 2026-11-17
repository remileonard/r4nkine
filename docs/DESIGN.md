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
ui        the panels of Performer: buttons, switches, sliders, menus, trees
render    cull → draw list → sort → batch/instance → passes; materials
scene     Node, Group, Geode, Switch, LOD, Billboard, Instancer
terrain   (to come) blocks, LOD, seams, placement rules
assets    Model, Mesh, Material, Image; loaders: OBJ/MTL, libgobj .d,
          YAODL, flip .bin, pfb, dwb, OpenFlight, gset, glTF 2.0; SGI,
          PPM, 5551E, PNG/JPEG/BMP/TGA/GIF (to come: RealSpace IFF)
gpu       Device: buffers, textures, shaders, pipelines, draws
          backends: OpenGL 3.3 (done), SDL3 GPU / Vulkan (to come)
core      math: glm (Vec3, Mat4, column-major), Box, Sphere, Frustum
```

## Dependencies

The dependencies come from vcpkg in manifest mode (`vcpkg.json`, with the
baseline in `vcpkg-configuration.json`, the same as libRealSpace):

- **glm**: the vectors and matrices. `r4::Vec3` and `r4::Mat4` are
  `glm::vec3` and `glm::mat4`, so a program using glm passes its values as
  they are. R4nkine needs `GLM_FORCE_CTOR_INIT` (zero vectors, identity
  matrices by default): the target defines it for its users.
- **stb**: the common image formats.
- **cgltf**: glTF 2.0.
- **SDL2**: only for r4fly, the `viewer` feature.

The GUI (`r4::ui`) is part of the library and draws through `r4::gpu`; its
font is baked into `src/ui/font_data.inc` by `tools/bake_font.cpp`.

The library itself needs no graphics header and no GL loader, because the
OpenGL functions are loaded at run time.

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
  macOS has only core contexts above OpenGL 2.1 (up to 4.1): r4fly asks for
  a 4.1 core context there.

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
4. **Upload**: the model matrices of all the items go into one vertex
   buffer, uploaded once per frame. The shader reads them as a
   per-instance stream (locations 4 to 7). The normal matrix is computed in
   the shader.
5. **Draw**: consecutive items with the same mesh, material and pipeline
   become one instanced draw, starting at their offset in the matrix
   buffer. The device skips bindings that did not change.

**Billboards** turn about the origin of their node, with -Y towards the
eye: about Z only (axis), or about Z and X (point). Their bounds are taken
around their origin, so they stay valid for every turn.

**Instancers** hold levels (parts, each a drawable at a matrix, and the
farthest distance the level is used at) and a list of instance matrices.
Each instance is culled on its own sphere and gets the level for its
distance. All the visible instances of a part become one draw call after
sorting.

**Bounds**: they are lazy. A change to a node (matrix, children,
drawables) marks it and its ancestors dirty, and the next `bounds()` call
recomputes only what was marked. The world can therefore change at run
time, nodes added or removed, without full updates.

## The GUI (`r4::ui`)

The look is that of the GUI of IRIS Performer (libpfutil), as ported for
perfly in legacy_opengl_demo: the same colors and reliefs, the yellow
lights of the switches, the blue bars of the sliders, the lists of the
menu buttons. The model is immediate: the program calls the widgets each
frame with its own variables, and they report the changes. A frame is
built on the CPU into one vertex buffer (rectangles, triangles and glyphs,
all with one texture: the font atlas and a white texel), then drawn with
one pipeline and a few draws, clipped by the scissor to the panels. The
panels scroll with the wheel when their widgets are taller.

## Render options and measures

`Renderer::options()` turns the stages on and off at run time, to see what
each one is worth: frustum culling, sorting by state, the merging of equal
items into instanced draws, textures, lighting, and a scale on the
distances of the LODs and instancers. **Freezing the culling** keeps the
frustum and the eye of the moment: the camera can then move out and look at
what was culled. `stats()` gives the counters of the frame and the CPU time
of each stage (cull, sort, upload, draw).

## The old formats

Each loader is a port of the loader of legacy_opengl_demo. The original
behaviors are kept, translated into the neutral format:

- **State bits (libgobj)**: the statebits of the flight objects become node
  masks. The model's `defaultState` is the state of the original `drawobj`
  call (body, landing gear and weapons), applied as the state filter of the
  model's root.
- **Modes**: culled back faces, transparency and no depth writes (flames)
  are material flags. Subfaces (OpenFlight) and layers (pfb, dwb) are
  `decal` materials, drawn with a polygon offset.
- **YAODL execution**: YAODL objects are executed as in the original (the
  attributes pushed and popped per element), and the drawing calls are
  recorded into meshes. The matrices are applied to the vertices.
- **YAODL extras**: NURBS surfaces are tessellated, but their trimming
  curves are ignored. Sphere maps are done in the shader, and the linear
  texture coordinate generation on the CPU.
- **Switches and LODs**: sequences show their first frame, LODs keep their
  ranges, and switches their value.
- **Billboards**: billboard faces (OpenFlight templates 2 and 4) and pfb
  billboards become Billboard nodes at their centers, turned to the eye.
- **Encrypted data**: the coordinates of version 2 pfb files and the
  textures of the F-18 demo are decrypted. Some copies of the F-18 textures
  carry the encryption mark but are already in clear; the loader keeps the
  smoother image.
- **Orientation**: the Y-up formats (OBJ, libgobj, YAODL, flip, Y-up dwb)
  turn their root node to Z-up.

## Materials (lighting models)

- `Unlit`: the color as is.
- `Phong`: per-pixel Blinn-Phong with one directional light, ambient light,
  emission and linear fog. Two-sided faces get the normal turned towards the
  eye, whatever their winding (two-sided lighting).
- **Common options** (all models): vertex colors (lmcolor / color material),
  diffuse texture, alpha masks and blending, decals, sphere maps, and no
  depth writes.
- `PaletteRamp` (to come): lighting along the ramps of a fixed 256-color
  palette, through a lookup texture (colour index × intensity). This is the
  look of Strike Commander and of the other 8-bit games.

## Roadmap

| Step | Content | State |
| --- | --- | --- |
| 1 | the foundation: math, `r4::gpu` and the GL 3.3 backend, Model, OBJ/MTL, SGI and PPM loaders, scene graph with culling and LOD/Switch, Phong renderer, r4fly, tests | **done** |
| 2 | instancing in the renderer (equal mesh and material in one draw), `Instancer` node for thousands of copies (forests), Billboard, PNG/JPEG via stb_image, glTF 2.0 | **done** |
| 3 | the old formats, ported from legacy_opengl_demo: libgobj (flight), YAODL (powerflip, flip), flip .bin, pfb, dwb, OpenFlight, gset; 5551E images; all the demos in r4fly | **done** |
| 4 | RealSpace: IFF/RSEntity loader, `PaletteRamp` material, block terrain with LOD and seams, placement rules (forests, villages) | |
| 5 | libRealSpace integration: a renderer derived from `SCRenderer` that delegates to R4nkine, with host interop | |
| 6 | a second backend (SDL3 GPU: Vulkan, Metal, D3D12), shaders compiled to SPIR-V | |
| 3b | `r4::ui`, the GUI of perfly redrawn through `r4::gpu`; r4fly with its panels: settings, scene graph, performances; render options in the renderer (culling, freeze, sorting, instancing, textures, lighting, LOD scale) and CPU timings | **done** |
| later | threaded culling (APP/CULL/DRAW as in Performer), shadows | |

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
