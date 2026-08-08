# IBRT

IBRT is the Interactive BRL-CAD Ray Tracer. This repository now holds our application and our OSPRay plugins, not a vendored OSPRay source tree.

IBRT is currently beta software. Expect incomplete features and report reproducible
problems through the repository's GitHub issue tracker.

## What Lives Here

- `apps/IBRT`
  The Qt desktop viewer, render worker, and test suite.
- `plugins/brl_cad`
  The BRL-CAD OSPRay plugin built as `ospray_module_brl_cad`.
- `docs`
  Project-specific build notes, layout notes, and performance plans.

## Dependency Model

OSPRay, Qt, Embree, rkcommon, ISPCRT, TBB, and related runtime dependencies come from an external `bext` install tree supplied at CMake configure time with `BEXT_INSTALL_DIR`. Alternatively, point `BRLCAD_EXT_DIR` at the bext build directory (the one holding `install/` and `noinstall/`, as BRL-CAD does); the install tree is then `BRLCAD_EXT_DIR/install`.

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

### Debug and Release

A Debug or Release IBRT build must use BRL-CAD and bext built in the same
configuration; on Windows the two cannot be mixed. IBRT uses one configuration
per build directory, so Debug and Release builds coexist and you switch by
choosing a preset:

```sh
cmake --preset everything/Release && cmake --build --preset everything/Release
cmake --preset everything/Debug   && cmake --build --preset everything/Debug   # needs Debug deps
```

Each preset builds into `build/everything/<Config>`. `everything` is shorthand
for `everything/Release`. A configure-time check fails fast with a clear message
if the dependencies do not match the build type.

More detail lives in [docs/building.md](docs/building.md).

## Runtime Notes

- `IBRT` and `IBRTRenderWorker` use OSPRay from the supplied `bext` install.
- The BRL-CAD plugin is built in this repo and deployed into the local runtime output for the app, worker, and tests.
- Demo BRL-CAD databases are loaded from `BRLCAD_PREFIX/share/db` and copied into local viewer runtimes when available.

## Repository Docs

- [Building](docs/building.md)
- [Controls and Key Bindings](docs/keybindings.md)
- [Repo Layout](docs/repo-layout.md)
- [Performance Notes](docs/perf/ibrt-moss-performance-plan.md)
- [Release Checklist](docs/releasing.md)
