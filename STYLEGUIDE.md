# IBRT Style Guide

This repository contains the IBRT viewer, its render worker, and
repo-owned OSPRay plugins. The goal of this guide is to keep that code
easy to evolve now that OSPRay itself lives outside this tree.

## Core Expectations

- Prefer clear local reasoning over clever abstractions.
- Keep app code, plugin code, and build-system glue separated.
- Treat `bext` and BRL-CAD as external dependencies, not places to write
  generated artifacts into.
- Preserve cross-platform behavior for macOS, Linux, and Windows when
  touching runtime deployment or packaging logic.

## C++ and Qt

- Prefer standard library containers and RAII over manual ownership.
- Keep raw-pointer lifetimes short and obvious.
- Use `override` consistently where virtual overrides are intended.
- Favor small helper functions when UI, worker, and backend concerns
  start to blur together in one routine.
- Comments should explain why a piece of code exists or what invariant
  it preserves, not restate the code.

## OSPRay Plugin Code

- Build plugins against the installed OSPRay SDK found through
  `BEXT_INSTALL_DIR`.
- Keep plugin-specific code inside `plugins/<name>` and avoid
  introducing assumptions that only hold for the BRL-CAD plugin.
- When using OSPRay or Embree internals, document any non-obvious
  coupling to the external SDK version.

## Build System

- The committed configure contract is `BEXT_INSTALL_DIR` plus
  `BRLCAD_PREFIX`.
- New runtime copy logic should deploy into this repo's build or install
  outputs only.
- Prefer repo-owned CMake helpers in `cmake/` over scattering dependency
  logic across many directories.

## Documentation

- Keep repo docs focused on IBRT, its plugins, and its local workflows.
- If a note depends on upstream OSPRay source, label it clearly as an
  external reference instead of linking to a path that does not exist in
  this repo.
