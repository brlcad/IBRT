// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ibrt::cellplot {

struct Vec3
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Ray
{
  Vec3 origin;
  Vec3 direction;
};

struct Camera
{
  Vec3 position;
  Vec3 forward{0.0, 0.0, -1.0};
  Vec3 up{0.0, 1.0, 0.0};
  double verticalFovDegrees = 60.0;
  double aspectRatio = 1.0;
  double focusDistance = 1.0;
  bool orthographic = false;
};

struct Rgba
{
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
};

inline Vec3 operator+(const Vec3 &a, const Vec3 &b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(const Vec3 &a, const Vec3 &b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(const Vec3 &v, double scale)
{
  return {v.x * scale, v.y * scale, v.z * scale};
}

inline double dot(const Vec3 &a, const Vec3 &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalized(const Vec3 &v)
{
  const double length = std::sqrt(dot(v, v));
  if (length <= 1e-15)
    return {0.0, 0.0, 0.0};
  return v * (1.0 / length);
}

// Constructs the ray through the center of one grid cell. Row zero is the top
// of the displayed image, matching QImage/QPainter coordinates.
inline Ray rayForCell(
    const Camera &camera, int column, int row, int columns, int rows)
{
  const int safeColumns = std::max(columns, 1);
  const int safeRows = std::max(rows, 1);
  const double xNdc = 2.0 * (double(column) + 0.5) / double(safeColumns) - 1.0;
  const double yNdc = 1.0 - 2.0 * (double(row) + 0.5) / double(safeRows);

  const Vec3 forward = normalized(camera.forward);
  Vec3 right = normalized(cross(forward, camera.up));
  if (dot(right, right) <= 1e-15)
    right = {1.0, 0.0, 0.0};
  const Vec3 up = normalized(cross(right, forward));
  const double halfFovRadians =
      std::clamp(camera.verticalFovDegrees, 0.01, 179.0)
      * 3.14159265358979323846 / 360.0;
  const double halfHeight =
      std::max(camera.focusDistance, 1e-9) * std::tan(halfFovRadians);
  const double halfWidth = halfHeight * std::max(camera.aspectRatio, 1e-9);
  const Vec3 offset = right * (xNdc * halfWidth) + up * (yNdc * halfHeight);

  if (camera.orthographic)
    return {camera.position + offset, forward};

  return {camera.position,
      normalized(
          forward + offset * (1.0 / std::max(camera.focusDistance, 1e-9)))};
}

// SplitMix64 turns a stable BRL-CAD region id into an evenly distributed,
// vivid prototype color. The lookup table is built once when a scene loads.
inline Rgba colorForRegionId(std::int64_t regionId, std::uint8_t alpha = 176)
{
  std::uint64_t z =
      static_cast<std::uint64_t>(regionId) + 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  z ^= z >> 31;

  // Keep channels away from black so the overlay remains legible.
  const auto channel = [z](unsigned shift) {
    return static_cast<std::uint8_t>(64u + ((z >> shift) & 0x7fu));
  };
  return {channel(0), channel(8), channel(16), alpha};
}

} // namespace ibrt::cellplot
