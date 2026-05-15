// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "ospraybackend.h"
#include "renderworkerclient.h"

namespace ibrt::qualitysettings {

void seedCustomSettingsFromAutomatic(
    RenderWorkerClient::RenderSettingsState &settings);

void seedBackendCustomSettingsFromAutomatic(OsprayBackend &backend);

void mirrorBackendSettingsToWorkerState(const OsprayBackend &backend,
    RenderWorkerClient::RenderSettingsState &settings);

} // namespace ibrt::qualitysettings
