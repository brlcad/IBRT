// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "hiddenlineeffect.h"

#include <memory>
#include <string>
#include <vector>

namespace ibrt::render {

struct HiddenLineVec3
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct HiddenLineCamera
{
  HiddenLineVec3 eye;
  HiddenLineVec3 center;
  HiddenLineVec3 up{0.0f, 1.0f, 0.0f};
  float verticalFovDegrees = 60.0f;
  float aspectRatio = 1.0f;
  bool orthographic = false;
};

// First-hit AOV provider for the hidden-line effect.  It deliberately uses
// librt directly so region IDs and normals have the same meaning as rtedge,
// while the resulting mask remains a renderer-independent compositing layer.
class HiddenLineRenderer
{
 public:
  HiddenLineRenderer();
  ~HiddenLineRenderer();
  HiddenLineRenderer(const HiddenLineRenderer &) = delete;
  HiddenLineRenderer &operator=(const HiddenLineRenderer &) = delete;

  bool loadScene(const std::string &databasePath,
      const std::string &topObject,
      std::string &error);
  void clearScene();
  bool hasScene() const;

  bool renderMask(const HiddenLineCamera &camera,
      int width,
      int height,
      std::vector<std::uint8_t> &mask,
      HiddenLineSettings &settings,
      std::string &error) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ibrt::render
