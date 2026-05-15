// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <ospray/ospray_cpp/ext/rkcommon.h>

#include "renderworkerclient.h"

namespace ibrt::renderworkerqueue {

struct PendingCommands
{
  bool resize = false;
  int width = 1;
  int height = 1;

  bool camera = false;
  rkcommon::math::vec3f eye{0.f, 0.f, 1.f};
  rkcommon::math::vec3f center{0.f, 0.f, 0.f};
  rkcommon::math::vec3f up{0.f, 1.f, 0.f};
  float fovyDeg = 60.0f;

  bool resetAccumulation = false;

  bool renderer = false;
  QString rendererType;

  bool interacting = false;
  bool interactingState = false;

  bool settings = true;
  RenderWorkerClient::RenderSettingsState settingsState;
};

void queueResize(PendingCommands &commands, int width, int height);
void queueCamera(PendingCommands &commands,
    const rkcommon::math::vec3f &eye,
    const rkcommon::math::vec3f &center,
    const rkcommon::math::vec3f &up,
    float fovyDeg);
void queueResetAccumulation(PendingCommands &commands);
void queueRenderer(PendingCommands &commands, const QString &rendererType);
void queueInteracting(PendingCommands &commands, bool interacting);
void queueSettings(PendingCommands &commands,
    const RenderWorkerClient::RenderSettingsState &settings);
PendingCommands drain(PendingCommands &commands);

} // namespace ibrt::renderworkerqueue
