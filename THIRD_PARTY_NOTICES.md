# Third-Party Notices

This repo vendors a small set of third-party source files inside `apps/IBRT`.

## Dear ImGui

- Location: `apps/IBRT/imgui`
- Upstream: [Dear ImGui](https://github.com/ocornut/imgui)
- License: MIT

## tinyobjloader

- Location: `apps/IBRT/tiny_obj_loader.h`
- Upstream: [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
- License: MIT

## Binary Runtime Dependencies

Release archives may also redistribute libraries and data from the following
projects. These components are not relicensed by IBRT and retain their own
copyright and license terms.

| Project | Typical role | Upstream license |
| --- | --- | --- |
| [BRL-CAD](https://github.com/BRL-CAD/brlcad) | Geometry libraries and demo databases | LGPL, BSD, and public-domain components; see BRL-CAD's `COPYING` and per-file terms |
| [OSPRay](https://github.com/ospray/ospray) | Rendering runtime | Apache-2.0 |
| [Embree](https://github.com/RenderKit/embree) | Ray tracing kernels | Apache-2.0 |
| [rkcommon](https://github.com/ospray/rkcommon) | Rendering utility runtime | Apache-2.0 |
| [ISPC/ISPCRT](https://github.com/ispc/ispc) | SPMD compiler runtime | BSD-3-Clause |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | Task runtime | Apache-2.0 |
| [Qt](https://www.qt.io/) | Desktop UI runtime | LGPL/GPL or commercial, according to the Qt distribution used |

Binary distributors must include the exact license texts and notices shipped by
the dependency builds used to produce an archive. This summary is not a
substitute for those files.
