# Current Moss render reference

This image records the current SciVis lighting and scene appearance for `moss.g` / `all.g` at 1200 × 900. The scene SHA-256 is `c130ccd3418ffc1742ee2c46a8518b8636f1641bc8ea26d3271fe58ecbd5b6f1`. It was generated with `IBRTReferenceRender` from this codebase, the Debug bext OSPRay 3.2 runtime, and a local BRL-CAD build on 2026-10-02. Three independent renders produced the same SHA-256: `4a404bcd3af4c4a6a36f3f3e7603dbce40365b75ea51088d38a7e8a648fdf5a9`.

The older `d6e65f2d...` reference is retained as a historical rendering artifact. Current lights and shading have changed since that snapshot. The CTest comparison now decodes RGBA pixels and applies the tolerance in `imagecompare.h`, so PNG encoding and small SIMD differences do not fail the test.
