// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Dependency-free unit tests for the standalone utility headers added alongside
// the improvement plan: ibrt_constants.h, ipc_wire.h, colorramp.h,
// cameramath.h, imagecompare.h. This deliberately uses only the C++ standard
// library (no Qt/OSPRay/BRL-CAD) so it builds and runs anywhere, giving fast
// verification of the pure logic without the full app toolchain. It is wired as
// the IBRTUnitTests CTest.
//
// ipc_wire.h and ibrt_constants.h enforce their invariants via static_assert, so
// merely including them here is the test; the runtime checks below cover the
// ramp, camera math, and image comparison.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "cameramath.h"
#include "colorramp.h"
#include "edgerender.h"
#include "ibrt_constants.h"
#include "imagecompare.h"
#include "ipc_wire.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char *what)
{
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

bool nearf(float a, float b, float eps = 1e-4f)
{
  return std::fabs(a - b) <= eps;
}

// Minimal vector type exercising the cameramath templates the way
// rkcommon::math::vec3f does (public x/y/z + 3-arg constructor).
struct Vec3
{
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

void testConstants()
{
  check(ibrt::kIpcMagic == 0x54425249u, "ipc magic value");
  check(ibrt::kMaxIpcPayloadSize > std::size_t(1920) * 1080 * 4,
      "max payload exceeds a normal frame");
  check(nearf(ibrt::kInitialOrbitPhi, 1.77079633f, 1e-3f), "documented phi seed");
  check(ibrt::kDefaultWatchdogMs == 1500, "watchdog default");
}

void testColorRamp()
{
  using namespace ibrt::colorramp;

  // Byte-ordered RGBA: LSB is red.
  check(packRgba(255, 0, 0, 255) == 0xFF0000FFu, "packRgba red byte order");
  check(packRgba(0, 255, 0, 255) == 0xFF00FF00u, "packRgba green byte order");

  // Band selection is half-open [lo, hi).
  Rgb low = rampColorNormalized(0.0f);
  check(low.r == 105 && low.g == 105 && low.b == 105, "t=0 -> dim grey");
  Rgb top = rampColorNormalized(1.0f);
  check(top.r == 255 && top.g == 0 && top.b == 0, "t=1 -> red");
  Rgb mid = rampColorNormalized(0.5f);
  check(mid.r == 0 && mid.g == 100 && mid.b == 0, "t=0.5 -> dark green");
  // Boundary lands in the upper band.
  Rgb atBoundary = rampColorNormalized(0.05f);
  check(atBoundary.r == 176 && atBoundary.g == 196 && atBoundary.b == 222,
      "t=0.05 boundary -> light steel blue");

  // Clamping.
  check(rampColorNormalized(-1.0f).r == 105, "t<0 clamps to first band");

  // Out-of-range value yields the magenta out-of-range color.
  check(colorForValue(-0.1, 0.0, 1.0) == packRgba(kOutOfRangeColor),
      "below-min -> out of range");
  check(colorForValue(2.0, 0.0, 1.0) == packRgba(kOutOfRangeColor),
      "above-max -> out of range");
  check(colorForValue(0.5, 0.0, 1.0) == packRgba(rampColorNormalized(0.5f)),
      "in-range matches normalized ramp");

  // 3-bin alternate.
  check(rampColorNormalized(0.1f, RampKind::Bins3).b == 255, "3-bin low -> blue");
  check(rampColorNormalized(0.9f, RampKind::Bins3).r == 255, "3-bin high -> red");
}

void testCameraMath()
{
  using namespace ibrt::cameramath;

  for (UpAxis axis : {UpAxis::Z, UpAxis::Y}) {
    // Fly angles round-trip through the forward vector.
    const float yaws[] = {-1.2f, 0.0f, 0.7f, 2.5f};
    const float pitches[] = {-1.0f, -0.2f, 0.0f, 0.9f};
    for (float yaw : yaws) {
      for (float pitch : pitches) {
        Vec3 fwd = forwardFromAngles<Vec3>(yaw, pitch, axis);
        check(nearf(vlen(fwd), 1.0f), "forward is unit length");
        float yaw2 = 0, pitch2 = 0;
        anglesFromForward<Vec3>(fwd, axis, yaw2, pitch2);
        Vec3 fwd2 = forwardFromAngles<Vec3>(yaw2, pitch2, axis);
        check(nearf(fwd.x, fwd2.x) && nearf(fwd.y, fwd2.y) && nearf(fwd.z, fwd2.z),
            "fly angles<->forward round-trip");
      }
    }

    // Orbit direction round-trips (compare directions to avoid angle wrap).
    const float thetas[] = {-2.0f, 0.0f, 1.1f};
    const float phis[] = {0.4f, 1.2f, 2.5f};
    for (float theta : thetas) {
      for (float phi : phis) {
        Vec3 dir = orbitEyeDirection<Vec3>(theta, phi, axis);
        check(nearf(vlen(dir), 1.0f), "orbit dir is unit length");
        float theta2 = 0, phi2 = 0;
        orbitAnglesFromEyeDirection<Vec3>(dir, axis, theta2, phi2);
        Vec3 dir2 = orbitEyeDirection<Vec3>(theta2, phi2, axis);
        check(nearf(dir.x, dir2.x) && nearf(dir.y, dir2.y) && nearf(dir.z, dir2.z),
            "orbit angles<->direction round-trip");
      }
    }

    // Standard views: unit eyeDir, unit up, mutually orthogonal.
    for (StandardView v : {StandardView::Front, StandardView::Back, StandardView::Left,
             StandardView::Right, StandardView::Top, StandardView::Bottom,
             StandardView::Iso}) {
      ViewOrientation<Vec3> o = standardView<Vec3>(v, axis);
      check(nearf(vlen(o.eyeDir), 1.0f), "standard view eyeDir unit");
      check(nearf(vlen(o.up), 1.0f), "standard view up unit");
      check(nearf(vdot(o.eyeDir, o.up), 0.0f, 1e-3f), "standard view eyeDir _|_ up");
    }
  }

  // Azimuth/elevation -> eye direction.
  for (UpAxis axis : {UpAxis::Z, UpAxis::Y}) {
    const Vec3 up = worldUp<Vec3>(axis);
    const Vec3 north = worldForwardReference<Vec3>(axis);
    const Vec3 east = rightReference<Vec3>(axis);

    Vec3 d0 = eyeDirectionFromAzEl<Vec3>(0.f, 0.f, axis);
    check(nearf(vlen(d0), 1.0f), "azel dir unit length");
    check(nearf(vdot(d0, north), 1.0f, 1e-3f), "az0 el0 -> north");

    Vec3 dEast = eyeDirectionFromAzEl<Vec3>(90.f, 0.f, axis);
    check(nearf(vdot(dEast, east), 1.0f, 1e-3f), "az90 el0 -> east");

    Vec3 dTop = eyeDirectionFromAzEl<Vec3>(0.f, 90.f, axis);
    check(nearf(vdot(dTop, up), 1.0f, 1e-3f), "el90 -> up");

    // The 35/25 request: elevation component equals sin(25 deg).
    Vec3 d = eyeDirectionFromAzEl<Vec3>(35.f, 25.f, axis);
    check(nearf(vlen(d), 1.0f), "azel(35,25) unit length");
    check(nearf(vdot(d, up), std::sin(25.f * 3.14159265f / 180.f), 1e-3f),
        "azel(35,25) elevation component");
  }

  // Framing distance is positive and grows with extent.
  float d1 = fitDistanceFromBounds(2.0f, 60.0f);
  float d2 = fitDistanceFromBounds(4.0f, 60.0f);
  check(d1 > 0.0f && d2 > d1, "fit distance grows with extent");
}

void testImageCompare()
{
  using namespace ibrt::imagecompare;
  const int w = 4, h = 4;
  const int n = w * h * 4;
  std::uint8_t a[n];
  std::uint8_t b[n];
  for (int i = 0; i < n; ++i) {
    a[i] = std::uint8_t((i * 7) & 0xFF);
    b[i] = a[i];
  }

  check(imagesMatch(a, b, w, h), "identical images match");

  // Small per-channel noise within threshold still matches.
  for (int i = 0; i < n; ++i)
    b[i] = std::uint8_t(a[i] + (i % 2 ? 3 : 0));
  Result rNoise = compareRgba(a, b, w, h);
  check(rNoise.maxAbsError <= 3, "noise max error bounded");
  check(withinTolerance(rNoise), "small noise within tolerance");

  // A large localized difference fails.
  for (int i = 0; i < n; ++i)
    b[i] = a[i];
  for (int i = 0; i < n; ++i)
    b[i] = std::uint8_t(255 - a[i]);
  check(!imagesMatch(a, b, w, h), "inverted image fails tolerance");

  // Size mismatch guard.
  check(compareRgba(a, b, 0, h).sizeMismatch, "zero width is size mismatch");
  check(!imagesMatch(nullptr, b, w, h), "null buffer does not match");
}

void testEdgeRendering()
{
  using namespace ibrt::edgerender;

  constexpr int width = 5;
  constexpr int height = 5;
  constexpr std::uint32_t baseColor = 0xff332211u;
  const std::size_t pixelCount = std::size_t(width) * std::size_t(height);
  std::vector<float> depths(pixelCount, std::numeric_limits<float>::infinity());
  std::vector<float> normals(pixelCount * 3, 0.0f);
  std::vector<std::uint32_t> objectIds(pixelCount, 0u);

  for (int y = 1; y <= 3; ++y) {
    for (int x = 1; x <= 3; ++x) {
      const std::size_t index = std::size_t(y) * std::size_t(width) + std::size_t(x);
      normals[index * 3 + 2] = 1.0f;
      objectIds[index] = 1u;
    }
  }

  const Color edgeColor{1.0f, 0.0f, 0.0f};
  const Color fillColor{0.0f, 1.0f, 0.0f};
  std::vector<std::uint32_t> overlayPixels(pixelCount, baseColor);
  composite(overlayPixels.data(),
      depths.data(),
      normals.data(),
      3,
      objectIds.data(),
      width,
      height,
      Mode::Overlay,
      edgeColor,
      fillColor);

  check(overlayPixels[std::size_t(2) * width + 2] == baseColor,
      "overlay preserves non-edge object pixels");
  check(overlayPixels[std::size_t(2) * width + 1] == packSrgba(edgeColor),
      "overlay colors visible silhouette pixels");
  check(overlayPixels[0] == baseColor, "overlay preserves background pixels");

  std::vector<std::uint32_t> flatPixels(pixelCount, baseColor);
  composite(flatPixels.data(),
      depths.data(),
      normals.data(),
      3,
      objectIds.data(),
      width,
      height,
      Mode::FlatFill,
      edgeColor,
      fillColor);

  check(flatPixels[std::size_t(2) * width + 2] == packSrgba(fillColor),
      "flat fill colors non-edge object pixels");
  check(flatPixels[std::size_t(2) * width + 1] == packSrgba(edgeColor),
      "flat fill retains edge color");
  check(flatPixels[0] == baseColor, "flat fill preserves background pixels");

  const float idDepths[] = {1.0f, 1.0f, 1.0f};
  const float idNormals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  const std::uint32_t idObjects[] = {7u, 8u, 8u};
  check(isEdgePixel(idDepths, idNormals, 3, idObjects, 3, 1, 0, 0),
      "object ID transition is an edge");
  check(!isEdgePixel(idDepths, idNormals, 3, idObjects, 3, 1, 2, 0),
      "matching object ID is not an edge");
}

} // namespace

int main()
{
  testConstants();
  testColorRamp();
  testCameraMath();
  testImageCompare();
  testEdgeRendering();

  std::printf("IBRTUnitTests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
