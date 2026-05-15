# Repo Layout

## Top Level

- `apps`
  End-user applications and app-local tooling
- `plugins`
  OSPRay plugins owned by this repo
- `cmake`
  Repo-owned CMake helpers
- `docs`
  Project-specific documentation

## Application Area

- `apps/IBRT`
  The Qt viewer, render worker, tests, vendored ImGui sources, and app packaging helper

## Plugin Area

- `plugins/brl_cad`
  The BRL-CAD geometry bridge for OSPRay, including the ISPC intersection code and module initializer

## What Is Intentionally Not Here

- vendored OSPRay source
- OSPRay sample apps
- OSPRay superbuild logic
- upstream OSPRay docs and baseline image data

Those dependencies come from the external `bext` install tree referenced by `BEXT_INSTALL_DIR`.
