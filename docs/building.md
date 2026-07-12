# Building IBRT

## Required Inputs

The repo expects two primary configure-time inputs:

- `BEXT_INSTALL_DIR`
  The built `bext` install tree, typically `<bext>/build/everything/install`
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

## Presets

The committed [CMakePresets.json](../CMakePresets.json) uses environment variables:

- `BEXT_INSTALL_DIR`
- `BRLCAD_PREFIX`

Example:

```sh
export BEXT_INSTALL_DIR=/path/to/bext/.build/install
export BRLCAD_PREFIX=/path/to/brlcad/install
cmake --preset ninja-relwithdebinfo
cmake --build --preset build-relwithdebinfo
ctest --preset test-relwithdebinfo
```
