# IBRT moss.g Performance Plan

## Baseline Capture

- Capture date: 2026-05-14
- Capture window: 2026-05-14 20:10:45 EDT to 2026-05-14 20:11:11 EDT
- Host: `Miniagua.local`, `Darwin arm64` ([metadata.txt](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/metadata.txt:1))
- Scene: default startup demo model `moss.g`
- Artifacts:
  - Baseline report: [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md)
  - UI flame graph: [IBRT.svg](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRT.svg)
  - Worker flame graph: [IBRTRenderWorker.svg](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.svg)
  - Worker raw sample: [IBRTRenderWorker.sample.txt](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt)
  - Launch log: [launch.log](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log)
- Thread-count conclusion: 7 active render workers were observed in the sampled run, plus 1 worker main/IPC thread. The worker sample shows the main worker thread at [IBRTRenderWorker.sample.txt:24](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:24), idle workqueue helpers at [IBRTRenderWorker.sample.txt:83](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:83) and [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213), and the active TBB render workers beginning at [IBRTRenderWorker.sample.txt:87](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:87).

## Executive Summary

- The UI process was mostly idle during the capture. `mach_msg2_trap` accounted for `90.5%` of UI leaf samples in the baseline report, so the app process itself was not compute-bound for this run ([report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md)).
- The worker is dominated by one stack: `Renderer_default_renderTask -> SciVis_renderSample -> rtcIntersect8 -> brlcadIntersectN_C -> traceRay -> rt_shootray` ([report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md), [IBRTRenderWorker.sample.txt:87](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:87)).
- Worker leaf hotspots were `rt_shootray 29.5%`, `write 12.2%`, `rt_in_rpp 10.1%`, `rt_boolfinal 6.6%`, `rt_arb_shot 5.8%`, and `rt_boolweave 4.1%` ([report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md)).
- Interactivity wins are available before deep renderer surgery. The transport path copies and transmits full frames even when nothing changed, and automatic mode still escalates hard views to full-resolution accumulation despite hundreds of milliseconds of frame time ([worker_main.cpp:259](/Users/morrison/IBRT/apps/IBRT/worker_main.cpp:259), [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449), [launch.log:2697](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2697)).
- The preferred execution order is: `PERF-001 -> PERF-002 -> PERF-003 -> PERF-004 -> PERF-005 -> PERF-006 -> PERF-007 -> PERF-008/PERF-009/PERF-010`. This is the recommended order of work, not a dependency graph.

## Evidence And Citations

- [worker_main.cpp:259](/Users/morrison/IBRT/apps/IBRT/worker_main.cpp:259): `RequestFrame` always serializes the frame header and pixel payload after `advanceRender()`, even when `updated == false`.
- [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449): the client always requests a frame, allocates a `QImage`, and copies the full pixel buffer into it.
- [renderwidget.cpp:580](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:580): `paintGL()` performs `rgbSwapped().mirrored(false, true)` every frame before drawing.
- [renderwidget.cpp:1884](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:1884): the polling thread continuously requests worker frames and stores returned images.
- [renderwidget.cpp:1999](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:1999): the UI applies the pending image and calls `update()` without checking whether the image content actually changed.
- [ospraybackend.cpp:250](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:250): `timeBudgetMs` is currently unused in `advanceRender(int)`.
- [ospraybackend.cpp:1377](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1377): the progressive ladder unconditionally advances through `16 -> 8 -> 4 -> 2 -> 1`.
- [ospraybackend.cpp:1407](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1407): full-resolution accumulation work is launched once the state machine reaches accumulate mode.
- [ospraybackend.cpp:1443](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1443): completed frames always copy pass or accumulation pixels back into display storage.
- [ospraybackend.cpp:1555](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1555): AO backoff exists, but it is reactive and only runs after slow progressive frames complete.
- [ospraybackend.h:219](/Users/morrison/IBRT/apps/IBRT/ospraybackend.h:219): watchdog and dynamic-mode state exists but is not yet wired into an effective in-flight governor.
- [brlcad.cpp:279](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:279): `traceRay()` builds one `application` and calls `rt_shootray()` for a single ray.
- [brlcad.cpp:341](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:341): `brlcadIntersectN_C()` loops over packet lanes and calls `traceRay()` per lane, scalarizing Embree packets at the BRL-CAD bridge.
- [brlcad.cpp:421](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:421): `numPrimitives()` exposes the BRL-CAD scene to Embree as exactly one user primitive.
- [brlcad.cpp:510](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:510): `createEmbreeUserGeometry()` registers a single user-geometry object with that one primitive count.
- External OSPRay source `ospray/api/Device.cpp:104` from the `bext`-supplied SDK: OSPRay consumes `OSPRAY_NUM_THREADS` when initializing the tasking system.
- [bbox.c:163](/Users/morrison/brlcad.main/src/librt/bbox.c:163): `rt_in_rpp()` is a scalar slab-style bounding-box test and a measurable leaf hotspot.
- [prep.cpp:1118](/Users/morrison/brlcad.main/src/librt/prep.cpp:1118): `rt_get_solidbitv()` reuses or allocates bit vectors and clears them on reuse, contributing to per-ray bookkeeping overhead.
- [vshoot.c:242](/Users/morrison/brlcad.main/src/librt/vshoot.c:242): BRL-CAD has vector-shot hooks, but the generic `rt_vshootray()` path still bombs at the missing `boolweave` step, so it is not a near-term drop-in optimization.
- [launch.log:2653](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2653): camera 2 ramps from `0.94 ms -> 2.01 ms -> 9.29 ms` in progressive passes and then climbs to `25.91 ms`, `29.67 ms`, `38.31 ms`, and `72.46 ms` in full-resolution accumulation.
- [launch.log:2697](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2697): camera 3 ramps from `4.41 ms -> 13.45 ms -> 59.85 ms` in progressive passes and then spends many frames between roughly `344 ms` and `991 ms` in full-resolution accumulation.

## Recommendation Tracker

| ID | Priority | Status | Area | Expected Benefit | Evidence | Next Action | Validation |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PERF-001 | P0 | ready | Frame transport | Lower IPC, fewer CPU copies, less redundant UI work, faster apparent responsiveness when the image is unchanged | [worker_main.cpp:259](/Users/morrison/IBRT/apps/IBRT/worker_main.cpp:259), [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449), [renderwidget.cpp:1999](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:1999) | Define a no-pixel payload path for `updated == false`, keep stats flowing, and skip repaint on unchanged frames | Reprofile worker `write`, client copy, and UI paint cost on the same `moss.g` scene |
| PERF-002 | P0 | ready | Dynamic resolution | Biggest immediate interactivity win on hard views by refusing to escalate to scale `1` too early | [ospraybackend.cpp:250](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:250), [ospraybackend.cpp:1377](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1377), [launch.log:2697](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2697) | Add a real scale governor in automatic mode that holds or backs off scale on slow views | Re-run the same hard camera views and confirm scale `2` or coarser is retained when frame times blow past the target |
| PERF-003 | P0 | ready | Watchdog | Prevent long blocking full-res frames and preserve responsiveness during hard views | [ospraybackend.h:219](/Users/morrison/IBRT/apps/IBRT/ospraybackend.h:219), [ospraybackend.cpp:1407](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1407), [ospraybackend.cpp:1555](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1555) | Implement real in-flight timeout/cancel logic tied to automatic mode and reuse existing counters in telemetry | Confirm that hard views stop producing multi-hundred-millisecond stalls and that cancel counters rise in expected cases |
| PERF-004 | P1 | ready | Threading | Potentially recover the 8th core for render work, modest throughput gain if scaling is favorable | External OSPRay source `ospray/api/Device.cpp:104`, [IBRTRenderWorker.sample.txt:24](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:24), [IBRTRenderWorker.sample.txt:87](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:87) | Expose a worker-side thread-count control and benchmark default vs `8` vs `9` threads | Compare frame times and sample thread utilization across the same scene and camera views |
| PERF-005 | P1 | ready | UI presentation | Reduce per-frame image conversion overhead and lower UI-side copy pressure | [renderwidget.cpp:580](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:580), [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449) | Remove `rgbSwapped().mirrored()` from the hot path by aligning worker output with UI expectations | Confirm identical visual output and reduced UI-side sampling cost |
| PERF-006 | P1 | proposed | Spatial partitioning | Highest-upside throughput change by letting Embree cull more before BRL-CAD callback work | [brlcad.cpp:341](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:341), [brlcad.cpp:421](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:421), [brlcad.cpp:510](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:510) | Prototype scene partitioning options for BRL-CAD user geometry and choose a culling granularity | Measure callback count, `rt_shootray` share, and frame time before and after |
| PERF-007 | P2 | proposed | Per-ray bookkeeping | Mid-single-digit potential by trimming memory churn around bit vectors and tables | [prep.cpp:1118](/Users/morrison/brlcad.main/src/librt/prep.cpp:1118), [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213) | Inspect reuse patterns for solid bit vectors, `bu_bitv_clear`, and `bu_ptbl_reset` during repeated ray evaluation | Use sampling or counters to show reduced time in bookkeeping helpers without correctness regressions |
| PERF-008 | P2 | blocked | Packet bridge | Could improve packet efficiency, but currently blocked by incomplete BRL-CAD vector path plumbing | [brlcad.cpp:341](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:341), [vshoot.c:242](/Users/morrison/brlcad.main/src/librt/vshoot.c:242) | Document the exact `rt_vshootray()` blockers and decide whether to extend bundle logic or keep per-lane tracing | Unblock only after a viable boolweave-compatible path is identified |
| PERF-009 | P3 | proposed | SIMD bbox | Potential local speedup in a hot routine, but limited leverage unless higher-level culling remains unchanged | [bbox.c:163](/Users/morrison/brlcad.main/src/librt/bbox.c:163), [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md) | Estimate achievable speedup for `rt_in_rpp()` and compare it to broader culling work | Microbenchmark and end-to-end sample impact must justify the added complexity |
| PERF-010 | P3 | proposed | Boolean routines | May yield targeted routine wins after bigger structural bottlenecks are reduced | [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md), [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213) | Inspect `rt_boolfinal`, `rt_boolweave`, and `rt_arb_shot` call patterns after earlier fixes land | Reprofile after upstream changes so routine-level work is not optimized against a stale bottleneck mix |

## Recommendation Details

### PERF-001 Frame transport

- Classification: Immediate interactivity win
- Why it matters: The worker main thread spends visible time in `write()` and the client/UI side still copies and applies frames even when only metadata changed. This is the cheapest place to recover responsiveness because it avoids redundant work without changing render correctness.
- What to change: Split frame delivery into two cases. When `updated == true`, send pixels plus metadata as today. When `updated == false`, send header and stats only, skip the pixel payload, avoid `QImage` allocation/copy, and do not call `update()` unless the displayed image actually changed.
- Why it is low-hanging or not: Low-hanging. The `updated` bit already exists in the protocol, but it is not used to avoid transport or presentation work.
- Risks: IPC format changes need to stay backward-compatible within the current worker/client pair. The UI still needs frame-time, scale, watchdog, and renderer-name telemetry even when pixels are unchanged.
- Validation: Reprofile the same `moss.g` flow and confirm lower worker `write` cost, fewer UI-side copies, and no visual regressions when the image is stable.
- Citations: [worker_main.cpp:259](/Users/morrison/IBRT/apps/IBRT/worker_main.cpp:259), [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449), [renderwidget.cpp:1884](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:1884), [renderwidget.cpp:1999](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:1999)

### PERF-002 Dynamic resolution

- Classification: Immediate interactivity win
- Why it matters: Automatic mode always climbs to scale `1`, even for hard views that immediately become unresponsive. Camera 3 is the clearest example: `4.41 ms -> 13.45 ms -> 59.85 ms` in progressive passes, followed by many full-resolution frames in the `344 ms` to `991 ms` range.
- What to change: Convert automatic mode from a fixed escalation ladder into a real governor. Hold at the current coarse scale when frame times exceed the target, back off when repeated frames are too slow, and only advance to finer scales after repeated success within budget.
- Why it is low-hanging or not: Low-hanging relative to architectural work because the state machine already exists. The missing piece is making the decision logic honor performance outcomes instead of blindly progressing.
- Risks: Overly conservative scaling could make image refinement feel sluggish on easy views. The governor should bias toward responsiveness during motion and refinement when stable.
- Validation: Re-run camera 2 and camera 3. Confirm that hard views remain interactive, spend more time at scale `2` or coarser, and stop entering long runs of slow scale `1` accumulation.
- Citations: [ospraybackend.cpp:250](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:250), [ospraybackend.cpp:1377](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1377), [ospraybackend.cpp:1407](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1407), [launch.log:2653](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2653), [launch.log:2697](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2697)

### PERF-003 Watchdog

- Classification: Immediate interactivity win
- Why it matters: The backend already tracks watchdog-related state and counters, but it does not actually prevent in-flight full-resolution work from blowing through the interactive budget. That leaves the renderer vulnerable to multi-hundred-millisecond stalls.
- What to change: Implement actual time-based in-flight cancellation or preemption for automatic mode, tie it to `inFlightStart_`, record the event in existing counters, and route the next frame back to a coarser or otherwise cheaper mode.
- Why it is low-hanging or not: Medium effort, but still an early candidate because much of the state plumbing already exists.
- Risks: Cancellation semantics need to be safe with OSPRay futures and the worker/UI protocol. Incorrect cancellation could leave stale frame state or produce flicker if reset behavior is inconsistent.
- Validation: Force the hard camera views, confirm that over-budget full-resolution frames are canceled or avoided, and verify that `watchdogCancelCount_` becomes meaningful telemetry rather than a dead field.
- Citations: [ospraybackend.h:219](/Users/morrison/IBRT/apps/IBRT/ospraybackend.h:219), [ospraybackend.cpp:1407](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1407), [ospraybackend.cpp:1443](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1443), [ospraybackend.cpp:1555](/Users/morrison/IBRT/apps/IBRT/ospraybackend.cpp:1555), [launch.log:2697](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log:2697)

### PERF-004 Threading

- Classification: Medium-effort throughput win
- Why it matters: The sampled run used 7 active TBB render workers while the worker main thread handled IPC and packaging. There may be headroom to use all 8 cores for render work by allowing one extra render thread and tolerating mild oversubscription.
- What to change: Expose thread count in the worker launch or settings path and benchmark default behavior against explicit `OSPRAY_NUM_THREADS` values, starting with default, `8`, and `9`.
- Why it is low-hanging or not: Low implementation effort, but modest expected upside. This is more likely to produce incremental throughput gains than transformative interactivity gains.
- Risks: Oversubscription can hurt if the worker main thread, UI, or OSPRay internals become contention-heavy. Gains may vary by scene and renderer mode.
- Validation: Measure frame times, CPU utilization, and sample thread activity on the same scene and camera views. Keep the best setting only if it improves median and tail latency rather than peak throughput alone.
- Citations: External OSPRay source `ospray/api/Device.cpp:104`, [IBRTRenderWorker.sample.txt:24](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:24), [IBRTRenderWorker.sample.txt:87](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:87)

### PERF-005 UI presentation

- Classification: Medium-effort throughput win
- Why it matters: The UI is not the primary bottleneck, but it still performs avoidable image conversions and copies. Removing them reduces CPU overhead and makes the UI path cleaner once transport costs fall.
- What to change: Align worker output format and orientation with what the UI wants to draw, then eliminate `rgbSwapped().mirrored(false, true)` from the hot path. If possible, keep pixel ownership stable enough to avoid extra full-image copies.
- Why it is low-hanging or not: Moderate effort. The local code change is simple, but the correct format handoff between worker and UI needs verification.
- Risks: Easy to regress image orientation, channel order, or alpha behavior. Any change should be checked visually across renderers and resize events.
- Validation: Compare before/after output visually and confirm reduced UI-side activity in `paintGL()` and related copy paths.
- Citations: [renderwidget.cpp:580](/Users/morrison/IBRT/apps/IBRT/renderwidget.cpp:580), [renderworkerclient.cpp:449](/Users/morrison/IBRT/apps/IBRT/renderworkerclient.cpp:449), [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md)

### PERF-006 Spatial partitioning

- Classification: Longer-horizon architectural win
- Why it matters: Embree currently sees the BRL-CAD scene as one user primitive. That means it can cull against the scene bounds, but it cannot spatially subdivide BRL-CAD internals before invoking the callback, so too many rays fall into the expensive `traceRay() -> rt_shootray()` path.
- What to change: Split BRL-CAD exposure into multiple Embree-visible primitives or cells. Candidate partitioning schemes include region-level grouping, bounding-box clustering, or explicit spatial cells over prepared geometry.
- Why it is low-hanging or not: Not low-hanging. This is the highest-upside structural change, but it requires design work around geometry ownership, bounds generation, and callback mapping.
- Risks: Partition granularity can trade culling quality against build cost and memory pressure. Incorrect mapping could also break selection, instancing, or shading expectations.
- Validation: Add instrumentation around callback counts and compare worker samples before and after. Success should show fewer expensive BRL-CAD callback invocations and a lower inclusive share for `brlcadIntersectN_C` and `rt_shootray`.
- Citations: [brlcad.cpp:341](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:341), [brlcad.cpp:421](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:421), [brlcad.cpp:510](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:510), [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md)

### PERF-007 Per-ray bookkeeping

- Classification: Medium-effort throughput win
- Why it matters: Once the biggest architectural issues are accounted for, bookkeeping churn around bit vectors and pointer tables is large enough to matter. The sample shows `bu_ptbl_reset`, `rt_get_solidbitv`, and related helpers in the hot path alongside geometric work.
- What to change: Inspect how solid bit vectors and segment tables are reused per resource and per ray. Favor cheaper reuse patterns, reduce full clears where safe, and consider epoch-based or sparse marking techniques if they fit BRL-CAD invariants.
- Why it is low-hanging or not: Moderate effort. The routines are localized, but they sit on correctness-sensitive query state.
- Risks: This logic affects hit/miss correctness and boolean evaluation bookkeeping. Any optimization here must preserve exact traversal semantics.
- Validation: Compare time spent in `bu_ptbl_reset`, `rt_get_solidbitv`, and `bu_bitv_clear` before and after, then confirm identical rendered output for representative scenes.
- Citations: [prep.cpp:1118](/Users/morrison/brlcad.main/src/librt/prep.cpp:1118), [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213)

### PERF-008 Packet bridge

- Classification: Research / speculative
- Why it matters: Embree traverses packets efficiently, but the BRL-CAD bridge currently scalarizes packet lanes. Preserving packet structure deeper into BRL-CAD could reduce per-ray overhead if the BRL-CAD side can support it.
- What to change: First document the exact viability gap between current per-lane tracing and BRL-CAD bundle/vector capabilities. If a credible path exists, prototype either a limited bundle path for the intersection bridge or a safer extension of existing BRL-CAD bundle logic.
- Why it is low-hanging or not: Not low-hanging. The generic BRL-CAD vector path still stops at a missing `boolweave` integration point, which makes this a research task rather than an immediate optimization.
- Risks: High implementation complexity, correctness risk in boolean handling, and a real chance of spending time on a path that does not survive production constraints.
- Validation: Only proceed if the planned path reaches end-to-end shading with correct boolean results. Then compare packet coherence and total worker time on the same `moss.g` views.
- Citations: [brlcad.cpp:341](/Users/morrison/IBRT/plugins/brl_cad/geometry/brlcad.cpp:341), [vshoot.c:242](/Users/morrison/brlcad.main/src/librt/vshoot.c:242)

### PERF-009 SIMD bbox

- Classification: Research / speculative
- Why it matters: `rt_in_rpp()` is a real hotspot, so it is reasonable to ask whether a branch-reduced or SIMD implementation could help. However, its ceiling is bounded by how often the code is reached, which in turn depends on broader culling behavior.
- What to change: Measure the micro-level cost and branch behavior of `rt_in_rpp()`, then evaluate a minimal-complexity optimization path such as vector-friendly layout, branch reduction, or specialized packet helpers.
- Why it is low-hanging or not: Not especially low-hanging. Even a strong local speedup only attacks a slice of total worker time, and it does not fix the larger issue that too many rays reach BRL-CAD in the first place.
- Risks: Architecture-specific SIMD code increases maintenance cost and may not generalize across platforms or compilers.
- Validation: Use both microbenchmarks and end-to-end sampling. Do not keep the work unless it moves whole-frame performance in a meaningful way after higher-level culling decisions are understood.
- Citations: [bbox.c:163](/Users/morrison/brlcad.main/src/librt/bbox.c:163), [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md), [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213)

### PERF-010 Boolean routines

- Classification: Research / speculative
- Why it matters: `rt_boolfinal`, `rt_boolweave`, and `rt_arb_shot` are all visible hotspots. Once transport, dynamic resolution, watchdog behavior, and broader culling are improved, these routines may become better targets for focused optimization.
- What to change: Reprofile after earlier items land, then inspect routine-local costs, memory access patterns, and repeat work inside `rt_boolfinal`, `rt_boolweave`, and `rt_arb_shot`.
- Why it is low-hanging or not: Not low-hanging in the current bottleneck mix. Optimizing these now risks spending effort below more leverage-rich architectural and user-visible issues.
- Risks: These routines are core BRL-CAD query logic. Changes can be subtle and correctness-sensitive, especially around boolean edge cases.
- Validation: Reprofile to confirm they remain prominent after higher-priority work, then use scene coverage and regression rendering to ensure correctness.
- Citations: [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md), [IBRTRenderWorker.sample.txt:20213](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.sample.txt:20213)

## Experiment Log

| Date | Change | Scene/View | Result | Related IDs | Artifacts |
| --- | --- | --- | --- | --- | --- |
| 2026-05-14 | Baseline flame-graph capture with default `moss.g` startup scene | Default scene with representative camera views, including hard cameras `2` and `3` | UI mostly idle; worker dominated by `rtcIntersect8 -> brlcadIntersectN_C -> traceRay -> rt_shootray`; hard full-resolution views reached `25.91-72.46 ms` for camera 2 and roughly `344-991 ms` for camera 3 | PERF-001, PERF-002, PERF-003, PERF-004, PERF-006, PERF-007, PERF-008, PERF-009, PERF-010 | [report.md](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/report.md), [IBRTRenderWorker.svg](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRTRenderWorker.svg), [IBRT.svg](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/IBRT.svg), [launch.log](/Users/morrison/IBRT/.artifacts/perf/ibrt-moss-2026-05-14/launch.log) |

## Open Questions

- What worker/client message shape is best for metadata-only frame responses when `updated == false`, and can it be introduced without complicating existing message handling?
- Which automatic-mode policy produces the best balance between responsiveness and refinement on hard `moss.g` views: hold, backoff, or time-window averaging across several frames?
- Does the current OSPRay future model support safe early cancellation for the watchdog path, or will a responsive watchdog need cooperative preemption between frames instead?
- What partitioning granularity gives the best cost/benefit for BRL-CAD user geometry: region-level grouping, simple spatial cells, or another scheme tied to prepared bounds?
- Are the current bookkeeping hotspots dominated more by allocation/clearing cost or by list/table traversal, and which of those can be reduced without changing BRL-CAD semantics?
