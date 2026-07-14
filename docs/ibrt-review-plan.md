# IBRT Review & Follow-up Plan

_Generated 2026-07-14 from a build/test bring-up plus a multi-module code review of the
non-vendored IBRT sources (`apps/IBRT/**` excluding `imgui/` and `tiny_obj_loader.h`, and
`plugins/brl_cad/**`)._

## How this was produced

- Configured and built the tree with MSVC 14.51 (VS 18 Insiders) + Ninja against
  `BEXT_INSTALL_DIR=C:/devtools/bext/build/everything/install` and
  `BRLCAD_PREFIX=C:/devtools/scrs/brlcad.base/out/build/x64-Release`, `RelWithDebInfo`.
- Fixed one compilation blocker (see B-00) to get a clean build.
- Ran `ctest`: `IBRTTests` **passes**; `IBRTReferenceMossSciVis` **fails** only on the exact
  SHA-256 byte comparison — the produced image is **visually identical** to the reference
  (see T-01). Smoke-launched the GUI: OSPRay initializes, the BRL-CAD module loads, the
  demo scene renders, no crash.
- Reviewed each module for correctness bugs and improvement opportunities; the six items
  tagged **[VERIFIED]** were independently re-checked against the source and confirmed.
  One candidate (a supposed stale `tl_res` thread-local) was **rejected** after
  verification — see the note at the end.

Severity legend: **Critical** / **High** / **Medium** / **Low**. Item IDs are stable
handles for follow-up (`B` bug, `SEC` security, `C` concurrency, `P` perf, `R` refactor,
`T` test, `BLD` build/packaging, `F` feature).

---

## 0. Done in this pass

- **B-00 — [FIXED] `#warning` in BRL-CAD `common.h` broke the app build under MSVC.**
  `rkcommon/platform.h` (pulled in via `ospraybackend.h` → `ospray_cpp` → rkcommon) defines
  `UNUSED` before BRL-CAD's `common.h` was reached, so `common.h` hit its "UNUSED unexpectedly
  defined — include common.h first" `#warning`, which this MSVC rejects (C1188) unless
  `/std:c++23preview`. Honored BRL-CAD's contract by including `<brlcad/common.h>` first in
  `apps/IBRT/ospraybackend.cpp` (the only TU that pulls in both). Build is clean.
  _Follow-up:_ consider a tiny `ibrt_brlcad_prologue.h` if any future TU needs both, so the
  ordering rule lives in one documented place.

---

## 1. Correctness & security — High priority

- **SEC-01 — [VERIFIED][High] Unbounded IPC `payloadSize` → OOM / uncaught `bad_alloc` → `std::terminate`.**
  `worker_ipc.cpp:90-97` (Win) / `170-177` (POSIX): `readMessage` reads a 32-bit
  `header.payloadSize` and does `std::vector<char> buffer(header.payloadSize)` with no upper
  bound. A corrupt/hostile local peer can request ~4 GiB per message; the `bad_alloc` is
  uncaught in both `worker_main.cpp` loops and the client callers (`renderworkerclient.cpp:571/706`),
  so it propagates out of a `bool`-returning function and terminates the process.
  _Fix:_ define `kMaxPayloadSize` sized to the largest legitimate frame, reject oversized
  headers before allocating, and guard the allocation so a bad size closes the connection.

- **B-01 — [VERIFIED][High] Signed-int overflow in frame pixel-size validation.**
  `renderworkerclient.cpp:482-489`: `int pixelBytes = int(width) * int(height) * 4;` on
  worker-supplied `uint32` dimensions (only checked `!= 0`, never bounded). Overflow to a small
  positive value lets the `payload.size()` check pass while `QImage` is allocated for the true
  (huge) dimensions and `memcpy` under-copies → UB + silently corrupted frames.
  _Fix:_ use `size_t` arithmetic, bound `width/height` to a sane max, and verify
  `image.sizeInBytes() == pixelBytes` before the `memcpy`.

- **B-02 — [VERIFIED][High] Framebuffer `map()` result `memcpy`'d without a null check.**
  `ospraybackend.cpp:1488-1504` (`finishCompletedRender`): both the progressive and accum
  branches do `void *mapped = fb.map(OSP_FB_COLOR); std::memcpy(dst, mapped, …)` with no null
  guard. OSPRay's `map()` can return null (bad channel, device error, invalid/cancelled frame);
  a null source to `memcpy` is UB and crashes the viewer.
  _Fix:_ null-check `mapped` in both branches; on null, log and skip the copy (keep the previous
  display). Optionally assert the mapped size matches the destination.

- **B-03 — [VERIFIED][High] Handle closed from the UI thread while the poll thread is blocked reading it.**
  `renderwidget.cpp:2141-2143` (`restartWorkerAndReplayState`): when a request is in flight the
  UI thread calls `renderWorkerClient_->stop()` (which `closePipe()/closeSocket()` **without**
  taking `requestMutex_`) *before* `stopWorkerPolling()` joins the poll thread. The poll thread
  is blocked in `readMessage` on that same fd/HANDLE (holding `requestMutex_`). Closing an fd
  another thread is reading is a documented data race, and the fd can be immediately reused by
  the following `connectSocket()`.
  _Fix:_ join the poll thread (`stopWorkerPolling`) **before** `stop()` closes the transport, or
  serialize teardown under `requestMutex_`.

- **B-04 — [VERIFIED][High] Render watchdog is entirely dead code; runaway frames are never preempted.**
  `ospraybackend.cpp`: `advanceRender` discards `timeBudgetMs` (`(void)timeBudgetMs;`, line 280);
  `inFlightStart_` is recorded (1457/1470) but never compared to `watchdogTimeoutForCurrentMode()`;
  `watchdogCancelCount_` is reported over IPC but never incremented; `watchdogTriggered_` is only
  ever set false. A slow path-tracer frame blocks until OSPRay finishes on its own.
  _Fix:_ in the `!isReady` branch compare `steady_clock::now() - inFlightStart_` against the
  timeout; on exceed call `cancelInFlightFrame("watchdog")`, bump the counter, set the flag, apply
  AO backoff — or delete the dead watchdog fields/parameter if the feature is abandoned.

- **B-05 — [High] `requestId` mismatch leaves the byte stream desynchronized instead of resetting.**
  `renderworkerclient.cpp:578-581/713-716`: on a `requestId` mismatch the code sets `lastError_`
  and returns false but does **not** close the connection, unlike every other framing failure.
  A mismatch means framing is already out of sync, so all subsequent exchanges are corrupted.
  _Fix:_ treat it as fatal — `closePipe()/closeSocket()` + `setConnected(false)` so the worker
  restarts.

- **B-06 — [High] `IBRTReferenceRender` render params are non-deterministic (root of the test brittleness).**
  `reference_render.cpp:110-117`: 16 accumulated frames of stochastic scivis AO (aoSamples=1)
  with no fixed seed / thread-count pinning. Output depends on ISPC target + TBB scheduling, so
  it is not bit-reproducible off the exact baseline machine. This is the underlying cause behind
  T-01. _Fix:_ render to a high, converged frame count (and/or a deterministic sampling path) and
  compare with tolerance (T-01).

---

## 2. Correctness — Medium priority

- **B-07 — [Medium] `loadObj` face indices not range-checked against the vertex array.**
  `ospraybackend.cpp:518-534`: only negative indices are filtered; an index `>= vertices.size()`
  is pushed straight into the OSPRay index buffer → out-of-bounds read during BVH build/shading.
  _Fix:_ skip/reject triangles whose max index `>= vertices.size()`; report dropped faces via
  `setError`.

- **B-08 — [Medium] BRL-CAD solid-load bounds fallback cannot tell "no bounds" from "tiny scene".**
  `ospraybackend.cpp:659-711`: bounds seed to `[-1,1]^3` and are only overwritten when
  `ospGetBounds` returns finite values — but the default `±1` also passes `isfinite`. Degenerate
  plugin bounds silently leave a 2-unit cube, breaking camera framing.
  _Fix:_ require a non-degenerate box (upper > lower on ≥1 axis) before accepting; surface a
  warning on fallback so camera-fit can react.

- **B-09 — [Medium] Loaders report success on lossy/empty imports.**
  `ospraybackend.cpp` `loadBrlcad` (bounds fallback) and `loadObj` (silent non-triangle / bad-index
  skipping) return `true` even when nothing usable loaded. _Fix:_ treat "zero triangles" /
  "no finite bounds" as a soft failure surfaced in the UI; count and report skipped faces.

- **B-10 — [Medium] `main()` builds `QApplication` with stale `argc` after `ospInit` compacted `argv`.**
  `main.cpp:238-278`: `ospInit(&ac, av)` removes consumed args in place and decrements `ac`, but
  `QApplication a(argc, argv)` uses the **original** `argc`. With any `--osp:*` arg present Qt
  over-counts and reads stale/garbage trailing pointers.
  _Fix:_ `QApplication a(ac, const_cast<char**>(av));`.

- **B-11 — [Medium] OSPRay device never released; `ospShutdown()` runs after Qt teardown (likely the "crash on exit").**
  `main.cpp:248-282`: the CPU device is never released, and `ospShutdown()` is called only after
  the `QApplication` scope ends — so `OsprayBackend`/`RenderWidget` destructors release OSPRay
  handles while the GL context/event loop are being torn down. Matches TODO "investigate crash on
  exits". _Fix:_ deterministic order — destroy the window and all OSPRay-owning objects, then
  `ospRelease(device)`, then `ospShutdown()`; ensure the worker is joined/terminated first.

- **B-12 — [Medium] BRL-CAD module loads twice at startup (redundant load path).**
  `main.cpp:223` preloads via `OSPRAY_LOAD_MODULES` before `ospInit`; `ospraybackend.cpp:89`
  (`ensureBrlcadModuleLoaded`) calls `ospLoadModule("brl_cad")` again → the "Initializing BRL-CAD
  module" line is printed twice (confirmed in the smoke run). _Fix:_ pick one mechanism; make
  `ensureBrlcadModuleLoaded` a no-op when already present, and make `moduleInit`'s
  `registerType`/logging idempotent. (Distinct from the TODO's "reload during view changes".)

- **B-13 — [Medium] HiDPI: render target sized in logical px, viewport in physical px → blurry render.**
  `renderwidget.cpp`: `resizeGL(w,h)` passes logical pixels to `backend_.resize` /
  `queueWorkerResize`, but `paintGL` sets `glViewport(... * devicePixelRatioF())` and blits into
  the logical `rect()`, so the ray-traced frame is produced at 1/dpr and upscaled on any
  dpr>1 display. _Fix:_ size the render target in physical pixels (`w*dpr, h*dpr`), keep ImGui
  `DisplaySize` logical; re-verify readback + vertical flip.

- **B-14 — [Medium] `resizeGL` calls `resetView()` on every resize, discarding the user's camera.**
  `renderwidget.cpp:384`: every window resize/maximize/splitter move reframes to defaults and
  forces Orbit mode. Same class as the TODO "full-screen toggle resets the view".
  _Fix:_ on resize only update aspect + target size and re-sync the existing camera; auto-frame
  only on first sizing / when no scene camera is established.

- **B-15 — [Medium] Crash-dump filter writes next to the exe (often non-writable) and uses CRT in a corrupt state.**
  `main.cpp:110-161`: dumps go to `GetModuleFileNameA` dir (unwritable under Program Files → no
  dump, silently), the filter uses CRT/heap functions, doesn't chain the previous filter, and
  doesn't check path truncation. _Fix:_ write to `%LOCALAPPDATA%/IBRT/crashes` (fallback exe dir),
  minimize CRT use, preserve/chain the prior filter, surface the dump path.

- **B-16 — [Medium] BRL-CAD one-time init (`bu_setprogname`, etc.) never called in the plugin.**
  `plugins/brl_cad/geometry/brlcad.cpp:465-478`: `rt_dirbuild`/`rt_gettrees` run without any
  libbu/librt one-time init because the module loads into a non-BRL-CAD host. Degrades error
  reporting and temp/registry lookups. _Fix:_ do the once-only init in
  `ospray_module_init_brl_cad` (`moduleInit.cpp`), guarded to run exactly once.

- **B-17 — [Medium] `commit()` leaves the plugin geometry half-initialized on mid-commit failure.**
  `brlcad.cpp:465-491`: throwing after `rt_dirbuild` (e.g. a later `rt_gettrees` failure) leaves
  `rtip` set with `resources` empty; `numPrimitives()` still returns 1, so intersection runs with
  no primary resources. _Fix:_ on any post-`rt_dirbuild` failure, free `rtip`, null it, clear
  resources/overflow before rethrowing so the geometry is inert.

- **B-18 — [Medium] `RenderSettingsState` vs backend default disagree for `customLowQualityWhileInteracting`.**
  `renderworkerclient.h:62` (`false`) vs `ospraybackend.h:272` (`true`): the worker and in-process
  paths start with opposite interactive-quality behavior, and "Reset Custom Settings" applies
  whichever default matches the active path. _Fix:_ one authoritative default (ideally seed the
  backend from `RenderSettingsState`); add a test asserting the defaults match field-by-field.

- **B-19 — [Medium] Quality setters silently drop updates while a frame is in flight; inconsistent with siblings.**
  `ospraybackend.cpp:778-839`: `setAoDistance/setPixelSamples/setMaxPathLength/setRoulettePathLength`
  store an error and return without applying/queuing when `frameInFlight_`, so a slider drag during
  continuous rendering is lost with no UI feedback — while `setAoSamples` (767) has no such guard.
  _Fix:_ route all sampling setters through the `applyPendingState` buffer used by camera/renderer;
  don't use `lastError_` for benign "try again" conditions. (See also C-06.)

---

## 3. Correctness — Low priority / robustness

- **B-20 — [Low] Plugin `hitCallback` returns "hit" (1) even for an empty partition list.**
  `brlcad.cpp:247-277`: the fall-through `return 1;` reports a hit without writing `tfar`/`Ng`/
  `geomID`; correctness relies on the pre-`shootray` `RTC_INVALID_GEOMETRY_ID` reset rather than the
  callback's own logic. _Fix:_ `return 0` when no partition was consumed; only `return 1` from inside
  the loop after writing hit data.

- **B-21 — [Low] `db_ls` result indexed without a `dpv != nullptr` guard when `count > 0`.**
  `ospraybackend.cpp:1667-1673` / `1882-2005`: loops index `dpv[i]` relying on the `count>0 ⇒ dpv
  allocated` invariant without asserting it, despite the `dpv=nullptr` init pattern.
  _Fix:_ guard the loops with `if (dpv)` / `count > 0 && dpv`.

- **B-22 — [Low] Plugin ignores `ray.mask`; BRL-CAD occlusion/visibility masking not honored.**
  `brlcad.cpp:360-390`: the occlusion branch rebuilds the ray with `flags=0`, `id=0` and `traceRay`
  never applies `ray.mask` to the BRL-CAD trace, so mask-based light visibility is wrong for BRL-CAD
  geometry. _Fix:_ apply `ray.mask` before `rt_shootray`, or document the limitation.

- **B-23 — [Low] Packed region color rides `hit.u/hit.v`; silently coupled to barycentric semantics.**
  `brlcad.cpp:157-178` + `brlcad.ispc:11-21`: color survives only because Embree currently doesn't
  perturb `u/v` for this geometry and values ≤65535 are exact in f32. Any future filtering/
  interpolation/motion-blur would corrupt color. _Fix:_ assert the exact-integer-carrier invariant,
  or carry color in a dedicated per-hit channel to decouple from `u/v`.

- **B-24 — [Low] `LoadBrlcad`/`ListBrlcadObjects` payloads are newline-delimited and unvalidated.**
  `worker_main.cpp:197-218` (and dup 492-513): `path\nobject\nmode` split by `find('\n')` mis-parses
  any path/name containing a newline; no length/content validation before opening files.
  _Fix:_ length-prefixed field encoding; validate/normalize the path server-side.

- **B-25 — [Low] Wire protocol assumes matching struct padding/endianness with no guard.**
  `worker_ipc.h:36-43` + the POD payloads: raw `memcpy` of C structs with no `#pragma pack`, no
  `static_assert(sizeof(...))`, no byte-order normalization. Works only because both binaries share a
  toolchain/arch. _Fix:_ `static_assert` sizes, pack (or reorder to avoid padding), and/or use
  explicit little-endian read/write helpers. (Pairs with R-03.)

- **B-26 — [Low] Startup demo scheduled via `singleShot(0)` can race a user-initiated load.**
  `mainwindow.cpp:249/456-487`: the demo guard checks synchronous state that may not yet reflect an
  in-flight async load. _Fix:_ set a one-shot "initial load requested" flag the moment any load
  starts; bail the demo if set (or trigger from `showEvent` once).

- **B-27 — [Low] Worker reconnect is a fixed 500 ms retry with no backoff or cap.**
  `mainwindow.cpp:219-233`: a repeatedly-failing worker becomes a spawn loop, each attempt re-running
  OSPRay init. _Fix:_ exponential backoff + max attempts; after N failures stop, surface a persistent
  message, fall back to local rendering; ignore further disconnects while a restart is pending.

- **B-28 — [Low] `stop()` hard-kills the worker; no graceful `Shutdown`, no parent-death linkage.**
  `renderworkerclient.cpp:170-186`: always `process_->kill()`, never sends the defined
  `MessageType::Shutdown`; skips `ospShutdown` and (POSIX) leaks `/tmp/ibrt_render_<pid>.sock`; a
  parent crash orphans the worker. _Fix:_ send `Shutdown` + `waitForFinished` first, kill on timeout;
  Linux `PR_SET_PDEATHSIG`, Windows Job Object `KILL_ON_JOB_CLOSE`; unlink the socket on all exit paths.

- **B-29 — [Low] `focusOutEvent` re-implements `finishInteraction`; can strand `interacting=true`.**
  `renderwidget.cpp:1759-1777` vs `463-472`: duplicated teardown that, if focus is lost during a
  load, forces `interactionActive_=false` so a later `finishInteraction` early-returns and never
  clears the worker's interacting flag. _Fix:_ call `finishInteraction()`; reconcile the flag after a
  load completes regardless of focus.

- **B-30 — [Low] ImGui `io.DeltaTime` hardcoded to 1/60 regardless of real timing.**
  `renderwidget.cpp:611`: breaks the FPS readout and any time-based ImGui behavior (key repeat,
  tooltips). _Fix:_ measure elapsed seconds via `steady_clock` between `paintGL` calls (clamped).

- **B-31 — [Low] OpenGL state not bracketed around QPainter/ImGui in `paintGL`.**
  `renderwidget.cpp:583-629`: raw GL, then QPainter (mutates+doesn't restore GL state), then a second
  GL renderer, with no `beginNativePainting()/endNativePainting()`. Fragile across drivers.
  _Fix:_ bracket raw GL with native-painting calls or reset blend/scissor before ImGui.

- **B-32 — [Low] Stderr filter thread + pipe leaked for process lifetime; late-shutdown errors can be lost.**
  `main.cpp:34-108`: stderr is redirected through a detached, never-joined thread and never restored;
  errors emitted during teardown (relevant to the exit-crash) may not flush. _Fix:_ keep a restore
  handle and drain on shutdown; temporarily disable the filter when diagnosing B-11.

- **B-33 — [Low] Pre-Qt `MessageBoxA` on startup failure can hang a headless/worker-like launch.**
  `main.cpp:205-212/243-265`. _Fix:_ gate the modal behind an interactivity/`--no-gui` check; always
  keep the stderr path; add `MB_SETFOREGROUND|MB_TOPMOST`.

---

## 4. Concurrency & threading

- **C-01 — [Medium] `renderWorkerClient_` read/written across threads without synchronization.**
  `renderwidget.cpp`: `setRenderWorkerClient()` (main thread) writes the raw pointer while
  `workerPollingLoop()` (started in the ctor) dereferences it continuously. _Fix:_ make it
  `std::atomic<RenderWorkerClient*>` (or guard with `workerStateMutex_`), or start the poll thread
  only after assignment. Document that the client must outlive the widget.

- **C-02 — [Medium] `cameraVersion_` is tracked per request but never used to drop stale frames.**
  `ospraybackend.cpp`: `finishCompletedRender` never compares `activeRenderRequest_->cameraVersion`
  to `cameraVersion_`, so a frame rendered against a superseded (buffered) camera is still displayed
  and counted as accumulated. _Fix:_ discard the frame (skip the display copy + accum increment) on
  version mismatch and let `applyPendingState` re-render. This is exactly what the plumbing was for.

- **C-03 — [Medium] `connected_` / `lastError_` / `stopInProgress_` shared across threads unsynchronized.**
  `renderworkerclient.h:120-123`: plain `bool`/`QString` written on the poll thread and read on the
  UI thread; `lastError_` (a `QString`) can tear → use-after-free of shared Qt string data.
  _Fix:_ `std::atomic<bool>` for the flags; guard `lastError_` with a mutex or return a copy taken
  under `requestMutex_`.

- **C-04 — [Medium] `restartWorkerAndReplayState` does synchronous scene reload on the UI thread and ignores in-flight loads.**
  `renderwidget.cpp:2136-2157`: `replayWorkerState()` runs `loadObj/loadBrlcad` IPC synchronously on
  the UI thread (freezes the event loop for seconds) and there's no `sceneLoadThread_` guard.
  _Fix:_ defer when `sceneLoadInProgress_`; move replay onto `startAsyncLoad`. (Interacts with B-03.)

- **C-05 — [Medium] Blocking IPC round-trip with no timeout can hang the poll thread forever.**
  `renderworkerclient.cpp:550-576/686-711` + `readAll`: if the worker stalls mid-message the poll
  thread blocks indefinitely holding `requestMutex_`, and recovery relies on the unsafe cross-thread
  close (B-03). _Fix:_ overlapped I/O + `WaitForSingleObject` timeout (Win), `poll()`/`SO_RCVTIMEO`
  (POSIX) so `readMessage` can time out and the client can restart cleanly.

- **C-06 — [Low] `setAoSamples` bypasses the `frameInFlight_` deferral its siblings enforce.**
  `ospraybackend.cpp:767-775`: mutates `customAoSamples_` immediately (only the accumulation reset is
  deferred), so the next `advanceRender` reads the new value mid-flight — inconsistent with B-19.
  _Fix:_ apply one uniform policy across all sampling setters.

- **C-07 — [Low] `workerRequestStart_` (`time_point`) written on poll thread, read on UI thread unsynchronized.**
  `renderwidget.cpp:1960` vs `workerBusySeconds()`; not covered by `workerStateMutex_`.
  _Fix:_ store as `std::atomic<int64_t>` ticks, or guard with the mutex with acquire/release ordering.

- **C-08 — [Low] Async scene-load lambda captures `this` and re-enters via `invokeMethod` without a lifetime guard.**
  `renderwidget.cpp:1879-1893` + load lambdas: the dtor joins the thread, but an already-queued
  completion slot can target a partially-destroyed widget. _Fix:_ `QPointer<RenderWidget>` guard in
  posted lambdas, or drain/disconnect queued invocations in the dtor before tearing down GL/ImGui.

- **C-09 — [Low] Plugin resource selection bypasses the pre-sized primary array (see P-02); document librt single-thread contract.**
  librt global state (`rt_dirbuild`/`rt_free_rti`) is entered from multiple const backend methods on
  the UI thread without a mutex (`ospraybackend.cpp:1656-1705/1858-1880`). _Fix:_ serialize all librt
  entry points behind one backend-owned mutex (or cache a single `rt_i` per open DB); at minimum
  document/enforce single-threaded librt access.

- **C-10 — [Low] Scene-bounds fields written on load paths, read on UI thread; invariant undocumented.**
  `renderwidget.cpp`: `sceneBoundsMin_/Max_` are UI-thread-only in practice, but `replayWorkerState`
  writes them synchronously. _Fix:_ document UI-thread-only; if replay moves off-thread (C-04),
  marshal bounds back via a queued call like the async path.

---

## 5. Performance

- **P-01 — [Medium] BRL-CAD database is `rt_dirbuild`'d two–three times per load.**
  `ospraybackend.cpp:596-654`: `fopen/fclose` probe, then `listBrlcadObjects(path)` does a full
  `rt_dirbuild`+`db_ls`+`rt_free_rti` just to validate the object name, then the plugin re-parses the
  `.g` on commit. Matches TODO "only dirbuild/prep once". _Fix:_ one `rt_dirbuild`, reuse `rti_dbip`
  for the existence check via `db_lookup(LOOKUP_QUIET)` instead of enumerating all tops.

- **P-02 — [Medium] Global monotonic `getCpuId()` pushes most worker threads onto the overflow path.**
  `brlcad.cpp:32-37/125-142`: cpu ids grow process-globally and are never reset, so after the first
  load most live threads have `cpuId >= resources.size()` and hit `ensureOverflowResource()` (atomic
  CAS + heap alloc) on the hot intersection path, largely bypassing the pre-sized `resources` array.
  _Fix:_ key selection on a per-instance dense worker index (thread-local slot claimed from an
  instance-scoped counter reset each commit) so overflow is a true rare fallback.

- **P-03 — [Medium] Worker `RequestFrame` blocks the single-threaded IPC loop, defeating cancellation.**
  `ospraybackend.cpp:1481` + `worker_main.cpp` loop: the worker services one message at a time and
  `finishCompletedRender` does `currentFrame_.wait(OSP_FRAME_FINISHED)`, so a `SetCamera`/`SetInteracting`
  can't be processed until the frame completes — preemption degrades to killing the whole worker.
  _Fix:_ run OSPRay rendering on a dedicated worker thread and keep the IPC read loop responsive so an
  incoming control message can call `cancelRender()`.

- **P-04 — [Low] `PendingCommands.settings` defaults to `true`, forcing a redundant `SetRenderSettings` per drain/restart.**
  `renderworkerqueuelogic.h:34`. _Fix:_ default it `false` like the other flags and explicitly queue
  one settings send after connect/restart.

- **P-05 — [Low] Extra refcount churn: `models.push_back(model)` copies cpp wrappers that could be moved.**
  `ospraybackend.cpp:670-671/1817-1844`. _Fix:_ `push_back(std::move(model))`/`emplace`.

- **P-06 — [Low] `renderBudgetMs_` is adapted in the worker path where it has no effect (and corrupts a later local switch).**
  `renderwidget.cpp:2073-2076`: `advanceRender` is never called in worker mode, so the adjustment is
  dead computation. _Fix:_ gate behind `!usingWorkerRenderPath()`, or feed the worker's target frame
  time instead.

---

## 6. Refactoring & maintainability

- **R-01 — [High] Remove/gate the debug `fprintf` scaffolding (and the emoji crash marker) in `loadBrlcad`.**
  `ospraybackend.cpp:625-716`: ~20+ unconditional `fprintf(stderr, "STEP 1..17"/handle pointers)` on
  every load, plus `geom.commit(); // 🔥 VERY LIKELY CRASH POINT`. The codebase already has the right
  idiom (`verboseRenderLoggingEnabled()`, `kVerboseBRLCADLogging`). _Fix:_ delete or route the few
  useful lines through the verbose gate; remove the crash-point comment. (TODO "clean up logging".)

- **R-02 — [High] Deduplicate the ~220-line Windows/POSIX worker dispatch loop.**
  `worker_main.cpp:163-391` vs `458-686`: the entire `switch(message.type)` (13 handlers + payload
  struct bodies) is copy-pasted, differing only in the transport variable. _Fix:_ a `Transport`
  abstraction over `fd`/`HANDLE` + a single `runWorkerLoop(Transport&, OsprayBackend&)`; keep only
  accept/connect in the `#if` branches.

- **R-03 — [High][partly VERIFIED] Hoist the IPC wire structs into one shared header with size asserts.**
  `worker_ipc.h` currently defines only `MessageType`/`MessageHeader`/`Message`; `LoadResultPayload`,
  `FrameHeader` (×4), `SettingsPayload` (×2), `CameraPayload`, `ResizePayload` are re-declared as
  function-local PODs in `worker_main.cpp`/`renderworkerclient.cpp` (~13 definitions) and `memcpy`'d
  positionally — any field drift is silent memory corruption, not a compile error. _Fix:_ one
  authoritative definition per payload in `worker_ipc.h` + `static_assert` on each `sizeof`. (Pairs
  with B-25.)

- **R-04 — [High] Deduplicate `RenderWorkerClient::sendRequestBytes/sendRequest/sendPing` across platforms.**
  `renderworkerclient.cpp:540-629` vs `676-764`, including the ~40-line response-type validation
  cascade. _Fix:_ extract the transport-independent body behind an abstract handle + close callback
  (or an `isConnected()/closeTransport()` helper) so one implementation serves both.

- **R-05 — [Medium] Decompose the ~580-line `paintGL`.**
  `renderwidget.cpp:581-1159`: mixes GL clear/viewport, QPainter blit + version-specific flip, ImGui
  IO, overlays, stats, selectors, and the whole settings panel. _Fix:_ `blitFrame`, `beginImGuiFrame`,
  `drawStatsPanel`, `drawVisualizationControls`, `drawRendererControls`, `drawRenderSettingsPanel`.

- **R-06 — [Medium] Collapse the per-widget worker-vs-in-process branching in the settings panel.**
  `renderwidget.cpp:785-1060`: ~14 widgets each triplicate read/two-write branches. _Fix:_ drive the
  UI from a single `RenderSettingsState`, then apply it to either the worker or the backend via one
  `applyState(backend, state)` helper after the frame.

- **R-07 — [Medium] Define the Fast/Balanced/Quality preset ladder once.**
  Triplicated in `qualitysettings.cpp` (two near-duplicate seed functions) and
  `ospraybackend.cpp:1129-1211`. _Fix:_ one `constexpr` table `{startScale, aoSamples, pixelSamples}`
  indexed by preset; collapse the two seed functions into one.

- **R-08 — [Medium] Remove the dead object-manipulation API surface (or finish it).**
  `renderwidget.h:43-56/79-81/169`: `ManipulationTarget`, `applyObjectAction`, `setObjectTransform`,
  `objectTransform` are declared but never defined/used — any call fails to link. _Fix:_ delete the
  declarations + enum, or wire `manipulationTarget_` in `mouseMoveEvent` and implement the methods.

- **R-09 — [Low] Remove the mutable-reference accessor `getAoSamples()`.**
  `ospraybackend.h:131` / `.cpp:1620-1623`: returns `int&` to `customAoSamples_`, bypassing clamping
  + `resetAccumulation()`; currently dead. _Fix:_ delete it; use the existing by-value
  `customAoSamples()`.

- **R-10 — [Low] Collapse `listBrlcadHierarchy` alias + share the `rt_dirbuild`+cleanup boilerplate.**
  `ospraybackend.cpp:1656-1682/1858-2028`: `listBrlcadHierarchy` just forwards to `getBrlcadHierarchy`
  (dead public API); both re-implement open/enumerate/cleanup. Also drop the no-op
  `cleanup.dpv = dpv;` at 1883. _Fix:_ one helper reused by both enumeration functions.

- **R-11 — [Low] Remove the unused `application ap` member and duplicated intersect-pointer wiring in the plugin.**
  `brlcad.h:52` (`application ap` never used; `traceRay` builds a local one), and the identical
  `getSh()->super.*` function-pointer setup in both the ctor (413-418) and `commit()` (440-446) — a
  drift hazard for the ABI-sensitive wiring in the regression doc. _Fix:_ delete the member; factor
  the wiring into one helper called from `commit()`.

- **R-12 — [Low] Replace scattered magic literals with named constants.**
  IPC magic `0x54425249` (×3), watchdog `1500` duplicated in `renderworkerclient.h:64` vs
  `ospraybackend.h` `kDefaultWatchdogMs`, and low-precision `3.14159265f` (× many) plus the mystery
  `1.77079633f` orbit-phi seed. _Fix:_ `kIpcMagic`, derive the watchdog default from one source, a
  single `kPi` (or `std::numbers::pi`), and document the initial view angle.

- **R-13 — [Low] `setupMenus`: add standard shortcuts and drop redundant up-axis toggling.**
  `mainwindow.cpp:253-388`: no `QKeySequence` shortcuts; the yUp/zUp handlers manually mirror
  `setChecked` even though both are in an exclusive `QActionGroup`. _Fix:_ add
  `QKeySequence::Open/Quit/...`; rely on the action group; split into per-menu helpers.

- **R-14 — [Low] Remove the duplicate `setFocusPolicy(Qt::StrongFocus)` call.**
  `renderwidget.cpp:28` and `44`.

---

## 7. Tests

- **T-01 — [High] Replace the exact SHA-256 PNG comparison with a tolerance/perceptual diff.**
  `cmake/IBRTVerifyReferenceRender.cmake:23-31`: OSPRay/ISPC output is not bit-reproducible across
  SIMD targets, so `IBRTReferenceMossSciVis` is effectively a toolchain-pin, not a correctness test —
  it fails on this AVX-512 build despite a **visually identical** image. _Fix:_ add a small QImage-based
  comparator (mean/max per-channel AE, % pixels over threshold), fail only past configurable
  `-D` bounds; keep the SHA in the README as provenance. (See B-06 for the determinism root cause.)

- **T-02 — [High] Integration/system tests use bare `return;` (a PASS) when fixtures are missing.**
  `tests_ibrt.cpp` (e.g. 561-562, 585-586, 634-636, 799-800, 823-824, …): a missing BRL-CAD fixture
  makes many tests report green while asserting nothing (worker tests already use `QSKIP`, so the
  pattern is known but unevenly applied). _Fix:_ use `QSKIP` for preconditions and `QVERIFY2(..., lastError)`
  for mid-test guards after a successful fixture; wire `SKIP_RETURN_CODE`/`SKIP_REGULAR_EXPRESSION`.

- **T-03 — [Medium] Remove the personal-workstation absolute path from the fixture generator.**
  `tests_ibrt.cpp:114`: hardcoded `C:/brlcad-build/bin/wdb_example.exe`. _Fix:_ locate `wdb_example`
  under `BRLCAD_INSTALL_PREFIX/bin` (like the mged fallback) or via an env override
  (`IBRT_WDB_EXAMPLE`); pass `BRLCAD_PREFIX/bin` through CMake.

- **T-04 — [Medium] `IBRTTests` has no CTest `TIMEOUT` and there are no `LABELS` anywhere.**
  `apps/IBRT/CMakeLists.txt:336`: a hung worker/render can hang the suite forever; no way to run only
  the fast unit subset. _Fix:_ add a bounded `TIMEOUT`; split into labelled `add_test` entries
  (unit/integration/system) so `ctest -L unit` runs without a BRL-CAD install.

- **T-05 — [Medium] Add a headless plugin intersection test.**
  `plugins/brl_cad/CMakeLists.txt` has no test target; the ABI/packed-color/hit-callback paths are
  only exercised by the whole-app reference image. _Fix:_ a CTest that commits a `brlcad` geometry on
  a known `.g` and asserts `tfar`/`Ng`/unpacked region color for a few fixed rays — catches ABI/packing
  regressions far faster than a full render.

- **T-06 — [Medium] Cover the backend load/bounds/pixel-copy paths.**
  No tests around `loadBrlcad` bounds fallback (B-08), OBJ index validation (B-07), or
  `finishCompletedRender`'s null-map/pixel copy (B-02). _Fix:_ add targeted unit tests for each.

- **T-07 — [Low] Fill the two empty placeholder test slots (they currently count as PASSes).**
  `tests_ibrt.cpp:550-555` (`unitBackendLoadObjParsesSimpleTriangle`) and `1309-1315`
  (`unitWorkerIpcRoundTripMessage`). _Fix:_ refactor `ibrt::ipc` to (de)serialize into a
  `QByteArray` and round-trip in memory (no pipe); a `QTemporaryDir` `.obj` fixture for OBJ; else
  `QSKIP`.

- **T-08 — [Low] Add IPC framing-robustness tests (bad magic/version, oversized/truncated, requestId desync).**
  Use a `socketpair`/anonymous pipe. Directly exercises SEC-01, B-01, B-05.

- **T-09 — [Low] Make `systemWorkerCrashRecovery` portable and actually assert the kill.**
  `tests_ibrt.cpp:1121-1127`: hardcoded PowerShell path; on non-Windows it silently does nothing yet
  can pass. _Fix:_ kill via a test hook on the managed `QProcess` (`QProcess::kill`); assert the
  process actually terminated before asserting recovery.

- **T-10 — [Low] Add round-trip tests for the pure camera math.**
  `renderwidget.cpp:2109-2257` (orbit↔fly, angles↔forward, eye↔orbit) — up-axis-dependent and the
  exact code the Q4/Y-flip fixes kept touching. _Fix:_ extract the pure helpers (up-axis a
  parameter) and add inversion/round-trip tests.

---

## 8. Build system & packaging

- **BLD-01 — [Medium] `cmake --install` produces a non-runnable tree.**
  `apps/IBRT/CMakeLists.txt`: OSPRay/Qt/BRL-CAD runtimes and the `brl_cad` plugin are only staged
  next to build-tree binaries via POST_BUILD copies, never installed. _Fix:_ add `install()` /
  `install(CODE …)` (windeployqt / `file(GET_RUNTIME_DEPENDENCIES)`) so an install is self-contained,
  or document that only the build tree is runnable.

- **BLD-02 — [Medium] No Windows build wrapper / no MSVC preset, despite a Windows-first codebase.**
  `build.sh` is bash-only; `CMakePresets.json` has only Ninja presets. _Fix:_ add a `build.ps1`/`build.cmd`
  and/or MSVC configure/build/test presets; have `build.sh` delegate to `cmake --preset` to stay in
  sync. _(This session used a local vcvars64 + Ninja wrapper under `build/` — a committed equivalent
  would save the next person the same discovery.)_

- **BLD-03 — [Low] Reference-render CTest silently vanishes when `moss.g` is absent at configure time.**
  `apps/IBRT/CMakeLists.txt:292-304`: gated by `if(EXISTS ...)`, so a green run can hide a missing
  reference test; adding `moss.g` later needs a reconfigure. _Fix:_ always register and emit a CTest
  SKIP at run time, or at least `message(STATUS ...)` when disabled.

- **BLD-04 — [Low] `axis.g` is deployed but never listed in the Demo Models menu.**
  `mainwindow.cpp:403-406` vs `apps/IBRT/CMakeLists.txt:160`. _Fix:_ add `{"axis.g","Axis"}` or
  enumerate `*.g` in `demoModelsDir()` dynamically so the menu and deployment can't drift.

- **BLD-05 — [Low] `windeployqt` discovery is duplicated and a missing tool aborts the whole configure.**
  `apps/IBRT/CMakeLists.txt:254-267` and `324-334` (FATAL_ERROR at 259). _Fix:_ one shared helper;
  downgrade "missing windeployqt" to a warning that disables the deploy step for non-packaging builds.

- **BLD-06 — [Low] Guard the plugin's ISPC-target/OSPRay-ISA invariant at run time.**
  `IBRTDependencies.cmake:67-72` keeps `ISPC_TARGET_CPU == OSPRAY_ISPC_TARGET_LIST`, but nothing checks
  at startup that the plugin's linked intersect export matches the ISA OSPRay dispatches to — the exact
  mismatch class from `render-regression-investigation.md`. _Fix:_ document the invariant at the
  `*_addr()` call sites and add a build-time/startup assertion.

---

## 9. Feature backlog (from `TODO` + perf plan)

- **F-01 — Headless / non-GUI CLI render mode + rt-compatible CLI.** `reference_render.cpp` already
  proves `OsprayBackend` runs without Qt — a natural seed. (TODO 34-36)
- **F-02 — "Prep once unless hard-reset"** so dirbuild/prep isn't repeated on view changes (directly
  addresses P-01 and TODO 3/23).
- **F-03 — Lighting / brightness controls.** (TODO 38)
- **F-04 — Orthographic / orthogonal views.** (TODO 40)
- **F-05 — Debug overlay console.** (TODO 42)
- **F-06 — BRL-CAD branding in a corner + hotkey to hide the coordinate system.** (TODO 44/46)
- **F-07 — Progressive ladder: add a 32× rung and real dynamic-level adaptation.**
  `ospraybackend.h` `kProgressiveScales` is a fixed `{16,8,4,2,1}` and `beginNextProgressivePass`
  advances exactly one rung regardless of measured frame time; `targetFrameTimeForCurrentMode()`,
  `slowPassStreak_`, `progressiveFramesAtCurrentScale_` are maintained but never drive scale choice.
  (TODO 13/17)
- **F-08 — Edge renderer, cut planes, components-on-shotline in OSPRay.** (TODO 48-52)
- **F-09 — Broaden loading: STL and libgcv (beyond OBJ/BRL-CAD); colors on OBJ load for scivis/pt.**
  (TODO 60-64)
- **F-10 — rtwizard-style rendering; appleseed renderer integration; analytic integration plugin;
  metadata import for visualization; ability to run ged commands.** (TODO 54-70)
- **F-11 — Persist session preferences (quality/preset, up-axis, input mode) via QSettings.**
  Org/App names are already set (`main.cpp:272-274`) but nothing is persisted, so everything resets
  each launch.
- **F-12 — Tackle `docs/perf/ibrt-moss-performance-plan.md`.** (TODO 32)

---

## Rejected during verification (kept for the record)

- **Stale `tl_res` thread-local into a freed `resources` vector** (`brlcad.cpp:125-142`): **not a real
  defect.** `g_nextBrlcadInstanceId` is a monotonic, `fetch_add`-only 64-bit counter that is never
  reset or reused, so the cache-key comparison at line 130 can only short-circuit for the exact same
  live object at the exact same commit generation; every other case forces re-selection before
  `tl_res` is dereferenced. The proposed "add the owning pointer to the key" is optional defensive
  hardening (low priority), not a fix.
