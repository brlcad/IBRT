// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace ibrt::renderworkflow {

enum class RebuildAction
{
  None,
  RestartWorker,
  ReloadObj,
  ReloadBrlcad,
  ResetViewOnly
};

struct RebuildInputs
{
  bool sceneLoadInProgress = false;
  bool usingWorkerRenderPath = false;
  bool hasConnectedWorker = false;
  bool currentSceneIsObj = false;
  QString currentModelPath;
  QString currentBrlcadPath;
  QString currentBrlcadObject;
};

struct RebuildDecision
{
  RebuildAction action = RebuildAction::None;
  QString brlcadObjectName;
  bool shouldResetView = false;
};

bool shouldPreemptWorkerControl(bool usingWorkerRenderPath, float busySeconds);
bool shouldPreemptWorkerInteractiveCamera(bool usingWorkerRenderPath, float busySeconds);
RebuildDecision decideRebuildAction(const RebuildInputs &inputs);

} // namespace ibrt::renderworkflow
