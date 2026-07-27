// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

namespace ibrt::renderappearance {

struct LinearRgb
{
  float r;
  float g;
  float b;
};

// OSPRay must render the same opaque background that the viewport clears to.
// A transparent colored background contaminates partially covered silhouette
// samples before Qt composites them a second time.
inline constexpr LinearRgb kViewportBackground{0.1f, 0.1f, 0.12f};
inline constexpr LinearRgb kReferenceBackground{1.0f, 1.0f, 1.0f};

} // namespace ibrt::renderappearance
