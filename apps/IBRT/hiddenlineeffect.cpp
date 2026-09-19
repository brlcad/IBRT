// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "hiddenlineeffect.h"

#include <algorithm>
#include <cmath>

namespace ibrt::render {
namespace {

bool differs(const HiddenLineSample &a,
    const HiddenLineSample &b,
    const HiddenLineSettings &settings)
{
  if (a.hit != b.hit)
    return true;
  if (!a.hit)
    return false;

  if (settings.detectRegionIds && a.regionId != b.regionId)
    return true;
  if (settings.detectRegions && a.regionKey != b.regionKey)
    return true;
  if (settings.detectDistance
      && std::fabs(a.distance - b.distance) > settings.maxDistance) {
    return true;
  }
  if (settings.detectNormals) {
    const float dot =
        a.normalX * b.normalX + a.normalY * b.normalY + a.normalZ * b.normalZ;
    if (dot < settings.normalCosineTolerance)
      return true;
  }
  return false;
}

} // namespace

std::vector<std::uint8_t> detectHiddenLineEdges(const HiddenLineSample *samples,
    int width,
    int height,
    const HiddenLineSettings &settings)
{
  if (!samples || width <= 0 || height <= 0)
    return {};

  const std::size_t count = std::size_t(width) * std::size_t(height);
  std::vector<std::uint8_t> mask(count, 0u);
  const HiddenLineSample miss{};

  const auto at = [&](int x, int y) -> const HiddenLineSample & {
    if (x < 0 || x >= width || y < 0 || y >= height)
      return miss;
    return samples[std::size_t(y) * std::size_t(width) + std::size_t(x)];
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const HiddenLineSample &here = at(x, y);
      bool edge = differs(here, at(x - 1, y), settings)
          || differs(here, at(x, y - 1), settings);
      if (settings.bothSides) {
        edge = edge || differs(here, at(x + 1, y), settings)
            || differs(here, at(x, y + 1), settings);
      }
      mask[std::size_t(y) * std::size_t(width) + std::size_t(x)] =
          edge ? 1u : 0u;
    }
  }
  return mask;
}

void compositeHiddenLineEdges(std::uint32_t *pixels,
    std::size_t pixelCount,
    const std::uint8_t *edgeMask,
    HiddenLineMode mode,
    const HiddenLineSettings &settings)
{
  if (!pixels || !edgeMask || mode == HiddenLineMode::Disabled)
    return;

  if (mode == HiddenLineMode::EdgesOnly)
    std::fill(pixels, pixels + pixelCount, settings.backgroundColor);

  for (std::size_t i = 0; i < pixelCount; ++i) {
    if (edgeMask[i])
      pixels[i] = settings.lineColor;
  }
}

} // namespace ibrt::render
