# rayshade — mock ray-shading pipeline

A small, self-contained C++17 library providing a single class, `rayshade::RayShader`,
that models a ray-shading pipeline: load a bundle of models, probe geometry with rays,
queue rays for batch evaluation, and read per-channel shading values that a caller maps
to color.

**This is a development harness.** The implementation is a deterministic placeholder —
it computes nothing meaningful. Values are derived by hashing their inputs so results are
stable across runs for a given seed. It exists so that dependent code (viewers, plugins,
UIs) can be built and tested against a stable interface before a real backend is wired in.

## Layout

- `include/rayshade/RayShader.h` — the public interface (pimpl; no external dependencies).
- `src/RayShader.cpp` — the mock implementation.
- `examples/smoke.cpp` — usage example / smoke test of the intended call sequence.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Call sequence (typical)

```cpp
rayshade::RayShader shader("app");
shader.loadBundle("some.bundle", "");
size_t model = shader.getModelIndex(shader.getModelNames()[0]);
size_t inst  = shader.addInstance(model, "inst_0");

// geometry only:
auto hits = shader.probeRay(model, {{-500,0,0}, {1,0,0}});

// per-cell shading value:
shader.setSeed(seed);
shader.clearQueuedRays();
shader.queueRay(rayPreset, origin, dir, speed, range, seed);
shader.evaluateQueuedRays();
double v = shader.evaluateShadingParam(inst, channel, element); // [0,1)
shader.resetSamples();
```
