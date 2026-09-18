# Cell-Plot Secondary Visualization Plugin Prototype

**Status:** Implemented prototype.

This is the first executable slice of Phase 1 in
[`color-plugin-integration-plan.md`](color-plugin-integration-plan.md). It proves the host/plugin,
camera-ray, per-cell evaluation, and overlay path before the production `RayShader` backend is
available.

## Scope

- Keep the existing OSPRay render and worker paths unchanged.
- Load a secondary visualization implementation at runtime through Qt's plugin system.
- For a BRL-CAD scene, trace one first-hit ray through the center of every cell in a fixed
  100 x 100 view-aligned grid.
- Assign each BRL-CAD region id a deterministic hashed color when the plugin prepares the scene.
- Return a transparent RGBA grid for IBRT to scale over the current rendered frame.
- Cancel stale work and recompute after camera, projection, viewport, scene, or object changes.

The prototype deliberately does not implement physical ray-shading values, the production bundle
format, stochastic accumulation, the hidden-line edge pass, or the T2/T3 modes.

## Modules and ownership

| Module | Responsibility |
|---|---|
| `include/ibrt/cellplotplugininterface.h` | Versioned Qt host/plugin contract and fixed 100 x 100 default. |
| `include/ibrt/cellplotmath.h` | Dependency-free perspective/orthographic cell-ray construction and deterministic region colors. |
| `plugins/cell_plot/` | Runtime plugin; owns a prototype-only BRL-CAD ray database, first-hit tracing, and region-color table. |
| `apps/IBRT/cellplotpluginloader.*` | Shared plugin discovery and lifetime management for GUI and headless hosts. |
| `apps/IBRT/cellplotoverlaycontroller.*` | Plugin discovery, 75 ms view debounce, background evaluation, cancellation, stale-result rejection, and latest image/stats. |
| `RenderWidget` | Submits immutable camera snapshots, exposes the opt-in ImGui checkbox, and composites the latest grid with `QPainter`. |
| `IBRTOfflineRender` | Opt-in plugin evaluation, result validation, nearest-neighbor compositing, and PNG output without a window system. |

The plugin's BRL-CAD database is intentionally separate from OSPRay's database. This duplicates some
memory, but isolates the experiment and works identically with in-process and render-worker modes.

## Runtime flow

1. At viewer construction, the controller searches for the plugin and validates its interface id.
2. The user loads a `.g` scene and enables **Cell plot overlay (prototype)** in the Visualization
   panel.
3. `RenderWidget` snapshots eye, forward, up, FOV, aspect, projection, and focus distance.
4. After the debounce interval, the controller's background thread asks the plugin to load/cache the
   selected database and top object, then evaluates the grid.
5. The plugin builds a view ray for each cell, calls `rt_shootray`, reads the first partition's
   `reg_regionid`, and looks up the initialization-time hash color. Misses stay transparent.
6. Only the newest completed generation is accepted. A newer view cancels the old grid between rows.
7. `RenderWidget` draws the grid over the normal OSPRay image with nearest-neighbor scaling.

## Plugin discovery and deployment

CMake builds the plugin by default when `IBRT_BUILD_PLUGIN_CELL_PLOT=ON`. A viewer build copies it to:

```text
<IBRT executable directory>/plugins/visualizations/
```

The controller searches that directory first and the executable directory second. For development or
diagnostics, set `IBRT_CELL_PLOT_PLUGIN` to an explicit plugin library path. Installed builds place the
plugin in the same executable-relative plugin directory.

Set `IBRT_ENABLE_CELL_PLOT=1` to enable the overlay at startup for automated smoke tests or prototype
demos; otherwise it remains opt-in through the Visualization panel.

Disabling `IBRT_BUILD_PLUGIN_CELL_PLOT` leaves the viewer functional; the checkbox is disabled and the
controller reports that no plugin was found.

### Headless rendering

`IBRTOfflineRender` uses the same executable-relative discovery path and plugin implementation as the
viewer. Add `--cell-plot` to make plugin loading and nonempty output mandatory; a discovery, scene-load,
ray-count, or evaluation failure returns a nonzero exit status instead of writing a base-only image.
The default grid is 100 x 100 and can be overridden for diagnostics.

```text
IBRTOfflineRender toyjeep.g auto toyjeep-cell-plot.png \
  --renderer scivis --width 1200 --height 900 --frames 4 \
  --cell-plot --cell-plot-columns 100 --cell-plot-rows 100
```

Use `--cell-plot-plugin PATH` to select an explicit library. The
`IBRT_CELL_PLOT_PLUGIN` environment variable remains available when no explicit path is passed.

## Verification

- `IBRTUnitTests` checks perspective and orthographic cell-ray orientation plus stable, visible region
  hash colors.
- `IBRTCellPlotPluginSmoke` dynamically loads the built library, prepares `moss.g/all.g`, confirms that
  every grid cell produces one ray and that some cells hit geometry, verifies repeatability for an
  unchanged view, then verifies that a moved view produces a different grid.
- `IBRTOfflineCellPlotMoss` exercises executable-relative discovery, scene loading, cell evaluation,
  compositing, and PNG creation through the headless command.
- Building the `IBRT` target verifies host integration and deploys the plugin beside the executable.

## Prototype limitations and next steps

- Ray evaluation is single-threaded; a 100 x 100 grid performs 10,000 `rt_shootray` calls.
- Region ids are colored, not evaluated through the production ray-shading backend.
- Only the nearest region is represented in a cell; full ordered hit lists remain a production step.
- The grid is recomputed after a short debounce rather than continuously during every mouse event.
- The edge/hidden-line composite described in the plan is not part of this secondary overlay prototype.
- A production plugin should consume the real `RayShader` implementation behind the same host
  lifecycle, add batching/thread-local evaluators, and expose measured timing in the UI.
