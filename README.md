# IBRT

IBRT is the Interactive BRL-CAD Ray Tracer. This repository now holds our application and our OSPRay plugins, not a vendored OSPRay source tree.

## What Lives Here

- `apps/IBRT`
  The Qt desktop viewer, render worker, and test suite.
- `plugins/brl_cad`
  The BRL-CAD OSPRay plugin built as `ospray_module_brl_cad`.
- `docs`
  Project-specific build notes, layout notes, and performance plans.

## Dependency Model

OSPRay, Qt, Embree, rkcommon, ISPCRT, TBB, and related runtime dependencies come from an external `bext` install tree supplied at CMake configure time with `BEXT_INSTALL_DIR`.

This repo does not vendor or build OSPRay itself anymore.

## Build

You need:

- a `bext` install tree, typically something like `<bext>/.build/install`
- a BRL-CAD install prefix
- CMake 3.20+
- a working C++ toolchain

Example:

```sh
cmake -S . -B build/local \
  -DBEXT_INSTALL_DIR=/path/to/bext/.build/install \
  -DBRLCAD_PREFIX=/path/to/brlcad/install
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

If Qt is not part of the supplied `bext` install, extend `CMAKE_PREFIX_PATH` when configuring.

More detail lives in [docs/building.md](/Users/morrison/IBRT/docs/building.md).

## Runtime Notes

- `IBRT` and `IBRTRenderWorker` use OSPRay from the supplied `bext` install.
- The BRL-CAD plugin is built in this repo and deployed into the local runtime output for the app, worker, and tests.
- Demo BRL-CAD databases are loaded from `BRLCAD_PREFIX/share/db` and copied into local viewer runtimes when available.

## Repository Docs

- [Building](/Users/morrison/IBRT/docs/building.md)
- [Repo Layout](/Users/morrison/IBRT/docs/repo-layout.md)
- [Performance Notes](/Users/morrison/IBRT/docs/perf/ibrt-moss-performance-plan.md)
