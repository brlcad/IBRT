// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Headless (no-GUI) still-image renderer built on OsprayBackend (improvement
// plan item #49 / F-01). Unlike IBRTReferenceRender (a fixed-purpose regression
// fixture), this is a general command-line tool: pick the model/object,
// renderer, projection, resolution, camera azimuth/elevation, frame count, and
// background, then write a PNG.
//
//   IBRTOfflineRender <db.g> <object|auto> <output.png> [options]
//     --renderer   scivis|pathtracer|ao   (default: pathtracer)
//     --projection perspective|orthographic (default: perspective)
//     --width N  --height N               (default: 1024 x 1024)
//     --az DEG   --el DEG                 (default: 35 / 25)
//     --fovy DEG                          (default: 40)
//     --frames N                          (default: 64 accumulation frames)
//     --pixel-samples N                   (default: 4)
//     --up z|y                            (default: z)
//     --show-sky                          (draw the environment; default hides
//     it
//                                          for a clean white background)
//     --hidden-lines off|overlay|only     (default: off)
//     --cell-plot                         composite the runtime cell-plot
//     plugin
//     --cell-plot-columns N               (default: 100)
//     --cell-plot-rows N                  (default: 100)
//     --cell-plot-plugin PATH             explicit plugin library (optional)
//
// "object" may be "auto" to pick a sensible top-level object automatically.

#include <QCoreApplication>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <ospray/ospray.h>

#include "cameramath.h"
#include "cellplotpluginloader.h"
#include "ospraybackend.h"
#include "renderappearance.h"

namespace cameramath = ibrt::cameramath;

namespace {

struct Options
{
  std::string db;
  std::string object = "auto";
  std::string output;
  std::string renderer = "pathtracer";
  bool orthographic = false;
  int width = 1024;
  int height = 1024;
  float az = 35.f;
  float el = 25.f;
  float fovy = 40.f;
  int frames = 64;
  int pixelSamples = 4;
  cameramath::UpAxis up = cameramath::UpAxis::Z;
  bool showSky = false;
  ibrt::render::HiddenLineMode hiddenLineMode =
      ibrt::render::HiddenLineMode::Disabled;
  bool cellPlot = false;
  int cellPlotColumns = ibrt::cellplot::kDefaultGridColumns;
  int cellPlotRows = ibrt::cellplot::kDefaultGridRows;
  std::string cellPlotPlugin;
};

void ensureOsprayLoadModule(const char *moduleName)
{
  const QByteArray module(moduleName);
  QByteArray modules = qgetenv("OSPRAY_LOAD_MODULES");
  for (const QByteArray &entry : modules.split(',')) {
    if (entry.trimmed() == module)
      return;
  }
  if (!modules.isEmpty() && !modules.endsWith(','))
    modules.append(',');
  modules.append(module);
  qputenv("OSPRAY_LOAD_MODULES", modules);
}

bool wantsArg(const char *a, const char *name)
{
  return std::strcmp(a, name) == 0;
}

bool parseArgs(int argc, char **argv, Options &opt)
{
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    auto next = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Missing value for %s\n", name);
        return nullptr;
      }
      return argv[++i];
    };

    if (a[0] != '-') {
      positional.push_back(a);
    } else if (wantsArg(a, "--renderer")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.renderer = v;
    } else if (wantsArg(a, "--projection")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.orthographic =
          (std::strcmp(v, "orthographic") == 0 || std::strcmp(v, "ortho") == 0);
    } else if (wantsArg(a, "--width")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.width = std::atoi(v);
    } else if (wantsArg(a, "--height")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.height = std::atoi(v);
    } else if (wantsArg(a, "--az")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.az = float(std::atof(v));
    } else if (wantsArg(a, "--el")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.el = float(std::atof(v));
    } else if (wantsArg(a, "--fovy")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.fovy = float(std::atof(v));
    } else if (wantsArg(a, "--frames")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.frames = std::max(1, std::atoi(v));
    } else if (wantsArg(a, "--pixel-samples")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.pixelSamples = std::max(1, std::atoi(v));
    } else if (wantsArg(a, "--up")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.up = (std::strcmp(v, "y") == 0 || std::strcmp(v, "Y") == 0)
          ? cameramath::UpAxis::Y
          : cameramath::UpAxis::Z;
    } else if (wantsArg(a, "--show-sky")) {
      opt.showSky = true;
    } else if (wantsArg(a, "--hidden-lines")) {
      const char *v = next(a);
      if (!v)
        return false;
      if (std::strcmp(v, "overlay") == 0) {
        opt.hiddenLineMode = ibrt::render::HiddenLineMode::Overlay;
      } else if (std::strcmp(v, "only") == 0
          || std::strcmp(v, "edges") == 0) {
        opt.hiddenLineMode = ibrt::render::HiddenLineMode::EdgesOnly;
      } else if (std::strcmp(v, "off") == 0
          || std::strcmp(v, "none") == 0) {
        opt.hiddenLineMode = ibrt::render::HiddenLineMode::Disabled;
      } else {
        std::fprintf(stderr,
            "Unknown hidden-line mode: %s (expected off, overlay, or only)\n",
            v);
        return false;
      }
    } else if (wantsArg(a, "--cell-plot")) {
      opt.cellPlot = true;
    } else if (wantsArg(a, "--cell-plot-columns")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.cellPlotColumns = std::clamp(std::atoi(v), 1, 1024);
    } else if (wantsArg(a, "--cell-plot-rows")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.cellPlotRows = std::clamp(std::atoi(v), 1, 1024);
    } else if (wantsArg(a, "--cell-plot-plugin")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.cellPlotPlugin = v;
    } else {
      std::fprintf(stderr, "Unknown option: %s\n", a);
      return false;
    }
  }

  if (positional.size() < 3) {
    std::fprintf(stderr,
        "usage: IBRTOfflineRender <db.g> <object|auto> <output.png> [options]\n");
    return false;
  }
  opt.db = positional[0];
  opt.object = positional[1];
  opt.output = positional[2];
  return true;
}

// Picks a reasonable top-level object when the caller passes "auto": prefer one
// whose name matches the database file stem (e.g. toyjeep.g -> "toyjeep"), then
// a conventional "all"/"all.g" assembly, otherwise the first listed object.
std::string chooseObject(OsprayBackend &backend, const std::string &db)
{
  const std::vector<std::string> objects = backend.listBrlcadObjects(db);
  if (objects.empty())
    return std::string();

  std::string stem = db;
  const size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos)
    stem = stem.substr(slash + 1);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos)
    stem = stem.substr(0, dot);

  for (const std::string &o : objects) {
    std::string base = o;
    const size_t d = base.find_last_of('.');
    if (d != std::string::npos)
      base = base.substr(0, d);
    if (base == stem || o == stem)
      return o;
  }
  for (const std::string &o : objects) {
    if (o == "all.g" || o == "all")
      return o;
  }
  return objects.front();
}

bool renderUntilReady(OsprayBackend &backend, uint64_t targetFrames)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(10);
  while (std::chrono::steady_clock::now() < deadline) {
    backend.advanceRender(10);
    if (backend.pixels() && backend.accumulatedFrames() >= targetFrames)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

QImage makeViewportImage(const OsprayBackend &backend)
{
  const uint32_t *pixels = backend.pixels();
  if (!pixels || backend.width() <= 0 || backend.height() <= 0)
    return {};

  QImage image(backend.width(), backend.height(), QImage::Format_RGB888);
  for (int y = 0; y < backend.height(); ++y) {
    auto *dst = image.scanLine(y);
    const int sourceY = backend.height() - 1 - y;
    for (int x = 0; x < backend.width(); ++x) {
      const uint32_t packed = pixels[sourceY * backend.width() + x];
      // OSP_FB_SRGBA packs R,G,B,A least- to most-significant; the viewport
      // presents with R/B swapped, matched here so offline output agrees.
      dst[x * 3 + 0] = static_cast<unsigned char>((packed >> 16) & 0xffu);
      dst[x * 3 + 1] = static_cast<unsigned char>((packed >> 8) & 0xffu);
      dst[x * 3 + 2] = static_cast<unsigned char>(packed & 0xffu);
    }
  }
  return image;
}

bool evaluateCellPlot(const Options &opt,
    const std::string &object,
    const rkcommon::math::vec3f &eye,
    const rkcommon::math::vec3f &center,
    const rkcommon::math::vec3f &up,
    QImage &overlay)
{
  CellPlotPluginLoader loader;
  const QString explicitPath = QString::fromStdString(opt.cellPlotPlugin);
  if (!loader.discover(explicitPath)) {
    std::fprintf(stderr, "%s\n", loader.status().toUtf8().constData());
    return false;
  }

  auto *plugin = loader.plugin();
  QString loadError;
  if (!plugin->loadScene(QString::fromStdString(opt.db),
          QString::fromStdString(object),
          loadError)) {
    std::fprintf(stderr,
        "Cell-plot scene load failed: %s\n",
        loadError.toUtf8().constData());
    return false;
  }

  const rkcommon::math::vec3f forward = center - eye;
  const double focusDistance = std::sqrt(double(forward.x) * forward.x
      + double(forward.y) * forward.y + double(forward.z) * forward.z);
  ibrt::cellplot::Request request;
  request.columns = opt.cellPlotColumns;
  request.rows = opt.cellPlotRows;
  request.camera.position = {eye.x, eye.y, eye.z};
  request.camera.forward = {forward.x, forward.y, forward.z};
  request.camera.up = {up.x, up.y, up.z};
  request.camera.verticalFovDegrees = opt.fovy;
  request.camera.aspectRatio = double(opt.width) / double(opt.height);
  request.camera.focusDistance = std::max(focusDistance, 1e-9);
  request.camera.orthographic = opt.orthographic;

  const ibrt::cellplot::Result cellPlot = plugin->evaluate(request, nullptr);
  const std::uint64_t expectedRays =
      std::uint64_t(request.columns) * std::uint64_t(request.rows);
  if (!cellPlot.error.isEmpty() || cellPlot.cancelled
      || cellPlot.raysTraced != expectedRays || cellPlot.cellsHit == 0
      || cellPlot.overlay.isNull()) {
    std::fprintf(stderr,
        "Cell-plot evaluation failed: error='%s', rays=%llu/%llu, hits=%llu\n",
        cellPlot.error.toUtf8().constData(),
        static_cast<unsigned long long>(cellPlot.raysTraced),
        static_cast<unsigned long long>(expectedRays),
        static_cast<unsigned long long>(cellPlot.cellsHit));
    return false;
  }

  overlay = cellPlot.overlay;
  std::printf("Cell plot: %s via %s, %llu/%llu cells hit\n",
      plugin->name().toUtf8().constData(),
      loader.libraryPath().toUtf8().constData(),
      static_cast<unsigned long long>(cellPlot.cellsHit),
      static_cast<unsigned long long>(cellPlot.raysTraced));
  return true;
}

bool saveViewportPng(const std::string &path,
    const OsprayBackend &backend,
    const QImage &overlay)
{
  QImage image = makeViewportImage(backend);
  if (image.isNull())
    return false;

  if (!overlay.isNull()) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(image.rect(), overlay);
  }
  return image.save(QString::fromStdString(path), "PNG");
}

} // namespace

int main(int argc, char **argv)
{
  Options opt;
  if (!parseArgs(argc, argv, opt))
    return 2;

  ensureOsprayLoadModule("brl_cad");

  int ospArgc = argc;
  const char **ospArgv = const_cast<const char **>(argv);
  if (ospInit(&ospArgc, ospArgv) != OSP_NO_ERROR) {
    std::fprintf(stderr, "OSPRay initialization failed.\n");
    return 1;
  }

  int qtArgc = 1;
  QCoreApplication qtApp(qtArgc, argv);

  int result = 1;
  {
    OSPDevice device = ospNewDevice("cpu");
    if (!device) {
      std::fprintf(stderr, "OSPRay CPU device creation failed.\n");
    } else {
      ospSetCurrentDevice(device);
      ospCommit(reinterpret_cast<OSPObject>(device));

      OsprayBackend backend;
      backend.init();
      const auto worldUp =
          cameramath::worldUp<rkcommon::math::vec3f>(opt.up);
      backend.setWorldUp(worldUp);
      const auto referenceBackground =
          ibrt::renderappearance::kReferenceBackground;
      backend.setOpaqueBackgroundColor(rkcommon::math::vec3f(
          referenceBackground.r, referenceBackground.g, referenceBackground.b));
      backend.setSettingsMode(OsprayBackend::SettingsMode::Custom);
      backend.setCustomStartScale(1);
      backend.setCustomAccumulationEnabled(true);
      backend.setCustomMaxAccumulationFrames(opt.frames);
      backend.setPixelSamples(opt.pixelSamples);
      backend.setAoSamples(1);
      backend.resize(opt.width, opt.height);
      backend.setRenderer(opt.renderer);
      backend.setProjectionMode(opt.orthographic
              ? OsprayBackend::ProjectionMode::Orthographic
              : OsprayBackend::ProjectionMode::Perspective);
      // White background: hide the environment unless explicitly requested so
      // escaped rays are not tinted by the sky dome.
      backend.setEnvironmentVisible(opt.showSky);
      backend.setHiddenLineMode(opt.hiddenLineMode);

      const std::string object = (opt.object == "auto" || opt.object == "-")
          ? chooseObject(backend, opt.db)
          : opt.object;
      if (object.empty()) {
        std::fprintf(
            stderr, "No selectable object found in %s\n", opt.db.c_str());
      } else if (!backend.loadBrlcad(opt.db, object)) {
        std::fprintf(stderr,
            "Load failed (%s / %s): %s\n",
            opt.db.c_str(),
            object.c_str(),
            backend.lastError().c_str());
      } else {
        using vec3f = rkcommon::math::vec3f;
        const vec3f center = backend.getBoundsCenter();
        const float maxExtent = std::max(backend.getBoundsMaxExtent(), 1.f);
        const float distance =
            cameramath::fitDistanceFromBounds(maxExtent, opt.fovy, 1.3f);
        const vec3f eyeDir =
            cameramath::eyeDirectionFromAzEl<vec3f>(opt.az, opt.el, opt.up);
        const vec3f eye = center + distance * eyeDir;
        backend.setCamera(eye, center, worldUp, opt.fovy);
        backend.resetAccumulation();

        std::printf(
            "Rendering %s (%s) %dx%d %s %s hidden-lines=%s "
            "az/el=%.0f/%.0f frames=%d ...\n",
            opt.db.c_str(),
            object.c_str(),
            opt.width,
            opt.height,
            opt.renderer.c_str(),
            opt.orthographic ? "orthographic" : "perspective",
            opt.hiddenLineMode == ibrt::render::HiddenLineMode::Overlay
                ? "overlay"
                : (opt.hiddenLineMode
                            == ibrt::render::HiddenLineMode::EdgesOnly
                        ? "only"
                        : "off"),
            opt.az,
            opt.el,
            opt.frames);

        if (!renderUntilReady(backend, uint64_t(opt.frames))) {
          std::fprintf(stderr, "Render timed out.\n");
        } else {
          QImage cellPlotOverlay;
          const bool cellPlotReady = !opt.cellPlot
              || evaluateCellPlot(
                  opt, object, eye, center, worldUp, cellPlotOverlay);
          if (!cellPlotReady) {
            std::fprintf(stderr, "Cell-plot overlay generation failed.\n");
          } else if (!saveViewportPng(opt.output, backend, cellPlotOverlay)) {
            std::fprintf(
                stderr, "Could not write PNG: %s\n", opt.output.c_str());
          } else {
            std::printf("Wrote %dx%d image%s: %s\n",
                opt.width,
                opt.height,
                opt.cellPlot ? " with cell plot" : "",
                opt.output.c_str());
            result = 0;
          }
        }
      }
    }
  }

  ospShutdown();
  return result;
}
