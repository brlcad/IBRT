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

  // Reset View changes only the camera. Reloading a local scene or restarting
  // and replaying the worker scene repeats BRL-CAD prep work needlessly.
  decision.action = RebuildAction::ResetViewOnly;
  decision.shouldResetView = true;
  return decision;
}

} // namespace ibrt::renderworkflow
