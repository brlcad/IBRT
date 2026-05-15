# Contributing

## Before Opening A Change

- Build from the repo root.
- Run `ctest --test-dir <build-dir> --output-on-failure`.
- Update docs when the build contract, layout, or runtime behavior changes.

## Build Contract

The committed CMake interface for this repo is:

- `BEXT_INSTALL_DIR`
- `BRLCAD_PREFIX`

Do not reintroduce repo-local OSPRay forks, superbuild logic, or per-dependency prefix variables unless there is a strong project-level reason.

## Pull Request Expectations

- Keep changes focused.
- Call out runtime or deployment implications.
- Include screenshots for viewer UI changes when helpful.
- Mention any gaps if you could not run the full build or test suite.

## Code Notes

- `apps/IBRT` is the viewer and worker.
- `plugins/*` is where OSPRay plugins belong.
- `docs/*` should describe project behavior, not upstream OSPRay internals.
