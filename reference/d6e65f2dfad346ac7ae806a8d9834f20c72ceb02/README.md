# Known-good Moss render

This directory captures the rendering baseline from IBRT commit
`d6e65f2dfad346ac7ae806a8d9834f20c72ceb02`, built with its embedded OSPRay
CPU module and BRL-CAD plugin.

## Reference image

- File: `moss-all_scivis_1200x900.png`
- SHA-256: `610457100347a4607548ffed6668a964ef8d2167279b6dadb94e46c6b72df164`
- Size: 1200 x 900, RGB PNG, 121662 bytes
- Scene: BRL-CAD `moss.g`, object `all.g`
- Scene SHA-256: `c130ccd3418ffc1742ee2c46a8518b8636f1641bc8ea26d3271fe58ecbd5b6f1`
- Renderer: SciVis
- Render scale: 1 (full resolution)
- Pixel samples: 1
- AO samples: 1
- Accumulated frames: 16
- Camera: the viewer's Reset View calculation, Z-up, 60-degree vertical FOV

The exporter reads `OSP_FB_SRGBA` as the documented packed integer layout
(`R` in bits 0-7, `G` in 8-15, `B` in 16-23, `A` in 24-31), then explicitly
applies the known-good viewport's red/blue swap and vertical flip. `QImage` is
used only to encode the already-converted RGB rows as PNG.

Two independent renders produced the exact SHA-256 above.

## Build matrix

- IBRT/embedded OSPRay source: `d6e65f2dfad346ac7ae806a8d9834f20c72ceb02`
- Embedded OSPRay: 3.3.0
- BRL-CAD source: `c363f3206ea04f209a7a3e1c7e4c27d5042083ad`
- rkcommon: 1.15.2 from bext
- Embree: 4.4.1 from bext
- ISPC: 1.30.0 from bext
- oneTBB: 2023.0.0 from bext
- Qt: 6.10.2 from bext

## Reproduce

From this baseline build:

```sh
.build-d6e65f2dfad346ac7ae806a8d9834f20c72ceb02/viewer/IBRTReferenceRender \
  /Users/morrison/brlcad.main/.build/share/db/moss.g \
  all.g \
  reference/d6e65f2dfad346ac7ae806a8d9834f20c72ceb02/moss-all_scivis_1200x900.png
```

Verify it with:

```sh
shasum -a 256 reference/d6e65f2dfad346ac7ae806a8d9834f20c72ceb02/moss-all_scivis_1200x900.png
```
