// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Tolerance / perceptual image comparison for raw RGBA buffers. The
// reference-render regression test currently compares an exact SHA-256 of the
// PNG, which is really a toolchain pin:
// OSPRay/ISPC output is not bit-reproducible across SIMD targets, so the test
// fails on a visually-identical image. This header provides the pieces of a
// tolerance-based comparator that both the CTest and unit tests can share.
//
// It operates on plain interleaved 8-bit RGBA byte buffers so it has no Qt /
// image-library dependency and is trivially unit-testable. Callers decode PNGs
// (QImage, stb_image, etc.) into RGBA and hand the bytes here.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ibrt::imagecompare {

struct Result
{
  bool sizeMismatch = false; // dimensions/length differed; other fields unset
  double meanAbsError = 0.0; // mean per-channel absolute difference, 0..255
  int maxAbsError = 0;       // largest single-channel absolute difference, 0..255
  double fractionOverThreshold = 0.0; // fraction of channels differing by > perChannelThreshold
  std::size_t channelsCompared = 0;
};

// Tolerances for a pass/fail verdict. Defaults are deliberately loose enough to
// absorb SIMD-target rounding noise but tight enough to catch a real regression.
struct Tolerance
{
  int perChannelThreshold = 12;      // a channel diff <= this is "noise"
  double maxFractionOverThreshold = 0.02; // allow up to 2% noisy channels
  double maxMeanAbsError = 2.0;      // average diff must stay small
};

// Compares two tightly-packed RGBA8 buffers of width*height pixels (4 bytes per
// pixel, no row padding). Alpha is included in the per-channel statistics.
inline Result compareRgba(const std::uint8_t *a, const std::uint8_t *b, int width,
    int height, const Tolerance &tol = Tolerance{})
{
  Result r;
  if (!a || !b || width <= 0 || height <= 0) {
    r.sizeMismatch = true;
    return r;
  }

  const std::size_t channels = std::size_t(width) * std::size_t(height) * 4u;
  std::uint64_t sumAbs = 0;
  std::uint64_t overCount = 0;
  int maxAbs = 0;

  for (std::size_t i = 0; i < channels; ++i) {
    const int diff = int(a[i]) - int(b[i]);
    const int ad = diff < 0 ? -diff : diff;
    sumAbs += std::uint64_t(ad);
    if (ad > maxAbs)
      maxAbs = ad;
    if (ad > tol.perChannelThreshold)
      ++overCount;
  }

  r.channelsCompared = channels;
  r.meanAbsError = double(sumAbs) / double(channels);
  r.maxAbsError = maxAbs;
  r.fractionOverThreshold = double(overCount) / double(channels);
  return r;
}

// True when the two images are within tolerance (i.e. no meaningful regression).
inline bool withinTolerance(const Result &r, const Tolerance &tol = Tolerance{})
{
  if (r.sizeMismatch)
    return false;
  return r.meanAbsError <= tol.maxMeanAbsError
      && r.fractionOverThreshold <= tol.maxFractionOverThreshold;
}

// Convenience: compare and return the verdict in one call.
inline bool imagesMatch(const std::uint8_t *a, const std::uint8_t *b, int width,
    int height, const Tolerance &tol = Tolerance{})
{
  return withinTolerance(compareRgba(a, b, width, height, tol), tol);
}

} // namespace ibrt::imagecompare
