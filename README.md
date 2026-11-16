# R4nkine

A 3D rendering library in the spirit of IRIS Performer, for today's GPUs:
a scene graph built for speed, loaders for old and new asset formats, and
`r4fly`, a viewer that is the heir of perfly.

The name comes from the Rankine scale, the absolute version of the
Fahrenheit scale. Fahrenheit was the scene graph SGI, Microsoft and HP
announced in 1997 and never shipped. R4nkine is a homage to the OpenGL demos
of that era. The design is in [docs/DESIGN.md](docs/DESIGN.md).

## What is there

- `r4::gpu`: a backend interface modeled on the explicit APIs, with an
  **OpenGL 3.3** backend. The backend loads its functions at run time and
  works in core or compatibility contexts. It caches state and can save and
  restore the GL state of a host program.
- `r4::assets`: one neutral model format, and loaders for the formats of
  the SGI demos and their era:

  | Format | Files | From |
  | --- | --- | --- |
  | Wavefront OBJ/MTL | `.obj` | the smooth demos, everything else |
  | libgobj | `.d` | the flight simulator (state bits, transforms, lit and colored sections) |
  | YAODL | `.ydl`, `.defs` | powerflip and flip (polygons, NURBS, textures, sphere maps) |
  | flip objects | `.bin` | flip |
  | IRIS Performer | `.pfb` | Mekton, perfly; versions 1 and 2 (scrambled coordinates) |
  | Designer's Workbench | `.dwb` | the F-18 demo (encrypted textures) |
  | OpenFlight | `.flt` | 11, 12 and 14.2: palettes, subfaces, LOD, external references, `.attr` |
  | line drawings | `.gset` | the head-up display of Mekton |
  | glTF 2.0 | `.gltf`, `.glb` | today's assets (cgltf) |

  The image formats are **SGI** (`.rgb`, `.rgba`, `.bw`, `.int`, `.inta`; RLE
  or verbatim, 8 or 16 bits), **PPM/PGM**, the scrambled **5551E**
  images of perfly.face, and **PNG, JPEG, BMP, TGA, GIF** (stb_image). Textures and external references are searched next
  to the model and in the search paths (`addSearchPath`, `-F` in r4fly).
- `r4::scene`: Group, Geode, Switch, LOD, **Billboard** (axis or point) and
  **Instancer** nodes. An Instancer draws the same geometry at many places
  (forests, villages), culls each instance and picks its level of detail by
  distance. `collectParts` turns any loaded model into instancer parts.
  Bounding spheres are kept up to date lazily, so the graph can change at
  run time.
- `r4::render`: culling of whole subtrees, a draw list sorted by state,
  and **GPU instancing**: the model matrices go in a per-instance vertex
  stream, and consecutive items with the same mesh, material and pipeline
  are one draw call. Shading is per-pixel Blinn-Phong with fog. Triangles,
  lines and points are drawn; decals and sphere maps are supported.
- `r4fly`: the viewer. Use the trackball, `w` for wireframe and `s` for
  statistics. `-snap` saves a frame, `-view` places the eye, `-state`
  chooses the state bits of the models, `-F` gives the search paths, and
  `-forest N[,F]` plants N copies of the files with an Instancer (the
  first file near, the next ones beyond F, 2F...).

## Building

The dependencies are managed by [vcpkg](https://vcpkg.io) in manifest
mode (`vcpkg.json`):

| Dependency | For | Feature |
| --- | --- | --- |
| stb | the PNG, JPEG, BMP, TGA and GIF images | always |
| cgltf | glTF 2.0 | always |
| SDL2 | r4fly, the viewer | `viewer` (default) |

With `VCPKG_ROOT` set to a vcpkg folder, the presets of `CMakePresets.json`
use its toolchain, and vcpkg installs the dependencies at configure time:

```
cmake --preset linux          # or windows (VS 2022), windows-vs (the newest VS), macos, linux-debug
cmake --build --preset linux
ctest --preset linux
```

Without the viewer (and without SDL2), add `-DR4_BUILD_APPS=OFF`: the
`viewer` feature of the manifest is then left out.

```
out/build/linux/r4fly model.obj other.obj
out/build/linux/r4fly -F pftown:pftexture:pfmodel pftown/town.flt
```

## Using it

```cpp
#include "r4/assets/model.h"
#include "r4/render/renderer.h"

auto device = r4::gpu::createGLDevice(SDL_GL_GetProcAddress);
r4::render::Renderer renderer(*device);

auto root = r4::scene::make<r4::scene::Group>();
auto model = r4::assets::loadModel("cow.obj");
root->addChild(renderer.upload(*model));

r4::render::Camera camera;
camera.lookAt({0, -5, 2}, {0, 0, 0});
camera.perspective(45, 16.0f / 9, 0.1f, 100);
renderer.render(*root, camera, 0, 0, width, height);
```

## Planting a forest

```cpp
auto tree = renderer.upload(*r4::assets::loadModel("tree.glb"));
auto far  = renderer.upload(*r4::assets::loadModel("tree_card.obj"));
auto forest = r4::scene::make<r4::scene::Instancer>();
forest->addLevel(r4::scene::collectParts(*tree), 150);  // up to 150 m
forest->addLevel(r4::scene::collectParts(*far), 2000);  // then up to 2 km
for (const r4::Mat4 &m : placements)
    forest->addInstance(m);
root->addChild(forest);
```

## License

MIT, see [LICENSE](LICENSE).
