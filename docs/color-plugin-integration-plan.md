# Ray-Shading Color Plugin for IBRT — Plan of Record

**Status:** Draft / design agreed in principle. Phase 1 (T1) approved to start.
**Owners:** IBRT dev.
**Related:** `docs/ibrt_architecture.md`, `docs/repo-layout.md`.
**Backend:** the `rayshade::RayShader` ray-shading library (mock harness at `D:/devtools/rayshade-mock`; interface in `include/rayshade/RayShader.h`).

> During early development IBRT builds against the `RayShader` mock, which returns stable
> placeholder values. The production backend is swapped in later behind the same interface.

---

## 1. Goal

Let IBRT interactively display per-ray **shading values** produced by a ray-shading backend
(`RayShader`), color-coded on a fixed color ramp — reproducing, then extending, the 2D grid
views an offline cell viewer produces, with **no secondary disk I/O** in the interactive hot path.

Three visualization techniques, delivered as three phases. All three are **selectable render modes**
in the IBRT UI, exactly like the existing Solid/Wireframe selector (`apps/IBRT/renderwidget.cpp:719-730`)
and the ao/SciVis/PathTracer renderer selector (`renderwidget.cpp:737-776`).

| Phase | Technique | View | What you see |
|-------|-----------|------|--------------|
| **1** | **Cell plot (2D)** | camera **is** the probe direction | grid of colored cells (one probe ray per cell) under a hidden-line/edge rendering of the geometry. |
| **2** | **Region tint (3D)** | orbit freely; probe direction **frozen/decoupled** | geometry shaded normally but each region flat-tinted by its shading value for the current probe direction. |
| **3** | **Projected value texture (3D)** | orbit freely; probe direction frozen/decoupled | spatially varying per-ray shading value projected onto the geometry from the probe direction. |

---

## 2. Guiding architectural principle: decouple *evaluation* from *rendering*

- **`RayShader` is an evaluation backend.** For a given *(model, ray preset, direction)* it produces
  shading values. This is comparatively expensive and changes **only** when the ray preset, direction,
  or geometry changes — **not every frame**.
- **OSPRay stays the renderer, always.** It keeps path tracing, AO, shading, and (future)
  MPI/distributed rendering. Its per-frame job in these modes is to *consume* a compact `RayShader`
  result cheaply (a value grid or a per-region LUT), never to call `RayShader` inside the per-ray hot path.

Corollary: **do not** call `RayShader` from inside the OSPRay/Embree ISPC intersection callback
(`plugins/brl_cad/geometry/brlcad.cpp:289` `traceRay`). That path is SIMD-vectorized and first-hit-only
(`a_onehit = 1`, `brlcad.cpp:305`); the backend is heavy scalar C++ needing the full hit list along a
ray. The "callback that returns a color" becomes a **cheap array lookup** (a value LUT) in that hit path
for T2/T3; the heavy backend work happens out-of-band.

### Process/model placement
The backend runs **inside the existing render worker process** (`apps/IBRT/worker_main.cpp` /
`OsprayBackend`), reachable over the existing binary IPC (`apps/IBRT/worker_ipc.h`; `SetRenderer=15`,
`RequestFrame=13`, `FrameData=14`). Results flow back through the existing pixel/`FrameData` path — no
new disk, minimal new IPC. (A separate evaluation worker process remains a later option if isolation
demands it.)

---

## 3. The backend interface: `RayShader`

**Driver decision: use `rayshade::RayShader`** (single class, pimpl, self-contained header). Its call
sequence maps 1:1 onto what IBRT needs; it runs **zero secondary disk I/O** in the hot path.

### What `RayShader` already provides
- `probeRay(model_index, Ray) -> vector<Sample{Vec3 entry; size_t region_id}>` — ray geometry (which
  regions a ray crosses). Walks all hits along the ray.
- `queueRay(ray_index, origin, direction, speed, range, seed[, time])` → `evaluateQueuedRays()` →
  `evaluateShadingParam(instance_index, channel_index, element_index) -> double` — the shading value in
  [0,1].
- `getChannels` / `getChannelType` / `getChannelElements` — catalog to populate the color-source
  dropdown (types include `scalar`, `mask`, …).
- `getRegionName(model_index, region_id)`, `getModelNames`, `getRayNames`, `getBounds`,
  `getSceneCoordinates`/`getLocalCoordinates`, `getViewSpec`/`getSceneSpec`, `addInstance`/`moveInstance`,
  `setSeed`, `resetSamples`/`resetScene`, `loadBundle`/`setResourceRoot`.

### The multi-stage pipeline `RayShader` hides (why we don't reimplement it)
A single shading value is **not** one primitive call; the backend sequences and resets it correctly:
1. **Probe** — cast the ray, get the ordered hit list along it (pooled, in-memory).
2. **Accumulate** — deposit per-hit contributions into an internal accumulator (in-memory).
3. **Evaluate** — reduce the accumulator into per-region and per-channel values.
4. **Read** — read back the selected channel element as the shading value.

Stage 3 is **not** auto-run by stage 2; internal state resets between rays via `resetSamples()`. The
`RayShader` facade already sequences and resets all of this — that is the argument for using it rather
than driving the internal stages directly.

---

## 4. Color ramp (all phases)

Hard-code the standard 11-bin ramp, half-open `[low,high)` bins over the selected value's [min,max]
(default 0..1):

| Fraction | Color | RGB |
|---|---|---|
| 0.00–0.05 | dim grey | 105,105,105 |
| 0.05–0.15 | light steel blue | 176,196,222 |
| 0.15–0.25 | navy | 0,0,128 |
| 0.25–0.35 | royal blue | 65,105,225 |
| 0.35–0.45 | aquamarine | 127,255,212 |
| 0.45–0.55 | dark green | 0,100,0 |
| 0.55–0.65 | lime green | 50,205,50 |
| 0.65–0.75 | yellow | 255,255,0 |
| 0.75–0.85 | orange | 255,165,0 |
| 0.85–0.95 | light coral | 240,128,128 |
| 0.95–1.00 | red | 255,0,0 |

Pack to the framebuffer's existing RGBA `uint32_t`. Add an ImGui legend. Keep a 3-bin option as an
alternate ramp.

---

## 5. Phase 1 — Cell plot (T1)  *(build first)*

### 5.1 Semantics
Camera **is** the probe direction. A regular grid of cells is laid over the current view; each cell is
one probe ray fired along the view direction; the cell is colored by a selectable value. Two synchronized
passes are composited:

1. **Cell pass** — colored grid (value ramp), coarser resolution.
2. **Edge pass** — hidden-line / outline rendering of the geometry drawn **on top**.

### 5.2 Reference dual-pass recipe (to reproduce)
- **One camera for both passes.**
- **Cell pass:** for each ray, a cell rectangle in view-plane `(u,v)`, mapped to screen; fill with
  `colorRamp(value)`; light background. 1:1 ray→cell.
- **Cell value:** the selected channel element, averaged across stochastic iterations when the ray
  preset is stochastic; out-of-`[min,max]` → an out-of-range color.
- **Edge pass:** hidden-line render of the *same* geometry from the *same* camera into a separate
  buffer, with edge detection on **region-id boundaries**, **normal discontinuity**, and **depth
  discontinuity**; black edges; cached per view.
- **Composite:** overlay non-background edge pixels on top of the colored cell buffer. **In IBRT,
  composite in-buffer** (mask by background) — no external tool.

**IBRT realization of the edge pass:** produce it in OSPRay from the same camera. Two viable sources:
(a) reuse/extend the existing **Wireframe** visualization mode (`renderwidget.cpp:726-730`); or (b) run
edge detection on a **region-id + depth + normal** buffer (the brl_cad plugin already has per-hit region
+ normal + distance at `brlcad.cpp:247-278`), matching the reference recipe most faithfully.
Recommendation: (b) for fidelity; spike (a) first for speed.

### 5.3 `RayShader` call pattern (Phase 1, no backend changes required)
Correct per-cell values require **one ray per evaluation** (queuing many rays accumulates onto the
instance rather than yielding per-ray values). Honest baseline:

```cpp
// once, at bundle load (see §8):
size_t inst = shader.addInstance(model_index, "ibrt");
// resolve color source from the dropdown: (channelIdx, elementIdx)

// per evaluation pass (on camera/preset/param change), over a coarse WxH grid, per worker thread:
for (cell c in myGridSlice) {
    shader.setSeed(seedFor(c));           // deterministic per cell
    shader.clearQueuedRays();
    shader.queueRay(ray_index, originFor(c), dirFor(c), speed, range, seedFor(c));
    shader.evaluateQueuedRays();          // runs one evaluation internally
    value[c] = shader.evaluateShadingParam(inst, channelIdx, elementIdx);  // [0,1]
    shader.resetSamples();
}
// color[c] = colorRamp(value[c]); blit cell buffer; composite edge pass on top.
```

Because `evaluateQueuedRays` runs one internal evaluation per queued ray, this is **O(cells)
evaluations** — the central performance constraint (see §5.5).

### 5.4 Color-source selector
Populate an ImGui dropdown from `getChannels`/`getChannelType`/`getChannelElements`. **Default = the
primary scalar channel element** — note a 2D cell's value is inherently a *whole-ray* value (a ray
crosses many regions), so a per-region value is the natural default for **T2's** surface tint, not a 2D
cell. *(Open item 11.1: confirm the exact default element against a real bundle.)*

### 5.5 Performance strategy
- **Coarse grid while interacting, refine on idle** — reuses IBRT's existing progressive/dynamic-quality
  behavior (`docs/ibrt_architecture.md` §2.3). Start e.g. 48×48–128×128 cells; a cell plot is coarse by
  nature.
- **Threading:** one `RayShader` **per worker thread** (each instance is independent). Partition the grid
  across threads.
- **Deterministic per-cell seed** (`setSeed(row*W+col)`) → reproducible, cacheable, race-free.
- **Cache** value[c] keyed by (cell ray, ray preset, seed); recompute only cells whose ray changed. Big
  win on pure camera pans of a fixed grid.
- **Stochastic ray presets:** 1 iteration while moving; accumulate iterations on idle (maps to OSPRay
  accumulation), averaging.
- **Zero disk I/O:** inherent to `RayShader` (nothing in the hot path writes to disk).

### 5.6 Worker/IPC integration
- Add a `ShadeBackend` (or a mode within the worker) selected via `SetRenderer("shade_cellplot")` —
  reuses camera, framebuffer, and `FrameData`.
- Add a `LoadBundle` message (or extend `LoadBrlcad`) carrying the bundle path + key.
- Add UI: a new mode in the visualization selector; bundle/model/ray-preset pickers; color-source
  dropdown; ramp legend; grid-resolution control.

### 5.7 Optional Phase-1 backend additions (fast-follow, not blockers)
- `struct RayShader::CellBounds{double lu,lv,uu,uv;}; std::vector<CellBounds> getCellBounds(size_t queued_ray_index) const;`
  and `GridSpec getGridSpec(...) const;` — so cell rectangles and the `(u,v)→screen` transform come from
  the backend, guaranteeing edge/cell registration.
- **Grid batch evaluator** returning a per-cell value array in one call — removes the O(cells) call
  overhead and is the real T1 throughput win.

### 5.8 Phase-1 acceptance
Compare IBRT's T1 image against the reference offline cell view on a sample bundle (same direction,
grid, color source) within tolerance.

---

## 6. Phase 2 — Region tint (T2)  *(planned)*

### 6.1 Semantics
Orbit the geometry freely; each region is flat-tinted by its value for a **frozen** probe direction. The
probe direction is decoupled from the camera: provide a "freeze direction to current view" action and/or
independent yaw/pitch controls.

### 6.2 Pipeline
1. On direction/preset/geometry change, `RayShader` runs one evaluation pass (a grid of rays from the
   frozen direction) and produces a **per-region value LUT** (`region_id → value`).
2. The brl_cad OSPRay plugin holds the LUT in its C++/ISPC shared struct
   (`plugins/brl_cad/geometry/BRLCADShared.h`) and, in the hit path, maps the hit's geometry region →
   `RayShader` region_id → `LUT[region_id]` → color ramp → packs the color (replacing/extending the
   region-color path at `brlcad.cpp:157-178,247-278`).
3. OSPRay renders at full interactive rate; the LUT refreshes (buffer write + commit) only when the
   backend recomputes. **MPI-friendly:** the LUT is tiny and broadcasts to all ranks.

### 6.3 Required backend additions
- `size_t RayShader::getRegionCount(size_t model_index) const;`
- `std::vector<double> RayShader::getRegionValues(size_t instance_index) const;` — per-region value for
  the last evaluation, indexed by `region_id`.
- `size_t RayShader::getRegionIdByGeomId(size_t model_index, int geom_region_id) const;` (and/or the
  inverse) — so IBRT can map an OSPRay hit's geometry region back to a backend region_id without
  re-deriving the mapping (avoids divergence).

### 6.4 Notes
- Requires the **geometry-region ↔ backend-region identity** IBRT deferred in T1. Prefer the backend
  *exposing* the map over IBRT re-deriving it, to guarantee agreement.
- Flat-per-region is an overview; per-point spatial detail is T3.

---

## 7. Phase 3 — Projected value texture (T3)  *(planned)*

### 7.1 Semantics
Orbit freely; surfaces show spatially varying value of the ray that passed through them. Achieved by
projecting the per-ray value image (from the frozen direction) onto the geometry from the probe direction
(projective texturing in OSPRay).

### 7.2 Required backend additions (on top of T2)
- Extend `struct RayShader::Sample` from `{Vec3 entry; size_t region_id;}` to
  `{Vec3 entry; Vec3 exit; size_t region_id; double thickness; double value;}` — populate exit/thickness
  from the hit list already walked in `probeRay`, and attach a per-hit value.

### 7.3 Notes
- Standard projective texturing; the evaluation pass is shared with T1/T2.
- Alternative to projection: bake a value texture from the probe direction and sample it during shading.

---

## 8. One-time initialization (all phases)

Driven through `RayShader`, once per session (hold resident; one instance per worker thread):
1. `RayShader shader(program_name); shader.setResourceRoot(resource_path);`
2. `shader.loadBundle(...)` — parses the bundle and prepares geometry (builds an acceleration structure
   once).
3. Enumerate `getModelNames`/`getRayNames`/`getChannels`/`getChannelElements`; resolve model/ray/instance
   indices and the color-source element.
4. `addInstance`/`moveInstance`; align IBRT's camera frame to the scene via `getBounds`/`getSceneCoordinates`.

For v1, **let the backend own its ray casting** (its own acceleration structure from the bundle's
geometry); IBRT passes camera rays and gets values. A second acceleration structure costs RAM but the
evaluation is infrequent, and it removes any geometry-region reconciliation for T1. (T2/T3 reintroduce the
mapping, exposed via the new accessors above. A shared-geometry path — feeding IBRT hits to the backend —
is a later memory/throughput optimization.)

---

## 9. Cross-cutting concerns

- **Geometry-engine version match (do first).** The production backend links the same geometry engine
  (BRL-CAD librt) as IBRT; they **must be the same** version or the `rt_i`/`vect_t` structs can silently
  mismatch. Reconcile before any linking. *(N/A for the mock, which has no geometry-engine dependency.)*
- **Dependency footprint.** Consume a reduced backend library, not a monolithic one that drags in
  UI/scripting deps. Feature-flag the shade modes so the worker builds without the backend. *(The mock is
  already minimal.)*
- **Threading:** one `RayShader` per worker thread; seed per ray.
- **Units & coordinates:** the backend and IBRT must agree on units and the view-plane `(u,v)→screen`
  convention exactly, or the edge and cell passes misregister.
- **Locked bundles:** production bundles may restrict region-name / full-hit-list access via bundle
  options; confirm the bundles IBRT will use aren't restricted in a way that blocks needed data. *(N/A for
  the mock.)*

---

## 10. Consolidated backend additions

| For | Signature (proposed) |
|-----|----------------------|
| T1 (opt) | `std::vector<RayShader::CellBounds> getCellBounds(size_t queuedRayIdx) const;` `GridSpec getGridSpec(size_t queuedRayIdx) const;` |
| T1 (opt, perf) | grid batch evaluator → per-cell `std::vector<double>` |
| T2 (req) | `size_t getRegionCount(size_t modelIdx) const;` |
| T2 (req) | `std::vector<double> getRegionValues(size_t instanceIdx) const;` |
| T2 (req) | `size_t getRegionIdByGeomId(size_t modelIdx,int geomRegionId) const;` |
| T3 (req) | extend `Sample` → `{entry,exit,region_id,thickness,value}` |
| opt | `RayShader::renderToBuffer(...)` (in-memory image instead of file) |

---

## 11. Open questions & risk register

1. **Default color source for T1** — confirm the exact scalar channel element against a real bundle; the
   element↔region correspondence is undocumented.
2. **T1 performance** — `evaluateQueuedRays` is O(cells) serial evaluations; validate interactive feel
   with coarse grid + threading before committing; prioritize the grid batch evaluator if needed.
3. **Locked bundles** — confirm the bundles IBRT will use aren't restricted in a way that blocks region
   IDs / full hit lists.
4. **Edge pass fidelity** — decide reuse-Wireframe vs region-id/normal/depth edge detection.
5. **`getRegionValues` lifecycle** — exposing per-region values means retaining/recomputing at the right
   time and confirming it carries a reduced scalar vs a raw value still needing reduction.
6. **Re-entrancy/threading of the backend** under live IBRT use is unproven at interactive rates —
   validate early.
7. **Reference dual-pass specifics** (hidden-line internals, grid spec format, composite semantics) —
   re-verify when implementing the edge/composite step.
8. **Geometry-engine version skew** between the backend and IBRT builds (see §9) — the highest-severity
   integration risk for the production swap.

---

## 12. Milestones

- **M0 — De-risk (spike):** standalone console app links `RayShader` (mock), loads a bundle, queues +
  evaluates a few rays, prints values. Proves deps and coordinate framing. *(Done for the mock — see
  `rayshade-mock/examples/smoke.cpp`.)*
- **M1 — T1 skeleton:** `ShadeBackend` + `SetRenderer("shade_cellplot")`; fixed coarse grid,
  single-threaded, static camera; returns `FrameData`.
- **M2 — T1 camera + UI:** grid driven by the real camera; color-source dropdown; ramp legend;
  bundle/model/ray-preset pickers.
- **M3 — T1 dual pass:** edge pass + composite; match the reference offline output (acceptance §5.8).
- **M4 — T1 performance:** N threads (one `RayShader` each), deterministic seeds, per-cell cache,
  progressive coarse→fine, idle iteration accumulation.
- **M5 — T2:** backend additions (§6.3); per-region LUT in the brl_cad plugin; region tint mode.
- **M6 — T3:** extended `Sample` (§7.2); projected value texture mode.
