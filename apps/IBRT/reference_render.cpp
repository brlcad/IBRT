// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <QImage>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include <ospray/ospray.h>

#include "ospraybackend.h"
#include "renderappearance.h"

namespace {

constexpr int kWidth = 1200;
constexpr int kHeight = 900;

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

rkcommon::math::vec3f normalize(const rkcommon::math::vec3f &v)
{
  const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return length > 1e-8f ? v / length : rkcommon::math::vec3f(0.f, 0.f, 1.f);
}

bool renderUntilReady(OsprayBackend &backend, uint64_t targetFrames)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (std::chrono::steady_clock::now() < deadline) {
    backend.advanceRender(10);
    if (backend.pixels() && backend.accumulatedFrames() >= targetFrames)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool saveViewportPng(const char *path, const OsprayBackend &backend)
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

      // OSP_FB_SRGBA packs R, G, B, A from least to most significant byte.
      // The known-good viewport swaps R/B when presenting the mapped frame.
      dst[x * 3 + 0] = static_cast<unsigned char>((packed >> 16) & 0xffu);
      dst[x * 3 + 1] = static_cast<unsigned char>((packed >> 8) & 0xffu);
      dst[x * 3 + 2] = static_cast<unsigned char>(packed & 0xffu);
    }
  }

  return image.save(QString::fromLocal8Bit(path), "PNG");
}

} // namespace

int main(int argc, char **argv)
{
  if (argc != 4) {
    std::fprintf(stderr,
        "usage: %s <moss.g> <object> <output.png>\n",
        argv[0]);
    return 2;
  }

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
      const auto referenceBackground =
          ibrt::renderappearance::kReferenceBackground;
      backend.setOpaqueBackgroundColor(rkcommon::math::vec3f(referenceBackground.r,
          referenceBackground.g,
          referenceBackground.b));
      backend.setSettingsMode(OsprayBackend::SettingsMode::Custom);
      backend.setCustomStartScale(1);
      backend.setCustomAccumulationEnabled(true);
      backend.setCustomMaxAccumulationFrames(16);
      backend.setAoSamples(1);
      backend.setPixelSamples(1);
      backend.resize(kWidth, kHeight);
      backend.setRenderer("scivis");

      if (!backend.loadBrlcad(argv[1], argv[2])) {
        std::fprintf(stderr, "Moss load failed: %s\n", backend.lastError().c_str());
      } else {
        const auto center = backend.getBoundsCenter();
        const float maxExtent = std::max(backend.getBoundsMaxExtent(), 1.f);
        const float fovy = 60.f;
        const float distance =
            (0.5f * maxExtent) / std::tan(0.5f * fovy * 3.14159265f / 180.f)
            * 1.3f;
        const float theta = 0.3f;
        const float phi = 1.77079633f;
        const rkcommon::math::vec3f eyeDirection = normalize(
            rkcommon::math::vec3f(std::sin(theta) * std::sin(phi),
                std::cos(theta) * std::sin(phi),
                std::cos(phi)));
        const rkcommon::math::vec3f eye = center + distance * eyeDirection;
        const rkcommon::math::vec3f forward = -eyeDirection;
        const rkcommon::math::vec3f worldUp(0.f, 0.f, 1.f);
        const float upDot = worldUp.x * forward.x + worldUp.y * forward.y
            + worldUp.z * forward.z;
        const rkcommon::math::vec3f cameraUp =
            normalize(worldUp - upDot * forward);
        backend.setCamera(eye, center, cameraUp, fovy);
        backend.resetAccumulation();

        if (!renderUntilReady(backend, 16)) {
          std::fprintf(stderr, "Moss render timed out.\n");
        } else if (!saveViewportPng(argv[3], backend)) {
          std::fprintf(stderr, "Could not write PNG: %s\n", argv[3]);
        } else {
          std::printf("Wrote %dx%d reference image: %s\n", kWidth, kHeight, argv[3]);
          result = 0;
        }
      }
    }
  }

  ospShutdown();
  return result;
}
