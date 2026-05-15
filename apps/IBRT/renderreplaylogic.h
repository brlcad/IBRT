// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace ibrt::renderreplay {

enum class SceneReplayType
{
  None,
  Obj,
  Brlcad
};

struct ReplayInputs
{
  bool usingWorkerRenderPath = false;
  int width = 1;
  int height = 1;
  QString renderer;
  bool currentSceneIsObj = false;
  QString currentModelPath;
  QString currentBrlcadPath;
  QString currentBrlcadObject;
};

struct ReplayPlan
{
  bool shouldReplay = false;
  int width = 1;
  int height = 1;
  QString renderer;
  SceneReplayType sceneType = SceneReplayType::None;
  QString scenePath;
  QString brlcadObjectName;
  bool shouldSyncCamera = false;
  bool shouldResetAccumulation = false;
  bool shouldRenderOnce = false;
};

ReplayPlan buildReplayPlan(const ReplayInputs &inputs);

} // namespace ibrt::renderreplay
