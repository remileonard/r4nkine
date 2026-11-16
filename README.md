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
- `r4::assets`: one neutral model format, with Wavefront **OBJ/MTL**, **SGI**
  (`.rgb`, `.rgba`, `.bw`; RLE or verbatim, 8 or 16 bits) and **PPM/PGM**
  loaders.
- `r4::scene`: Group, Geode, Switch and LOD nodes. Bounding spheres are kept
  up to date lazily, so the graph can change at run time.
- `r4::render`: culling of whole subtrees, a draw list sorted by state,
  per-frame uniform upload, and per-pixel Blinn-Phong with fog.
- `r4fly`: the viewer. Use the trackball, `w` for wireframe and `s` for
  statistics; `-snap` saves a frame.

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The library needs only a C++17 compiler. `r4fly` needs SDL2 (`libsdl2-dev`,
vcpkg `sdl2`, or the SDL2 development package on Windows). Set
`-DR4_BUILD_APPS=OFF` to build without it.

```
build/r4fly model.obj other.obj
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

## License

MIT, see [LICENSE](LICENSE).
