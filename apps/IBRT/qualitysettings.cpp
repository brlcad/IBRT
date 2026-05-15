// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "qualitysettings.h"

namespace ibrt::qualitysettings {

void seedCustomSettingsFromAutomatic(
    RenderWorkerClient::RenderSettingsState &settings)
{
  const RenderWorkerClient::RenderSettingsState defaultSettings;

  settings.customTargetFrameTimeMs = settings.automaticTargetFrameTimeMs;
  settings.customAccumulationEnabled = settings.automaticAccumulationEnabled;

  switch (settings.automaticPreset) {
  case 0:
    settings.customStartScale = 16;
    settings.customAoSamples = 0;
    settings.customPixelSamples = 1;
    break;
  case 2:
    settings.customStartScale = 4;
    settings.customAoSamples = 2;
    settings.customPixelSamples = 2;
    break;
  case 1:
  default:
    settings.customStartScale = 8;
    settings.customAoSamples = 1;
    settings.customPixelSamples = 1;
    break;
  }

  settings.customAoDistance = defaultSettings.customAoDistance;
  settings.customMaxPathLength = defaultSettings.customMaxPathLength;
  settings.customRoulettePathLength = defaultSettings.customRoulettePathLength;
  settings.customMaxAccumulationFrames = 0;
  settings.customLowQualityWhileInteracting = false;
  settings.customFullResAccumulationOnly = true;
  settings.customWatchdogTimeoutMs = defaultSettings.customWatchdogTimeoutMs;
}

void seedBackendCustomSettingsFromAutomatic(OsprayBackend &backend)
{
  const RenderWorkerClient::RenderSettingsState defaultSettings;
  const auto preset = backend.automaticPreset();

  if (preset == OsprayBackend::AutomaticPreset::Fast) {
    backend.setCustomStartScale(16);
    backend.setAoSamples(0);
    backend.setPixelSamples(1);
  } else if (preset == OsprayBackend::AutomaticPreset::Quality) {
    backend.setCustomStartScale(4);
    backend.setAoSamples(2);
    backend.setPixelSamples(2);
  } else {
    backend.setCustomStartScale(8);
    backend.setAoSamples(1);
    backend.setPixelSamples(1);
  }

  backend.setAoDistance(defaultSettings.customAoDistance);
  backend.setMaxPathLength(defaultSettings.customMaxPathLength);
  backend.setRoulettePathLength(defaultSettings.customRoulettePathLength);
  backend.setCustomTargetFrameTimeMs(backend.automaticTargetFrameTimeMs());
  backend.setCustomAccumulationEnabled(backend.automaticAccumulationEnabled());
  backend.setCustomMaxAccumulationFrames(0);
  backend.setCustomLowQualityWhileInteracting(false);
  backend.setCustomFullResAccumulationOnly(true);
  backend.setCustomWatchdogTimeoutMs(defaultSettings.customWatchdogTimeoutMs);
}

void mirrorBackendSettingsToWorkerState(const OsprayBackend &backend,
    RenderWorkerClient::RenderSettingsState &settings)
{
  settings.settingsMode =
      backend.settingsMode() == OsprayBackend::SettingsMode::Automatic ? 0 : 1;
  settings.automaticPreset =
      backend.automaticPreset() == OsprayBackend::AutomaticPreset::Fast
      ? 0
      : (backend.automaticPreset() == OsprayBackend::AutomaticPreset::Balanced ? 1 : 2);
  settings.automaticTargetFrameTimeMs = backend.automaticTargetFrameTimeMs();
  settings.automaticAccumulationEnabled = backend.automaticAccumulationEnabled();
  settings.customStartScale = backend.customStartScale();
  settings.customTargetFrameTimeMs = backend.customTargetFrameTimeMs();
  settings.customAoSamples = backend.customAoSamples();
  settings.customAoDistance = backend.customAoDistance();
  settings.customPixelSamples = backend.customPixelSamples();
  settings.customMaxPathLength = backend.customMaxPathLength();
  settings.customRoulettePathLength = backend.customRoulettePathLength();
  settings.customAccumulationEnabled = backend.customAccumulationEnabled();
  settings.customMaxAccumulationFrames = backend.customMaxAccumulationFrames();
  settings.customLowQualityWhileInteracting =
      backend.customLowQualityWhileInteracting();
  settings.customFullResAccumulationOnly =
      backend.customFullResAccumulationOnly();
  settings.customWatchdogTimeoutMs = backend.customWatchdogTimeoutMs();
}

} // namespace ibrt::qualitysettings
