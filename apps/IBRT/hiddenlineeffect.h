// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ibrt::render {

// Hidden-line rendering is a frame effect, not a base renderer.  Keeping this
// state independent of the OSPRay renderer selection lets the same edges be
// composited over SciVis, AO, path tracing, or future renderer providers.
enum class HiddenLineMode : std::uint32_t
{
  Disabled = 0,
  Overlay = 1,
  EdgesOnly = 2
};

struct HiddenLineSample
{
  bool hit = false;
  std::int32_t regionId = -1;
  std::uintptr_t regionKey = 0;
  float distance = 0.0f;
  float normalX = 0.0f;
  float normalY = 0.0f;
  float normalZ = 0.0f;
};

// Defaults mirror rtedge/viewedge.c: region-id, distance, and normal changes
// are edges; region-pointer changes are optional.  bothSides is enabled for
// the interactive viewer so thin contours remain visible on either side of a
// foreground/background transition.
struct HiddenLineSettings
{
  bool detectRegionIds = true;
  bool detectRegions = false;
  bool detectDistance = true;
  bool detectNormals = true;
  bool bothSides = true;
  float normalCosineTolerance = 0.91f;
  float maxDistance = 0.0f;
  std::uint32_t lineColor = 0xff000000u;
  std::uint32_t backgroundColor = 0xffffffffu;
};

std::vector<std::uint8_t> detectHiddenLineEdges(const HiddenLineSample *samples,
    int width,
    int height,
    const HiddenLineSettings &settings);

void compositeHiddenLineEdges(std::uint32_t *pixels,
    std::size_t pixelCount,
    const std::uint8_t *edgeMask,
    HiddenLineMode mode,
    const HiddenLineSettings &settings);

} // namespace ibrt::render
