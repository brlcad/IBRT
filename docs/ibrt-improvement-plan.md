# IBRT Improvement Plan — 50 User-Visible + 24 Supporting

_Generated 2026-07-17 from an in-depth read of the current, non-vendored IBRT sources
(`apps/IBRT/**` excluding `imgui/` and `tiny_obj_loader.h`, and `plugins/brl_cad/**`), a
7-way subsystem mapping of the live user-facing surface, and the existing
[`docs/ibrt-review-plan.md`](ibrt-review-plan.md), [`TODO`](../TODO),
[`docs/color-plugin-integration-plan.md`](color-plugin-integration-plan.md), and
[`docs/ibrt_architecture.md`](ibrt_architecture.md)._

This plan is **feature/UX-oriented** and is a companion to the bug/correctness-oriented
`ibrt-review-plan.md` (whose stable IDs — `B-`, `SEC-`, `R-`, `T-`, `F-`, `BLD-` — are
cross-referenced below). Every item was checked against the code so nothing here is already
shipped. Note: the in-progress **OIDN "Denoise" toggle** is treated as done and is not listed.

Effort key: **S** ≤ half a day · **M** ~1–3 days · **L** > 3 days / needs design.

Legend: **[IMPLEMENTED]** items shipped in this pass live in their own files (see
§ *Implemented in this pass*); **[foundation]** marks a user-visible item that an
implemented module directly unblocks.

---

## Part A — 50 user-visible improvements

### A1. Navigation & camera (1–10)

1. **Standard view presets** — Front/Back/Top/Bottom/Left/Right/Iso via a View▸Standard Views
   submenu, in-viewport buttons, and `1/3/7`(+Ctrl)/`0` hotkeys; snaps the orbit eye to
   axis-aligned directions. **[IMPLEMENTED — `cameramath.h` wired into `RenderWidget::setStandardView` + menu/hotkeys]** _(M; renderwidget.cpp, mainwindow.cpp)_
2. **Orthographic projection toggle** — parallel projection for distortion-free measurement views. **[IMPLEMENTED — `OsprayBackend::ProjectionMode` + `RenderWidget::setOrthographic`; panel checkbox, View menu, `5` hotkey]** _(L; ospraybackend camera path + IPC + UI)_
3. **Preserve the camera on resize / fullscreen** — stop `resizeGL` calling `resetView()`, so maximizing or splitter moves no longer snap back to defaults + Orbit. _(M; renderwidget.cpp; B-14)_
4. **Sharp render on HiDPI displays** — size the render target in physical pixels so the ray-traced image isn't produced at 1/dpr and upscaled. _(M; renderwidget.cpp; B-13)_
5. **Frame-on-selection (F)** — recenter/zoom on the picked object instead of only whole-scene bounds. _(M; renderwidget.cpp)_
6. **Orbit sensitivity sliders** — expose rotate/pan/zoom speed like Fly already exposes fly-speed (`orbitSpeed_`/`panSpeed_`/`zoomFactor_` are currently hard-coded). _(S; renderwidget.cpp)_
7. **Invert-Y (and optional Invert-X) look/orbit toggle** — match user muscle memory. _(S; renderwidget.cpp)_
8. **Smooth animated camera transitions** — short eased interpolation for presets and Reset View so jumps aren't disorienting. **[foundation: `cameramath.h`]** _(M; renderwidget.cpp)_
9. **Camera bookmarks** — save/restore named viewpoints during a review session. **[foundation: `cameramath.h`]** _(M; renderwidget.cpp)_
10. **Middle-mouse pan + zoom-to-cursor** — MMB-drag pans (industry standard; MMB is currently captured only for ImGui) and the wheel zooms toward the point under the cursor. _(M; renderwidget.cpp)_

### A2. Viewport, overlays & feedback (11–20)

11. **Save Screenshot / Export PNG** — one button to write the current frame; today there's no in-app capture. _(S; renderwidget.cpp)_
12. **Persistent corner orientation gizmo / nav-cube** — always-on XYZ triad (click-to-snap to presets); the current axis lines only draw at the orbit pivot and vanish off-screen. _(M–L; renderwidget.cpp; TODO F-06)_
13. **Correct UI FPS / frame-time** — measure real `io.DeltaTime` instead of the hard-coded `1/60`. _(S; renderwidget.cpp; B-30)_
14. **In-viewport error toast/banner** — surface `lastError_` (loads, worker death) in the overlay instead of only status-bar/`QMessageBox`. _(M; renderwidget.cpp)_
15. **Collapsible panel sections** — `CollapsingHeader` for Stats/Renderer/Settings/Controls; the panel is one long scroll today. _(S; renderwidget.cpp)_
16. **Hover tooltips on render settings** — explain AO Distance, Early-Exit Depth, Roulette, Full-res Accumulation, etc. _(S; renderwidget.cpp)_
17. **Convergence progress bar + "settled" badge** — show accumulated/target ratio and a clear cue when refinement stops (safe to screenshot). _(S; renderwidget.cpp)_
18. **Toggle just the coordinate-axis overlay** — separate hotkey/checkbox from `G` (which hides the whole panel). _(S; renderwidget.cpp; TODO F-06)_
19. **Compact corner mode legend** — e.g. "PathTracer / Wireframe" stays visible when the panel is hidden with `G`. _(S; renderwidget.cpp)_
20. **Live eye position + FOV in stats** — center + azimuth/elevation are shown; add eye XYZ and FOV for reproducing views. _(S; renderwidget.cpp)_

### A3. Files, session & shell (21–32)

21. **Keyboard shortcuts for all menu actions** — `QKeySequence::Open/Quit`, view toggles, etc. _(S; mainwindow.cpp; R-13)_
22. **Fullscreen toggle (F11)** that preserves the camera (pairs with #3). _(S; mainwindow.cpp/renderwidget.cpp)_
23. **Show the loaded model/object in the window title** — currently a fixed string. _(S; mainwindow.cpp)_
24. **Recent Files menu** (QSettings-backed). _(M; mainwindow.cpp)_
25. **Persist session preferences** — up-axis, input mode, renderer, quality preset/custom, window geometry, and ImGui panel layout, so the app reopens as left. _(M; new `SessionSettings` + call sites; F-11)_
26. **Help menu + About dialog** — show IBRT/OSPRay/BRL-CAD/Qt versions and build info for bug reports (`IBRT_VERSION` is compiled but never displayed). _(S; mainwindow.cpp)_
27. **Dynamic Demo Models menu** — enumerate every deployed `*.g` (so shipped `axis.g` appears) instead of a hardcoded moss/havoc list. _(S; mainwindow.cpp; BLD-04)_
28. **Remember the last-used open directory** — the Open dialog currently always starts empty. _(S; mainwindow.cpp)_
29. **Drag-and-drop `.g`/`.obj` onto the window** to open. _(M; mainwindow.cpp)_
30. **Open a model path from the command line** — enables file associations / shell launch. _(S; main.cpp/mainwindow.cpp; F-01 adjacent)_
31. **"Reload current model" action** — pick up on-disk edits without re-navigating the dialog. _(S; mainwindow.cpp)_
32. **Honest unsupported-format handling** — the Open filter advertises `*.stl *.ply` but selecting them shows a generic "Not Yet Implemented" box; either hide them or give a clear message (until #46/STL lands). _(S; mainwindow.cpp)_

### A4. Rendering quality & appearance (33–42)

33. **Lighting brightness / exposure slider** — light intensities are fixed constants; dark/blown-out imports are currently unrecoverable. _(M; ospraybackend + UI; F-03)_
34. **Background color picker + light/dark/gradient presets** — background is hard-wired white (`backgroundColor=1.0`). _(M; ospraybackend + UI)_
35. **Fix and expose the halo/gradient backdrop** — a radial halo makes single objects read better. _(M; ospraybackend + UI; TODO "fix halo bgcolor")_
36. **Sun/light direction control (azimuth/elevation)** — key/fill/rim directions are compile-time constants; rotating the sun changes shading/shadows dramatically. _(M; ospraybackend + UI)_
37. **Ambient / AO intensity slider** — AO sample count is adjustable but ambient (0.18) and AO intensity (1.0) are hard-coded. _(S; ospraybackend + UI)_
38. **Tone-mapping / exposure image op for the path tracer** — filmic, presentable stills. _(M; ospraybackend + UI)_
39. **Renderer labels + tooltips** — "ao/SciVis/PathTracer" → friendly names with one-line guidance (fast preview vs physically based). _(S; renderwidget.cpp)_
40. **Sun-sky environment toggle + turbidity** — a visible sky for attractive outdoor lighting (path tracer already builds a dim `sunSky`). _(M; ospraybackend + UI)_
41. **32× progressive rung + real dynamic scaling** — instant coarse preview on heavy assemblies; drive rung choice by measured frame time (fields already tracked but unused). _(L; ospraybackend; F-07, TODO 13/17)_
42. **Bilinear upsampling of progressive passes** — remove blocky nearest-neighbor previews. _(S; ospraybackend; TODO "test upscale interpolation")_

### A5. Geometry inspection & analysis (43–48)

43. **Region-color ↔ neutral/clay shading toggle** — the plugin already honors a `colorEnabled` param end-to-end; only the UI/IPC toggle is missing (form study vs true colors). _(S; ospraybackend + UI + plugin)_
44. **Per-region visibility / solo from the hierarchy tree** — hide or isolate regions/combs to inspect internals without reloading whole top objects. _(L; ospraybackend + UI + plugin)_
45. **Click-to-identify region + highlight** — report/outline the hit region (the hit callback already knows the region pointer/name). _(L; plugin + UI)_
46. **Interactive cut/clip planes** — reveal CSG interiors/cross-sections (ray-plane `tnear/tfar` clamp in the librt trace). _(L; plugin + ospraybackend + UI; TODO, F-08)_
47. **Components-on-shotline probe** — pick a ray, list the ordered regions/thicknesses crossed (classic BRL-CAD analysis). _(M; plugin + ospraybackend + UI; TODO, F-08)_
48. **Ray-shading color modes** — cell plot (2D value grid + edge overlay), region tint, projected value texture, each with a **color-ramp legend**. **[foundation: `colorramp.h`]** _(L; per `color-plugin-integration-plan.md`)_

### A6. Distribution & reliability the user feels (49–50)

49. **Headless CLI render mode** — batch-render a `.g`/object to an image without the GUI.
    **[IMPLEMENTED — new `IBRTOfflineRender` tool (`offline_render.cpp`)]** _(M; F-01)_
50. **Self-contained installer / package** — CPack (NSIS/DMG/zip) that bundles OSPRay/Qt/BRL-CAD runtimes + the plugin + demo models + README/licenses, so a clean machine can run it; today `cmake --install` yields a non-runnable tree. _(L; CMake/packaging; BLD-01)_

---

## Part B — 24 supporting (non-user-visible) improvements

These back the Part-A work: modularity so features have a home, dedup so a fix lands once,
code-smell removal, and testing/build so features ship reliably.

### B1. Modularity & structure

- **N1. Extract a `Camera` type** (orbit/fly pose + all conversions) out of `RenderWidget`.
  Unblocks presets (#1), bookmarks (#9), animation (#8). **[IMPLEMENTED — first step: `cameramath.h`]** _(L; R-05 adjacent)_
- **N2. Split `resizeGL` framing from aspect-update** — a lightweight aspect-only path is the
  prerequisite for "preserve camera on resize" (#3). _(S)_
- **N3. Single `setPose(eye,target,up)` sink** routed to backend + worker, replacing the
  per-mode `syncCameraToBackend` duplication. Simplifies #1/#8/#9. _(M)_
- **N4. Decompose the ~590-line `paintGL`** into `blitFrame / beginImGuiFrame / drawStats /
  drawRendererControls / drawRenderSettings / drawControls`. _(L; R-05)_
- **N5. Data-driven `LightRig` struct** so brightness/direction/tone controls (#33/#36/#38) wire
  to one place instead of inline constants. _(M)_
- **N6. Introduce a `SessionSettings` wrapper over QSettings** — one tested load/save path for
  recent files (#24), prefs (#25), window geometry. _(M)_

### B2. Deduplication

- **N7. Collapse worker-vs-in-process branching** in every settings widget behind one
  `applyState(state)` adapter driven by `RenderSettingsState` (~15 controls triplicate the same
  read/two-write pattern). _(L; R-06)_
- **N8. Preset ladder as one `constexpr` table** `{startScale, aoSamples, pixelSamples}` indexed
  by preset, replacing the triplicated values across two seed functions + per-parameter switches. _(S; R-07)_
- **N9. Collapse the ~9 `configured*ForCurrentMode` getters** into one resolver returning a
  `QualityParams` struct. _(M)_
- **N10. Dedup the Windows/POSIX worker dispatch loop** (~220 lines, 13 handlers copy-pasted)
  behind a `Transport` abstraction + one `runWorkerLoop`. _(M; R-02)_
- **N11. Dedup `RenderWorkerClient::sendRequest*`** across platforms behind an abstract
  handle + close callback. _(M; R-04)_
- **N12. Dedup the Qt→ImGui key-translation blocks** in `keyPressEvent`/`keyReleaseEvent`
  into one table-driven helper; factor the inline-label + pulsing-progress-bar helpers. _(S)_
- **N13. Share the `rt_dirbuild`+cleanup boilerplate** across wireframe / hierarchy / object-list
  paths (prep for edge/shotline modes #45–#47). _(M; R-10)_

### B3. Code-smell removal

- **N14. Name magic literals once** — IPC magic `0x54425249`, watchdog `1500`, `3.14159265f`,
  the `1.77079633f` phi seed, pitch clamp, fly-step factor. **[IMPLEMENTED — `ibrt_constants.h`]** _(S; R-12)_
- **N15. Bound IPC `payloadSize`** with a named max before allocating (unbounded → OOM/terminate).
  **[IMPLEMENTED — `kMaxIpcPayloadSize` in `ibrt_constants.h`, enforced by `ipc_wire.h`]** _(S; SEC-01)_
- **N16. Remove the mutable `getAoSamples() int&`** accessor that bypasses clamping. _(S; R-09)_
- **N17. Remove debug `fprintf` "STEP N" scaffolding + emoji crash marker** from `loadBrlcad`;
  route the few useful lines through the verbose gate. _(S; R-01)_
- **N18. Decouple per-hit color from `hit.u/hit.v`** in the plugin (explicit shared channel) and
  make per-hit color a pluggable callback — prerequisite for #45/#47/#48. _(M; R-11, B-23)_

### B4. Testing

- **N19. Round-trip unit tests for the pure camera math** (orbit↔fly, angles↔forward, eye↔orbit,
  view presets), up-axis parameterized. **[IMPLEMENTED — `cameramath.h` + `tests_units.cpp`]** _(M; T-10)_
- **N20. Tolerance / perceptual image comparator** shared by unit tests and the reference CTest,
  replacing the brittle exact SHA-256 check. **[IMPLEMENTED — `imagecompare.h` + `tests_units.cpp`]** _(M; T-01)_
- **N21. Hoist IPC wire structs into one header with `static_assert(sizeof…)`** so field drift is a
  compile error, not silent memory corruption. **[IMPLEMENTED — `ipc_wire.h`]** _(M; R-03, B-25)_
- **N22. Split `IBRTTests` into labelled CTests with timeouts** (`unit`/`integration`/`system`),
  replace bare-`return` fixture guards with `QSKIP`, and make fixture/tool discovery portable
  (no hardcoded `C:/brlcad-build` / PowerShell paths). _(M; T-02/T-03/T-04)_
- **N23. Headless plugin-intersection CTest** — commit a `brlcad` geometry and assert
  `tfar`/`Ng`/unpacked region color for fixed rays, catching ABI/packing regressions fast. _(M; T-05)_

### B5. Build & packaging

- **N24. `cmake --install` produces a runnable tree** + committed Windows/MSVC build presets and a
  CI matrix (Win/macOS) with published artifacts — the foundation under installer #50. _(M–L; BLD-01/02/05)_

---

## Implemented in this pass

Five self-contained modules, each in its own new file so they can be reviewed and accepted
individually. All are header-only pure utilities (no Qt/OSPRay/BRL-CAD dependency except where
noted), verified by a new plain-C++ `IBRTUnitTests` target.

| File | Backs plan items | Notes |
|------|------------------|-------|
| `apps/IBRT/ibrt_constants.h` | N14, N15 (R-12, SEC-01) | Named IPC magic/version, `kMaxIpcPayloadSize`, watchdog default, `kPi`, documented initial orbit angles. |
| `apps/IBRT/ipc_wire.h` | N21 (R-03, B-25) | One authoritative definition per IPC payload + `static_assert` on every `sizeof`; enforces `kMaxIpcPayloadSize`. Depends on `ibrt_constants.h`. |
| `apps/IBRT/colorramp.h` | N/A→#48, "shared ramp" | 11-bin + 3-bin value→RGBA ramp, packing, out-of-range color, and legend descriptors (per `color-plugin-integration-plan.md` §4). |
| `apps/IBRT/cameramath.h` | N1, N19 (#1/#8/#9) | Templated pure camera math: `forwardFromAngles`/`anglesFromForward` round-trip, orbit direction, framing distance, and standard view directions (front/back/top/bottom/left/right/iso), up-axis parameterized. |
| `apps/IBRT/imagecompare.h` | N20 (T-01) | Tolerance image diff over raw RGBA buffers: mean/max per-channel abs error and %-pixels-over-threshold. |

Verification: `apps/IBRT/tests_units.cpp` (new `IBRTUnitTests` CTest, plain C++ — no Qt/OSPRay)
exercises `colorramp`, `cameramath`, and `imagecompare`; `ibrt_constants` and `ipc_wire` are
checked at compile time by their `static_assert`s.

These are intentionally additive: none rewrites existing behavior, so each can be adopted by the
live code paths incrementally in follow-up changes.

### Wired into the UI (follow-up pass)

`cameramath.h` is now **load-bearing**, not just a foundation:

- **Standard view presets (user-visible item #1)** — `RenderWidget::setStandardView()` uses
  `cameramath::standardView` + `orbitAnglesFromEyeDirection` to snap the orbit camera to
  Front/Back/Left/Right/Top/Bottom/Iso. Exposed three ways: a **View ▸ Standard Views** submenu
  (`mainwindow.cpp`), a row of **in-viewport buttons** under "Standard Views" (`renderwidget.cpp`
  overlay), and **hotkeys** `1`/`3`/`7` (front/right/top, `Ctrl` = opposite) and `0` (iso),
  handled in `keyPressEvent` after the ImGui keyboard-capture guard so they never clash with
  numeric text fields.
- **Camera-math dedup (internal N1)** — `RenderWidget::worldUp / worldForwardReference /
  forwardFromAngles / anglesFromForward / orbitEyeDirection / fitDistanceFromBounds /
  setOrbitFromEyePosition` now delegate to `cameramath.h` (behavior-preserving), and the orbit
  seed / π literals were replaced with `ibrt_constants.h` values. The full `IBRTTests` suite
  still passes, confirming no regression.

Verified with a full MSVC build of `IBRT`, `IBRTTests`, and `IBRTUnitTests`, plus
`ctest` (`IBRTUnitTests` and `IBRTTests` green).

**Orthographic projection (user-visible item #2)** is now implemented end-to-end:

- **Backend** — `OsprayBackend::ProjectionMode {Perspective, Orthographic}` with
  `setProjectionMode/projectionMode`. Camera-param application was funneled through one
  `applyCameraParams()` (perspective → `fovy`, orthographic → OSPRay `height` =
  `2·distance·tan(fovy/2)`, so framing is preserved across the toggle), and the OSPRay camera
  object is recreated (its type is immutable) by `rebuildCameraForProjection()` deferred to
  `applyPendingState` between frames — the same safe-point pattern as the denoiser rebuild. The
  last pose is retained in `cameraState_`.
- **IPC** — projection rides the existing settings channel (like the denoiser): a new
  `projectionMode` field in `RenderSettingsState`, `SettingsPayload` (client + both worker
  handlers), the authoritative `ipc_wire.h` struct (size assert 68 → 72 bytes), and the
  `qualitysettings` mirror.
- **UI** — `RenderWidget::setOrthographic/isOrthographic` is the single entry point for a
  Render-Settings **checkbox**, a **View ▸ Orthographic** checkable menu item (kept in sync via a
  new `projectionModeChanged` signal), and the **`5`** hotkey. `projectWorldToScreen` (axis
  overlay) is now projection-aware so the gizmo stays aligned in ortho.

Verified: full MSVC build of `IBRT`/`IBRTTests`/`IBRTUnitTests` and `ctest` green (the
`IBRTTests` worker suite round-trips the widened settings payload).

### Headless offline renderer (user-visible item #49)

New `IBRTOfflineRender` CLI tool (`apps/IBRT/offline_render.cpp`, target in
`apps/IBRT/CMakeLists.txt`) renders a `.g`/object to a PNG with no GUI:

```
IBRTOfflineRender <db.g> <object|auto> <out.png>
  [--renderer scivis|pathtracer|ao] [--projection perspective|orthographic]
  [--width N --height N] [--az DEG --el DEG] [--fovy DEG]
  [--frames N] [--pixel-samples N] [--up z|y] [--show-sky]
```

It reuses the same `OsprayBackend`, and the camera is placed with
`cameramath::eyeDirectionFromAzEl` (a new az/el → center→eye helper, unit-tested)
at `cameramath::fitDistanceFromBounds`. `object=auto` picks a sensible top object
(file-stem match → `all` → first). Background defaults to a clean white via the
new **`OsprayBackend::setEnvironmentVisible(false)`** (partial progress on
appearance items #34/#40): it hides the path-tracer sky/sun as *background* while
keeping their illumination, so escaped rays show the white `backgroundColor`
instead of the sky dome. `makeDefaultLights` now takes the visibility flag.

Validated by rendering `toyjeep.g` and `havoc.g` at 1024×1024, path tracer, az/el
35/25, in both perspective and orthographic — all four produced correct
white-background stills.
