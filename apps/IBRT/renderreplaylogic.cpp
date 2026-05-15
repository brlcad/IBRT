// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "renderreplaylogic.h"

#include <algorithm>

namespace ibrt::renderreplay {

ReplayPlan buildReplayPlan(const ReplayInputs &inputs)
{
  ReplayPlan plan;
  if (!inputs.usingWorkerRenderPath)
    return plan;

  plan.shouldReplay = true;
  plan.width = std::max(1, inputs.width);
  plan.height = std::max(1, inputs.height);
  plan.renderer = inputs.renderer;
  plan.shouldSyncCamera = true;
  plan.shouldResetAccumulation = true;
  plan.shouldRenderOnce = true;

  if (inputs.currentSceneIsObj && !inputs.currentModelPath.isEmpty()) {
    plan.sceneType = SceneReplayType::Obj;
    plan.scenePath = inputs.currentModelPath;
  } else if (!inputs.currentBrlcadPath.isEmpty()) {
    plan.sceneType = SceneReplayType::Brlcad;
    plan.scenePath = inputs.currentBrlcadPath;
    plan.brlcadObjectName = inputs.currentBrlcadObject.trimmed().isEmpty()
        ? QStringLiteral("all")
        : inputs.currentBrlcadObject.trimmed();
  }

  return plan;
}

} // namespace ibrt::renderreplay
