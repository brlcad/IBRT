// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ibrt::edgerender {

enum class Mode
{
  Disabled,
  Overlay,
  FlatFill
};

struct Color
{
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

inline float clampUnit(float value)
{
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

inline Color clampColor(Color color)
{
  color.r = clampUnit(color.r);
  color.g = clampUnit(color.g);
  color.b = clampUnit(color.b);
  return color;
}

inline std::uint32_t packSrgba(Color color)
{
  color = clampColor(color);
  const auto toByte = [](float value) {
    return static_cast<std::uint32_t>(value * 255.0f + 0.5f);
  };
  return toByte(color.r) | (toByte(color.g) << 8) | (toByte(color.b) << 16)
      | 0xff000000u;
}

inline bool hasSurface(float depth)
{
  return std::isfinite(depth);
}

inline bool hasValidNormal(
    const float *normals, std::size_t normalStride, std::size_t index)
{
  if (!normals || normalStride < 3)
    return false;

  const float *normal = normals + index * normalStride;
  const float lengthSquared =
      normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
  return std::isfinite(lengthSquared) && lengthSquared > 1e-12f;
}

inline bool hasSurface(const float *depths,
    const float *normals,
    std::size_t normalStride,
    std::size_t index)
{
  return (depths && hasSurface(depths[index]))
      || hasValidNormal(normals, normalStride, index);
}

inline bool hasNormalDiscontinuity(const float *normals,
    std::size_t normalStride,
    std::size_t first,
    std::size_t second)
{
  if (!normals || normalStride < 3)
    return false;

  const float *firstNormal = normals + first * normalStride;
  const float *secondNormal = normals + second * normalStride;
  const float firstLengthSquared = firstNormal[0] * firstNormal[0]
      + firstNormal[1] * firstNormal[1] + firstNormal[2] * firstNormal[2];
  const float secondLengthSquared = secondNormal[0] * secondNormal[0]
      + secondNormal[1] * secondNormal[1] + secondNormal[2] * secondNormal[2];
  if (!std::isfinite(firstLengthSquared) || !std::isfinite(secondLengthSquared)
      || firstLengthSquared <= 1e-12f || secondLengthSquared <= 1e-12f) {
    return false;
  }

  const float cosine =
      (firstNormal[0] * secondNormal[0] + firstNormal[1] * secondNormal[1]
          + firstNormal[2] * secondNormal[2])
      / std::sqrt(firstLengthSquared * secondLengthSquared);
  return cosine < 0.91f;
}

inline bool hasDepthDiscontinuity(float firstDepth, float secondDepth)
{
  const float tolerance = std::max(
      0.005f, 0.015f * std::min(std::fabs(firstDepth), std::fabs(secondDepth)));
  return std::fabs(firstDepth - secondDepth) > tolerance;
}

inline bool isEdgePixel(const float *depths,
    const float *normals,
    std::size_t normalStride,
    const std::uint32_t *objectIds,
    int width,
    int height,
    int x,
    int y)
{
  if ((!depths && !normals) || x < 0 || y < 0 || x >= width || y >= height)
    return false;

  const std::size_t index =
      std::size_t(y) * std::size_t(width) + std::size_t(x);
  if (!hasSurface(depths, normals, normalStride, index))
    return false;

  constexpr int offsets[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (const auto &offset : offsets) {
    const int neighborX = x + offset[0];
    const int neighborY = y + offset[1];
    if (neighborX < 0 || neighborY < 0 || neighborX >= width
        || neighborY >= height)
      continue;

    const std::size_t neighbor =
        std::size_t(neighborY) * std::size_t(width) + std::size_t(neighborX);
    if (!hasSurface(depths, normals, normalStride, neighbor)
        || (objectIds && objectIds[index] != objectIds[neighbor])
        || (depths && std::isfinite(depths[index])
            && std::isfinite(depths[neighbor])
            && hasDepthDiscontinuity(depths[index], depths[neighbor]))
        || hasNormalDiscontinuity(normals, normalStride, index, neighbor)) {
      return true;
    }
  }

  return false;
}

inline void composite(std::uint32_t *pixels,
    const float *depths,
    const float *normals,
    std::size_t normalStride,
    const std::uint32_t *objectIds,
    int width,
    int height,
    Mode mode,
    Color edgeColor,
    Color flatFillColor)
{
  if (!pixels || (!depths && !normals) || width <= 0 || height <= 0
      || mode == Mode::Disabled) {
    return;
  }

  const std::uint32_t packedEdgeColor = packSrgba(edgeColor);
  const std::uint32_t packedFlatFillColor = packSrgba(flatFillColor);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index =
          std::size_t(y) * std::size_t(width) + std::size_t(x);
      if (!hasSurface(depths, normals, normalStride, index))
        continue;
      if (isEdgePixel(
              depths, normals, normalStride, objectIds, width, height, x, y))
        pixels[index] = packedEdgeColor;
      else if (mode == Mode::FlatFill)
        pixels[index] = packedFlatFillColor;
    }
  }
}

} // namespace ibrt::edgerender
