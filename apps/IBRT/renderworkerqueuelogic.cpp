// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "renderworkerqueuelogic.h"

#include <algorithm>

namespace ibrt::renderworkerqueue {

void queueResize(PendingCommands &commands, int width, int height)
{
  commands.resize = true;
  commands.width = std::max(1, width);
  commands.height = std::max(1, height);
}

void queueCamera(PendingCommands &commands,
    const rkcommon::math::vec3f &eye,
    const rkcommon::math::vec3f &center,
    const rkcommon::math::vec3f &up,
    float fovyDeg)
{
  commands.camera = true;
  commands.eye = eye;
  commands.center = center;
  commands.up = up;
  commands.fovyDeg = fovyDeg;
}

void queueResetAccumulation(PendingCommands &commands)
{
  commands.resetAccumulation = true;
}

void queueRenderer(PendingCommands &commands, const QString &rendererType)
{
  commands.renderer = true;
  commands.rendererType = rendererType;
}

void queueInteracting(PendingCommands &commands, bool interacting)
{
  commands.interacting = true;
  commands.interactingState = interacting;
}

void queueSettings(PendingCommands &commands,
    const RenderWorkerClient::RenderSettingsState &settings)
{
  commands.settings = true;
  commands.settingsState = settings;
}

PendingCommands drain(PendingCommands &commands)
{
  PendingCommands drained = commands;
  commands.resize = false;
  commands.camera = false;
  commands.resetAccumulation = false;
  commands.renderer = false;
  commands.interacting = false;
  commands.settings = false;
  return drained;
}

} // namespace ibrt::renderworkerqueue
