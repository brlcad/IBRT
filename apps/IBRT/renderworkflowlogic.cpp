// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "renderworkflowlogic.h"

namespace ibrt::renderworkflow {

bool shouldPreemptWorkerControl(bool usingWorkerRenderPath, float busySeconds)
{
  if (!usingWorkerRenderPath)
    return false;
  return busySeconds > 2.0f;
}

bool shouldPreemptWorkerInteractiveCamera(bool usingWorkerRenderPath, float busySeconds)
{
  if (!usingWorkerRenderPath)
    return false;
  return busySeconds > 0.1f;
}

RebuildDecision decideRebuildAction(const RebuildInputs &inputs)
{
  RebuildDecision decision;

  if (inputs.sceneLoadInProgress)
    return decision;

  if (inputs.usingWorkerRenderPath) {
    decision.action = RebuildAction::RestartWorker;
    decision.shouldResetView = inputs.hasConnectedWorker;
    return decision;
  }

  if (inputs.currentSceneIsObj && !inputs.currentModelPath.isEmpty()) {
    decision.action = RebuildAction::ReloadObj;
    decision.shouldResetView = true;
    return decision;
  }

  if (!inputs.currentBrlcadPath.isEmpty()) {
    decision.action = RebuildAction::ReloadBrlcad;
    decision.brlcadObjectName = inputs.currentBrlcadObject.trimmed().isEmpty()
        ? QStringLiteral("all")
        : inputs.currentBrlcadObject.trimmed();
    decision.shouldResetView = true;
    return decision;
  }

  decision.action = RebuildAction::ResetViewOnly;
  decision.shouldResetView = true;
  return decision;
}

} // namespace ibrt::renderworkflow
