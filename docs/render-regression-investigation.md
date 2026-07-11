# Render Regression Investigation

## Result

The rendering corruption introduced while moving from embedded OSPRay to the
bext OSPRay 3.2 package was an ISPC SIMD ABI mismatch. The bext CPU module was
compiled with `--target=neon-i32x8`, while the out-of-tree BRL-CAD plugin was
compiled with an empty `--target=` argument.

ISPC callbacks cross the shared-library boundary for geometry intersection and
post-intersection shading. Compiling the caller and callback for different
program widths corrupted lane data, producing checker patterns, horizontal
stripes, invalid normals, and missing colors. It was not a Qt, framebuffer,
TBB, Embree hit/miss, or color-space problem.

IBRT now sets `ISPC_TARGET_CPU` from the installed OSPRay package's
`OSPRAY_ISPC_TARGET_LIST`. Configuration fails instead of silently building an
ABI-incompatible plugin when the package does not report its ISPC target.

## Commit Walk

| Historical change | Result | Decision |
| --- | --- | --- |
| `d6e65f2` embedded OSPRay baseline | Exact reference SHA-256 `610457...164` | Ground truth |
| `79031c3` external bext OSPRay 3.2 | Checker-pattern corruption | Keep restructure; fix external plugin ISA |
| `a9e04d1` move packed color from `primID` to `u/v` | Removed unsafe primitive-ID use, but exposed gray/striped ABI corruption | Keep |
| `77a76a4` remove temporary Apple runtime hacks | Exact reference match after ISA fix | Keep |
| `a3262c4` add raw renderer tests | Does not compile alone and path tracer captures a 2x2 progressive frame | Keep only with `4ddc7e8`; test still needs redesign |
| `4ddc7e8` cancel in-flight frames | Exact reference match | Keep |
| `ec2394b` remove remaining Apple hacks | Exact reference match | Keep |
| `fb12eaa` architecture documentation | No rendering effect | Keep |

The latest tree with the ISA fix produces a PNG byte-for-byte identical to the
embedded baseline. `IBRTReferenceMossSciVis` regenerates that image through the
external bext stack and checks its SHA-256 during CTest.

## Separate Loader Issue

On the tested macOS bext OSPRay 3.2 build, loading `brl_cad` after CPU device
initialization can fail because CPU module factory symbols were loaded with
local visibility. Requesting `brl_cad` through `OSPRAY_LOAD_MODULES` before
`ospInit` works and is already used by the viewer and tests. This load-order
issue is independent of the image corruption and should not be worked around
by copying libraries into bext or adding platform-specific dependency scans.
