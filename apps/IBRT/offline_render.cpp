// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Headless (no-GUI) still-image renderer built on OsprayBackend (improvement
// plan item #49 / F-01). Unlike IBRTReferenceRender (a fixed-purpose regression
// fixture), this is a general command-line tool: pick the model/object, renderer,
// projection, resolution, camera azimuth/elevation, frame count, and background,
// then write a PNG.
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
//     --edge-mode off|overlay|flat         (default: off)
//     --edge-color R,G,B|#RRGGBB           (default: black)
//     --flat-fill-color R,G,B|#RRGGBB      (default: neutral gray)
//     --show-sky                          (draw the environment; default hides it
//                                          for a clean white background)
//
// "object" may be "auto" to pick a sensible top-level object automatically.

#include <QImage>

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
#include "ospraybackend.h"

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
  OsprayBackend::EdgeRenderMode edgeRenderMode =
      OsprayBackend::EdgeRenderMode::Disabled;
  rkcommon::math::vec3f edgeColor{0.0f, 0.0f, 0.0f};
  rkcommon::math::vec3f flatFillColor{0.78f, 0.78f, 0.78f};
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

bool parseColor(const char *value, rkcommon::math::vec3f &color)
{
  if (!value)
    return false;

  if (value[0] == '#' && std::strlen(value) == 7) {
    char *end = nullptr;
    const unsigned long packed = std::strtoul(value + 1, &end, 16);
    if (!end || *end != '\0' || packed > 0xfffffful)
      return false;
    color = rkcommon::math::vec3f(float((packed >> 16) & 0xffu) / 255.0f,
        float((packed >> 8) & 0xffu) / 255.0f,
        float(packed & 0xffu) / 255.0f);
    return true;
  }

  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  char extra = '\0';
  if (std::sscanf(value, "%f,%f,%f%c", &red, &green, &blue, &extra) != 3
      || !std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
    return false;
  }

  color = rkcommon::math::vec3f(
      std::clamp(red, 0.0f, 1.0f), std::clamp(green, 0.0f, 1.0f), std::clamp(blue, 0.0f, 1.0f));
  return true;
}

bool parseEdgeMode(const char *value, OsprayBackend::EdgeRenderMode &mode)
{
  if (wantsArg(value, "off") || wantsArg(value, "disabled")) {
    mode = OsprayBackend::EdgeRenderMode::Disabled;
    return true;
  }
  if (wantsArg(value, "overlay")) {
    mode = OsprayBackend::EdgeRenderMode::Overlay;
    return true;
  }
  if (wantsArg(value, "flat") || wantsArg(value, "flat-fill")) {
    mode = OsprayBackend::EdgeRenderMode::FlatFill;
    return true;
  }
  return false;
}

const char *edgeModeName(OsprayBackend::EdgeRenderMode mode)
{
  switch (mode) {
  case OsprayBackend::EdgeRenderMode::Overlay:
    return "overlay";
  case OsprayBackend::EdgeRenderMode::FlatFill:
    return "flat";
  case OsprayBackend::EdgeRenderMode::Disabled:
  default:
    return "off";
  }
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
    } else if (wantsArg(a, "--edge-mode")) {
      const char *v = next(a);
      if (!v || !parseEdgeMode(v, opt.edgeRenderMode)) {
        std::fprintf(stderr, "Invalid edge mode. Use off, overlay, or flat.\n");
        return false;
      }
    } else if (wantsArg(a, "--edge-color")) {
      const char *v = next(a);
      if (!v || !parseColor(v, opt.edgeColor)) {
        std::fprintf(stderr, "Invalid edge color. Use R,G,B or #RRGGBB.\n");
        return false;
      }
    } else if (wantsArg(a, "--flat-fill-color") || wantsArg(a, "--flat-color")) {
      const char *v = next(a);
      if (!v || !parseColor(v, opt.flatFillColor)) {
        std::fprintf(stderr, "Invalid flat fill color. Use R,G,B or #RRGGBB.\n");
        return false;
      }
    } else if (wantsArg(a, "--renderer")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.renderer = v;
    } else if (wantsArg(a, "--projection")) {
      const char *v = next(a);
      if (!v)
        return false;
      opt.orthographic = (std::strcmp(v, "orthographic") == 0 || std::strcmp(v, "ortho") == 0);
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
// whose name matches the database file stem (e.g. toyjeep.g -> "toyjeep"), then a
// conventional "all"/"all.g" assembly, otherwise the first listed object.
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

bool saveViewportPng(const std::string &path, const OsprayBackend &backend)
{
  const uint32_t *pixels = backend.pixels();
  if (!pixels || backend.width() <= 0 || backend.height() <= 0)
    return false;

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
      backend.setEdgeRenderMode(opt.edgeRenderMode);
      backend.setEdgeColor(opt.edgeColor);
      backend.setFlatFillColor(opt.flatFillColor);
      // White background: keep the (default white) backgroundColor and hide the
      // environment so escaped rays are not tinted by the sky dome.
      backend.setEnvironmentVisible(opt.showSky);

      const std::string object =
          (opt.object == "auto" || opt.object == "-")
          ? chooseObject(backend, opt.db)
          : opt.object;
      if (object.empty()) {
        std::fprintf(stderr, "No selectable object found in %s\n", opt.db.c_str());
      } else if (!backend.loadBrlcad(opt.db, object)) {
        std::fprintf(stderr, "Load failed (%s / %s): %s\n", opt.db.c_str(),
            object.c_str(), backend.lastError().c_str());
      } else {
        using vec3f = rkcommon::math::vec3f;
        const vec3f center = backend.getBoundsCenter();
        const float maxExtent = std::max(backend.getBoundsMaxExtent(), 1.f);
        const float distance =
            cameramath::fitDistanceFromBounds(maxExtent, opt.fovy, 1.3f);
        const vec3f eyeDir =
            cameramath::eyeDirectionFromAzEl<vec3f>(opt.az, opt.el, opt.up);
        const vec3f eye = center + distance * eyeDir;
        const vec3f up = cameramath::worldUp<vec3f>(opt.up);
        backend.setCamera(eye, center, up, opt.fovy);
        backend.resetAccumulation();

        std::printf(
            "Rendering %s (%s) %dx%d %s %s edge=%s az/el=%.0f/%.0f frames=%d ...\n",
            opt.db.c_str(), object.c_str(), opt.width, opt.height,
            opt.renderer.c_str(),
            opt.orthographic ? "orthographic" : "perspective",
            edgeModeName(opt.edgeRenderMode), opt.az, opt.el, opt.frames);

        if (!renderUntilReady(backend, uint64_t(opt.frames))) {
          std::fprintf(stderr, "Render timed out.\n");
        } else if (!saveViewportPng(opt.output, backend)) {
          std::fprintf(stderr, "Could not write PNG: %s\n", opt.output.c_str());
        } else {
          std::printf("Wrote %dx%d image: %s\n", opt.width, opt.height,
              opt.output.c_str());
          result = 0;
        }
      }
    }
  }

  ospShutdown();
  return result;
}
