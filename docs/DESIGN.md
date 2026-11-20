# R4nkine design

R4nkine is a 3D rendering library. Its model is IRIS Performer. It has these parts:

- a scene graph for high speed,
- loaders for many asset formats,
- a viewer that shows the assets.

R4nkine is in C++17 for the GPUs of today.

The name comes from the Rankine scale. The Rankine scale is the absolute version of the Fahrenheit scale. Fahrenheit was a scene graph of SGI, Microsoft and HP. They announced it in 1997, but they did not release it. R4nkine is a homage to the old OpenGL demos.

## Goals

1. **Speed**: the renderer culls, sorts by state, batches and instances. The CPU work for each object is small. There is no CPU work for each triangle.
2. **All assets**: each loader makes one neutral format (`r4::assets::Model`). Thus, a game can replace an old asset with a modern asset. The code of the game does not change.
3. **A world that changes**: you can add and remove nodes at run time (forests, villages, cities on a terrain). The bounds stay correct without manual updates.
4. **Different backends**: no code above `r4::gpu` calls a graphics API.
5. **Embedded use**: R4nkine can operate in a game that draws a part of its frame with fixed-pipeline OpenGL (libRealSpace).

## Layers

```
apps      r4fly (the viewer, after perfly), demos
ui        the panels of Performer: buttons, switches, sliders, menus, trees
raster    2D on the CPU: indexed or true-color surfaces, primitives, fonts
render    cull → shadow pass → draw list → sort → batch/instance → passes;
          materials; fog; overlays (raster surfaces), post process (MSAA,
          FXAA, CRT, palette)
scene     Node, Group, Geode, Switch, LOD, Billboard, Instancer
assets    Model, Mesh, Material, Image; loaders: OBJ/MTL, libgobj .d,
          YAODL, flip .bin, pfb, dwb, OpenFlight, gset, glTF 2.0; SGI,
          PPM, 5551E, PNG/JPEG/BMP/TGA/GIF; pixel art scalers (2xSaI, xBR)
gpu       Device: buffers, textures, shaders, pipelines, draws
          backends: OpenGL 3.3 (done), SDL3 GPU / Vulkan (not done)
core      math: glm (Vec3, Mat4, column-major), Box, Sphere, Frustum
```

The RealSpace data (Strike Commander, Tactical Operations) is not in these layers. libRealSpace reads it. `contrib/librealspace` converts it into R4nkine models (r4bridge) and shows it (r4objviewer). Refer to "RealSpace (`contrib/librealspace`)".

## Dependencies

The dependencies come from vcpkg in manifest mode. The manifest is `vcpkg.json`. The baseline is in `vcpkg-configuration.json`. It is the same baseline as libRealSpace.

- **glm**: vectors and matrices. `r4::Vec3` and `r4::Mat4` are `glm::vec3` and `glm::mat4`. Thus, a program that uses glm gives its values without conversion. R4nkine needs `GLM_FORCE_CTOR_INIT`: vectors are zero and matrices are identity by default. The target defines it for its users.
- **stb**: the usual image formats.
- **cgltf**: glTF 2.0.
- **SDL2**: only for r4fly (the `viewer` feature).

The GUI (`r4::ui`) is a part of the library. It draws through `r4::gpu`. `tools/bake_font.cpp` makes its font in `src/ui/font_data.inc`.

The library does not need a graphics header or a GL loader. It loads the OpenGL functions at run time.

## Conventions

- **Space**: the axes are right-handed and Z is up. Performer, OpenFlight, Vega, OpenSceneGraph, VulkanSceneGraph and the flight simulators use the same axes.
  - X is right, Y is forward, Z is up. The units are meters.
  - `r4/axes.h` gives the names of the axes: `axes::kRight`, `kForward`, `kUp`, `headingOf`, `pitchOf`, `heightOf`.
  - The code uses these names. It does not write `{0, 0, 1}`. Thus, the code that uses the convention is the code that includes this header.
- **One convention inside, one conversion at each border**:
  - These sources are Y-up: OBJ, glTF, the gobj and YAODL files of the SGI demos, a game with Y-up code (libRealSpace), the tracking space of OpenXR.
  - `axes::fromYUpMatrix()` turns a Y-up source one time, at the root of its data. `transformFromYUp` turns one transform.
  - No other code makes a conversion.
  - The two conventions are right-handed. Thus, the turn keeps the winding of the faces. It has no cost at run time (one matrix, or one time for each vertex when the asset loads).
- **Matrices**: column-major, `p' = M * p`. The GL backend uses the OpenGL clip space.
- **Images**: the rows start at the bottom (OpenGL order). Each channel has 8 bits.
- **Ownership**: the scene nodes are `shared_ptr`. The GPU meshes and materials are `shared_ptr`. They release their buffers when the last user releases them.

## GPU interface (`r4::gpu::Device`)

The interface uses the model of the explicit APIs (Vulkan, Metal, D3D12). Thus, all the backends can implement it.

- **Resources**: they are opaque handles: `BufferHandle`, `TextureHandle`, `ShaderHandle`, `PipelineHandle`.
- **States**: the pipelines contain the states. The program makes the pipelines before it uses them. A pipeline contains the vertex layout, the blending, the depth, the culling, the polygon mode and the polygon offset.
- **Uniforms**: they are in uniform buffers. The device binds a uniform buffer by slot and by range. The object matrices of a frame go into one buffer. Each draw binds its range.
- **Shaders**: the GL backend uses GLSL 330. GLSL 330 has no `binding` qualifier. Thus, `ShaderDesc` binds the uniform blocks and the samplers to slots by name. The other backends will use the same sources, compiled to SPIR-V and then cross-compiled.
- **Host interop**: `beginHostInterop()` and `endHostInterop()` save and restore the GL state of a host that uses the same context. With these functions, R4nkine draws in libRealSpace between its fixed-pipeline passes.

### OpenGL 3.3 backend

- **Function loading**: the backend loads the functions at run time through the loader of the window system (`SDL_GL_GetProcAddress`). The library does not need a GL header or a loader library.
- **One VAO for each pipeline**: OpenGL 3.3 has no separate vertex formats (they come with OpenGL 4.3). Thus, the backend sets the attributes again only when the buffers of a VAO change.
- **State cache**: the backend keeps all the states in a cache. Thus, it does not send the same state two times.
- **Profiles**: the backend operates in a core context and in a compatibility context. Above OpenGL 2.1, macOS has only core contexts (up to 4.1). Thus, r4fly requests a 4.1 core context on macOS.

## Frame sequence

1. **Cull**: the renderer goes through the graph with the world matrices.
   - It compares the bounding sphere of each node with the frustum.
   - When a node is fully inside, the renderer does not test its children.
   - When a node is outside, the renderer ignores the node and its subtree.
   - A Switch goes into its active children. A LOD goes into the child for the distance to the eye.
   - The renderer ignores a node when its mask has no bit of the camera mask.
2. **Shadow pass** (when `RenderOptions::shadows` is on):
   - The renderer makes one shadow map: an orthographic view from the light. It covers the view up to `shadowDistance`, and the casters up to `shadowCasterReach` in the direction of the light.
   - The view moves by complete texels of the map. Thus, the shadows do not shimmer when the camera moves.
   - The renderer culls the graph again from the light, with the camera mask and `shadowMask`.
   - The casters are the opaque items of the materials with `castShadows`.
   - The receivers compare their depth with a 3 x 3 PCF filter. A normal offset and a bias prevent shadow acne. The shadow fades at the end of `shadowDistance`.
   - The lit materials lose the direct light in the shadow. The unlit materials lose `shadowStrength` of their color.
3. **Draw list**: each visible drawable becomes an item. The item contains the mesh, the material, the world matrix, the depth and the pipeline.
4. **Sort**: the renderer sorts the opaque items by pipeline, then by material, then by mesh. This decreases the number of state changes. It sorts the transparent items from back to front.
5. **Upload**: the model matrices of all the items go into one vertex buffer. The renderer uploads this buffer one time for each frame. The shader reads the matrices as a per-instance stream (locations 4 to 7). The shader calculates the normal matrix.
6. **Draw**: consecutive items with the same mesh, material and pipeline become one instanced draw. The draw starts at their offset in the matrix buffer. The device does not bind again a binding that did not change.

**Billboards**: a billboard turns about the origin of its node. Its -Y axis points to the eye. An axis billboard turns about Z only. A point billboard turns about Z and X. Its bounds are around its origin. Thus, the bounds stay correct for all the turns.

**Instancers**: an instancer has levels and a list of instances. Each level has parts and a maximum distance. Each part is a drawable at a matrix. The renderer culls each instance on its sphere and selects the level for its distance. After the sort, all the visible instances of a part become one draw call.

An instance is a matrix or a `Placement`. A `Placement` is a position, a heading, a scale and a vertical scale: 24 bytes instead of 64. The forests use placements. `Instancer::setUp` sets the up axis of the placements.

**Fog**: `Environment` has a linear fog (`fogStart`, `fogEnd`, `fogColor`). It applies to all the materials. The application can set the far plane just after `fogEnd`. Thus, the depth buffer keeps its precision.

**Time**: `Environment::time` gives the time in seconds. The application sets it. The animated textures use it.

**Bounds**: the renderer calculates the bounds only when it needs them. A change to a node (matrix, children, drawables) sets a flag on the node and on its ancestors. The next call to `bounds()` calculates only the nodes with the flag. Thus, you can add or remove nodes at run time without full updates.

## GUI (`r4::ui`)

The GUI looks like the GUI of IRIS Performer (libpfutil), as the perfly port in legacy_opengl_demo shows it:

- the same colors and reliefs,
- the yellow lights of the switches,
- the blue bars of the sliders,
- the lists of the menu buttons.

The model is immediate. Each frame, the program calls the widgets with its own variables. The widgets tell the changes.

The CPU builds a frame into one vertex buffer. The buffer contains rectangles, triangles and glyphs. They all use one texture: the font atlas and a white texel. The GPU draws the frame with one pipeline and a small number of draws. The scissor clips the draws to the panels. When the widgets of a panel are taller than the panel, the wheel scrolls the panel.

## Texture quality

- **Scalers**: `r4/assets/upscale.h` enlarges an image one time when it loads: 2xSaI, xBR 2x, xBR 4x. The alpha counts as a color. Thus, the clear holes keep sharp edges.
- **Mipmaps**: `Image::mipmaps` selects them for each image.
- **Anisotropic filter**: `RenderOptions::anisotropy` applies to the textures that load after the change. `Limits::maxAnisotropy` gives the maximum of the GPU.

## 2D layers and post process

`r4::raster` draws on the CPU into surfaces of all sizes. A surface is indexed (one byte for each pixel and a palette, as VGA) or true color. `r4::raster` does not use the GPU. Thus, a game can continue to draw its cockpit pixel by pixel.

`render::Overlay` shows a surface:

- The indices go to the GPU as an R8 texture.
- The palette goes to the GPU as a 256 x 1 texture.
- The shader finds the colors in the palette. The transparent index is clear.
- Thus, a palette effect costs one upload of 1 KB.
- The overlay sends a surface again only when its version changes.
- `Overlay::texture()` draws the surface into a render target, for a material.

The device has **render targets**. A render target has a color texture, a depth buffer and multisampled buffers. A blit resolves the multisampled buffers into the texture. The device also has **texture updates**.

`PostProcess` gives the render target for the frame. Then it applies the effects in one pass (two passes with FXAA). For the reduction to a palette, the CPU makes a table of 32 x 32 x 32 colors. Each entry is the nearest color of the palette, after an ordered dither.

## Render options and measurements

`Renderer::options()` sets the stages on and off at run time. Thus, you can see the effect of each stage:

- frustum culling,
- sort by state,
- merge of equal items into instanced draws,
- textures,
- lighting,
- flat shading and Gouraud shading of the palette ramps,
- a scale on the distances of the LODs and the instancers,
- shadows and their parameters,
- anisotropic filter.

**Freeze culling** keeps the frustum and the eye of that moment. Then you can move the camera out and look at the culled nodes. `stats()` gives the counters of the frame and the CPU time of each stage (cull, sort, upload, draw).

## Old formats

Each loader is a port of the loader in legacy_opengl_demo. The loaders keep the original behavior and translate it into the neutral format.

- **State bits (libgobj)**: the statebits of the flight objects become node masks. The `defaultState` of the model is the state of the original `drawobj` call (body, landing gear and weapons). The state filter of the root of the model applies it.
- **Modes**: these modes are material flags: culled back faces, transparency, no depth writes (flames). Subfaces (OpenFlight) and layers (pfb, dwb) are `decal` materials. The renderer draws them with a polygon offset.
- **YAODL execution**: the loader executes the YAODL objects as the original does. It pushes and pops the attributes for each element. It records the draw calls into meshes. It applies the matrices to the vertices.
- **YAODL extras**: the loader tessellates the NURBS surfaces. It ignores their trimming curves. The shader makes the sphere maps. The CPU makes the linear texture coordinates.
- **Switches and LODs**: a sequence shows its first frame. A LOD keeps its ranges. A switch keeps its value.
- **Billboards**: the billboard faces (OpenFlight templates 2 and 4) and the pfb billboards become Billboard nodes at their centers. These nodes turn to the eye.
- **Encrypted data**: the loaders decrypt the coordinates of the version 2 pfb files and the textures of the F-18 demo. Some copies of the F-18 textures have the encryption mark, but they are not encrypted. The loader keeps the smoother image.
- **Orientation**: the root node of a Y-up format (OBJ, libgobj, YAODL, flip, Y-up dwb) turns to Z-up.

## Materials (lighting models)

- `Unlit`: the color without change.
- `Phong`: per-pixel Blinn-Phong with one directional light, ambient light, emission and linear fog. On a two-sided face, the normal turns to the eye, whatever the winding (two-sided lighting).
- **Common options** (all models): vertex colors (lmcolor or color material), diffuse texture, alpha masks and blending, decals, sphere maps, no depth writes.
- `PaletteRamp`: the lighting of the 8-bit games (Strike Commander and others).
  - The intensity is `rampAmbient + rampLight × max(N·L, 0)`.
  - The red of the vertex colors gives the palette index of the face. The index and the intensity select the shade in a table of 256 colors × 32 intensities. This table is the image of the material. The loader makes it from the ramps of the palette.
  - With `rampTextured`, the shader multiplies a texture by the intensity.
  - With `indexImage`, the material has an image of palette indices. The shader shades each texel along its ramp. Then it filters the texels (bilinear filter in the shader). With `vertexColors`, the red of the vertex colors gives the intensity. Without `vertexColors`, the light gives the intensity.
  - With `indexAtlasColumns` and `indexAtlasRows`, `indexImage` is an atlas of equal cells. The green and the blue of the vertex colors give the cell. The alpha gives the log2 of the width and the height of the image in the cell. The filter stays in this image.
- **Shadow mask**: the shadow pass culls with the camera mask and `RenderOptions::shadowMask`. When the mask of a node has no bit of this mask, the node and its children cast no shadow. The shadow pass does not go into this node.
- **Animated textures**: when `frames` is more than 1, `diffuseImage` is a strip of frames from top to bottom. The shader shows the frames one after the other at `framesPerSecond`. It uses the time of the environment (`Environment::time`). The application sets this time.
- **No textures**: this applies when the textures option is off, and always in wireframe.
  - The mean color of the opaque texels replaces each texture. Thus, the lines keep the color of their faces.
  - The renderer does not draw the layers on a face at the same depth (`depthEqual` decals, `indexImage`).

## Roadmap

| Step | Content | State |
| --- | --- | --- |
| 1 | the base: math, `r4::gpu` and the GL 3.3 backend, Model, OBJ/MTL, SGI and PPM loaders, scene graph with culling and LOD/Switch, Phong renderer, r4fly, tests | **done** |
| 2 | instancing in the renderer (one draw for equal mesh and material), `Instancer` node for thousands of copies (forests), Billboard, PNG/JPEG with stb_image, glTF 2.0 | **done** |
| 3 | the old formats, ports from legacy_opengl_demo: libgobj (flight), YAODL (powerflip, flip), flip .bin, pfb, dwb, OpenFlight, gset; 5551E images; all the demos in r4fly | **done** |
| 3b | `r4::ui`, the GUI of perfly drawn through `r4::gpu`; r4fly with its panels: settings, scene graph, performance; render options in the renderer (culling, freeze, sort, instancing, textures, lighting, LOD scale) and CPU times | **done** |
| 4 | RealSpace in `contrib/librealspace`: RSEntity to Model, `PaletteRamp` material, block terrain with LOD and seams, quadtree of the blocks, texture atlas, outside rings, forests, cities, r4objviewer | **done** |
| 4b | renderer: shadow map, fog, texture scalers, anisotropic filter, mipmaps, palette index textures, animated textures, shadow mask, compact placements | **done** |
| 5 | libRealSpace integration: a renderer from `SCRenderer` that sends the work to R4nkine, with host interop | not done |
| 6 | a second backend (SDL3 GPU: Vulkan, Metal, D3D12), shaders compiled to SPIR-V | not done |
| later | culling in threads (APP/CULL/DRAW as in Performer); occlusion culling (horizon, occlusion queries or Hi-Z) | not done |
| game | ISECT queries (ground height, collisions), APP traversal with node callbacks, trigger volumes connected to the message bus of libRealSpace, views that do not draw, for a server: see [GAME_INTEGRATION.md](GAME_INTEGRATION.md) | not done |

## RealSpace (`contrib/librealspace`)

libRealSpace reads the data of the game. The code in `contrib/librealspace` goes into the source tree of libRealSpace. It does not change the code of libRealSpace.

**r4bridge** converts the data of libRealSpace into R4nkine models:

- `toModel`: an `RSEntity` becomes a model, as `SCRenderer::drawModel` draws it. The LODs become a LOD node. The faces have a color pass, a texture pass and a transparent pass. Animated textures keep all their frames.
- `terrainModel`: the terrain of an `RSArea`.
  - Each block is a LOD node over the three levels of `RSArea`, with its seams.
  - A quadtree groups the blocks. Far groups show one merged mesh of their LVL2 blocks.
  - One atlas of palette indices contains all the textures of the terrain. The textures use the palette ramps, as the vertices do.
  - Outside the map, a collar and two rings of tiles continue the border to the edge of the world (4 times the map).
- `overlayModel`: the runways.
- `forestChunks`, `plantForest`, `treeModel`: the forests, from the green terrain or from the woods painted in the textures.
- `cityModel`: the buildings on the textures of cities, harbors, villages and farms.
- `groundHeight`: the height of the ground on the LVL0 triangles as `terrainModel` draws them. `RSArea::getY` gives incorrect heights.
- `onGround`: it puts an object on the ground. It only raises a flying object.

**r4objviewer** shows the objects, the missions and the worlds of Strike Commander and Tactical Operations. It has the GUI of r4fly. Its options are in `contrib/librealspace/README_R4NKINE.md`.

## Integration in libRealSpace (plan, step 5)

`SCRenderer` has virtual methods and a `setInstance()` hook. Thus, a `R4Renderer : public SCRenderer` can replace it. The game does not change.

- **Overridden functions**: only the high-level entry points: `renderWorldSolid`, the `drawModel` overloads, `drawModelWithChilds`, `renderMissionObjects` and `renderObjects`. The immediate-mode helpers are not necessary after this change.
- **GL context**: the game keeps its compatibility context. R4nkine draws between `beginHostInterop()` and `endHostInterop()`. Thus, the cockpit, the HUD and the 2D screens continue to draw with fixed-pipeline GL.
- **Draw order**: first, `drawModel` draws immediately with the precompiled meshes. Thus, the order of the game stays the same. After this step, a draw list with sort and instancing comes. The renderer sends this list at synchronization points, for example before the cockpit.
- **Objects added to the map**: they are in the graph of R4nkine (forests, villages). The renderer draws them with the terrain. The game can do collision queries on them with an intersection call.
