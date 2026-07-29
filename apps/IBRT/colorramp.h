// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Value -> color ramp shared by the (planned) ray-shading visualization modes
// and their on-screen legend (see docs/color-plugin-integration-plan.md §4 and
// improvement-plan item #48). Having one dependency-free definition means the
// brl_cad plugin (region tint), the backend (cell plot), and the ImGui legend
// all agree on the exact bin boundaries and colors instead of each hard-coding
// their own table.
//
// The ramp maps a normalized fraction in [0,1] to an RGB color using the
// standard 11-bin BRL-CAD cell-viewer palette. Values outside the selected
// [min,max] range map to a distinct out-of-range color so bad data is obvious.
// Colors pack to the framebuffer's byte-ordered RGBA uint32 (OSP_FB_SRGBA),
// i.e. the least-significant byte is red.

#pragma once

#include <array>
#include <cstdint>

namespace ibrt::colorramp {

struct Rgb
{
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// One legend row: the color and the half-open fraction band [lo, hi) it covers.
struct LegendEntry
{
  float lo = 0.0f;
  float hi = 0.0f;
  Rgb color;
};

// Packs an RGBA color into a byte-ordered RGBA uint32 (matches OSP_FB_SRGBA:
// byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A).
inline std::uint32_t packRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
    std::uint8_t a = 255)
{
  return std::uint32_t(r) | (std::uint32_t(g) << 8) | (std::uint32_t(b) << 16)
      | (std::uint32_t(a) << 24);
}

inline std::uint32_t packRgba(const Rgb &c, std::uint8_t a = 255)
{
  return packRgba(c.r, c.g, c.b, a);
}

// The color shown for values that fall outside the selected [min,max] range.
inline constexpr Rgb kOutOfRangeColor{255, 0, 255}; // magenta

// The standard 11-bin palette and its half-open fraction bands. Bands are
// narrow (0.05) at the extremes and 0.10 in the middle, matching the reference
// cell viewer.
inline constexpr std::array<LegendEntry, 11> kRamp11{{
    {0.00f, 0.05f, {105, 105, 105}}, // dim grey
    {0.05f, 0.15f, {176, 196, 222}}, // light steel blue
    {0.15f, 0.25f, {0, 0, 128}},     // navy
    {0.25f, 0.35f, {65, 105, 225}},  // royal blue
    {0.35f, 0.45f, {127, 255, 212}}, // aquamarine
    {0.45f, 0.55f, {0, 100, 0}},     // dark green
    {0.55f, 0.65f, {50, 205, 50}},   // lime green
    {0.65f, 0.75f, {255, 255, 0}},   // yellow
    {0.75f, 0.85f, {255, 165, 0}},   // orange
    {0.85f, 0.95f, {240, 128, 128}}, // light coral
    {0.95f, 1.00f, {255, 0, 0}},     // red
}};

// A coarser 3-bin alternate (low/mid/high), useful for pass/warn/fail style
// visualization.
inline constexpr std::array<LegendEntry, 3> kRamp3{{
    {0.00f, 1.0f / 3.0f, {0, 0, 255}}, // blue
    {1.0f / 3.0f, 2.0f / 3.0f, {0, 255, 0}}, // green
    {2.0f / 3.0f, 1.00f, {255, 0, 0}}, // red
}};

enum class RampKind
{
  Bins11,
  Bins3
};

// Selects the color for a normalized fraction t in [0,1]. t is clamped, and the
// half-open bands are matched so t == 1.0 lands in the final (top) band.
inline Rgb rampColorNormalized(float t, RampKind kind = RampKind::Bins11)
{
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;

  const auto pick = [t](const auto &table) -> Rgb {
    for (const LegendEntry &e : table) {
      if (t < e.hi)
        return e.color;
    }
    return table.back().color; // t == 1.0 falls through to the top band
  };

  return kind == RampKind::Bins11 ? pick(kRamp11) : pick(kRamp3);
}

// Maps a raw value in [min,max] to a normalized fraction in [0,1]. Returns a
// value < 0 or > 1 for out-of-range inputs so callers can detect them; a
// degenerate range (max <= min) collapses to 0.
inline float normalize(double value, double min, double max)
{
  if (max <= min)
    return 0.0f;
  return float((value - min) / (max - min));
}

// One-call value -> packed RGBA using the given range and ramp. Values outside
// [min,max] return the out-of-range color.
inline std::uint32_t colorForValue(double value, double min, double max,
    RampKind kind = RampKind::Bins11, std::uint8_t alpha = 255)
{
  if (value < min || value > max)
    return packRgba(kOutOfRangeColor, alpha);
  return packRgba(rampColorNormalized(normalize(value, min, max), kind), alpha);
}

} // namespace ibrt::colorramp
