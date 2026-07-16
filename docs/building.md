# Building IBRT

## Required Inputs

The repo expects two primary configure-time inputs:

- `BEXT_INSTALL_DIR`
  The built `bext` install tree, typically `<bext>/build/everything/install`.
  Alternatively set `BRLCAD_EXT_DIR` to the bext build directory (the one
  holding `install/` and `noinstall/`, as BRL-CAD does); `BEXT_INSTALL_DIR` is
  then taken as `BRLCAD_EXT_DIR/install`.
- `BRLCAD_PREFIX`
  The BRL-CAD install prefix

The repo resolves OSPRay, Qt, Embree, rkcommon, ISPCRT, and related runtime pieces from `BEXT_INSTALL_DIR`.

## Configure

```sh
cmake -S /path/to/IBRT -B /path/to/IBRT/build/local \
  -DBEXT_INSTALL_DIR=/path/to/bext/.build/install \
  -DBRLCAD_PREFIX=/path/to/brlcad/install
```

If Qt is not included in your `bext` install, extend `CMAKE_PREFIX_PATH` with the Qt prefix:

```sh
cmake -S /path/to/IBRT -B /path/to/IBRT/build/local \
  -DCMAKE_PREFIX_PATH=/path/to/Qt \
  -DBEXT_INSTALL_DIR=/path/to/bext/.build/install \
  -DBRLCAD_PREFIX=/path/to/brlcad/install
```

## Build

```sh
cmake --build /path/to/IBRT/build/local
```

The default build enables:

- `IBRT`
- `IBRTRenderWorker` on Windows, macOS, and Linux
- `ospray_module_brl_cad`
- `IBRTTests` when testing is enabled

## Test

```sh
ctest --test-dir /path/to/IBRT/build/local --output-on-failure
```

## Useful Options

- `IBRT_BUILD_VIEWER=OFF`
  Skip the desktop viewer target
- `IBRT_BUILD_PLUGIN_BRL_CAD=OFF`
  Skip the BRL-CAD plugin target
- `IBRT_BUILD_TESTS=OFF`
  Skip the test executable
- `IBRT_ENABLE_RENDER_WORKER=OFF`
  Build only the in-process viewer path

Set `IBRT_VERBOSE_RENDER_LOG=1` at runtime to emit the per-frame scheduling log
used for renderer performance investigations.

## Debug vs Release

A Debug or Release IBRT build must use BRL-CAD and bext built in the same
configuration. On Windows a Debug and a Release build cannot be mixed at all,
so a Debug IBRT build requires a Debug BRL-CAD **and** a Debug bext, and a
Release build requires the Release trees.

Because the BRL-CAD and bext trees are resolved once, at configure time, a
build directory can only match a single configuration. IBRT therefore uses one
configuration per build directory (collapsing multi-config generators such as
Visual Studio to the selected configuration) and lets Debug and Release builds
coexist as separate directories. On Windows, a configure-time check fails fast
with a clear message when the dependencies do not match the build type.

The generator is **not** fixed by the presets, so the same preset works with
Ninja, Visual Studio, or another generator — pick one with `-G`, the
`CMAKE_GENERATOR` environment variable, or your IDE.

## Presets

The committed [CMakePresets.json](../CMakePresets.json) defines:

| Preset               | Build type | Build directory            | Dependencies   |
| -------------------- | ---------- | -------------------------- | -------------- |
| `everything/Release` | Release    | `build/everything/Release` | `deps-release` |
| `everything/Debug`   | Debug      | `build/everything/Debug`   | `deps-debug`   |
| `everything`         | Release    | `build/everything/Release` | `deps-release` |

`everything` is shorthand for `everything/Release`. Each configuration lives in
its own directory, so both can exist on disk at once — switch by choosing a
preset:

```sh
cmake --preset everything/Release
cmake --build --preset everything/Release
ctest --preset everything/Release

cmake --preset everything/Debug          # requires provisioned Debug deps
cmake --build --preset everything/Debug
ctest --preset everything/Debug
```

The dependency paths live in the hidden `deps-release` / `deps-debug` presets.
To point them at your own trees without editing the committed file, create a
`CMakeUserPresets.json` (gitignored) that overrides `BEXT_INSTALL_DIR` /
`BRLCAD_PREFIX`, or pass them on the command line:

```sh
cmake --preset everything/Debug \
  -DBEXT_INSTALL_DIR=/path/to/bext-debug/install \
  -DBRLCAD_PREFIX=/path/to/brlcad/debug
```

For scripted or CI builds, the [build.sh](../build.sh) helper honors
`CMAKE_BUILD_TYPE`, `BEXT_INSTALL_DIR`, and `BRLCAD_PREFIX` from the
environment.
