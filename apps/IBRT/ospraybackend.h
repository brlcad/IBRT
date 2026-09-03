// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <ospray/ospray_cpp.h>
#include <ospray/ospray_cpp/ext/rkcommon.h>

#include "renderappearance.h"

class OsprayBackend
{
 public:
  // Minimal scene-tree representation exposed to the Qt UI for BRL-CAD object picking.
  struct BrlcadNode
  {
    std::string name;
    bool isCombination = false;
    bool isRegion = false;
    bool isPrimitive = false;
    std::vector<BrlcadNode> children;
  };

  enum class SettingsMode
  {
    Automatic,
    Custom
  };

  enum class AutomaticPreset
  {
    Fast,
    Balanced,
    Quality
  };
  enum class VisualizationMode { Solid, Wireframe };
  enum class EdgeRenderMode { Disabled, Overlay, FlatFill };

  // Camera projection. Perspective is the default; Orthographic uses a parallel
  // projection sized to preserve the perspective framing at the pivot plane.
  enum class ProjectionMode { Perspective, Orthographic };

  OsprayBackend() = default;

  // Backend lifecycle and render loop.
  void init();
  void resize(int w, int h);
  const std::string &currentRenderer() const;
  void setCamera(const rkcommon::math::vec3f &eye,
      const rkcommon::math::vec3f &center,
      const rkcommon::math::vec3f &up,
      float fovyDeg);

  void resetAccumulation();
  void cancelRender();
  bool advanceRender(int timeBudgetMs = 2);
  const uint32_t *pixels() const;

  bool loadObj(const std::string &path);
  bool loadBrlcad(const std::string &path, const std::string &topObject = "");
  void setVisualizationMode(VisualizationMode mode);
  VisualizationMode visualizationMode() const;
  void setEdgeRenderMode(EdgeRenderMode mode);
  EdgeRenderMode edgeRenderMode() const;
  void setEdgeColor(const rkcommon::math::vec3f &color);
  rkcommon::math::vec3f edgeColor() const;
  void setFlatFillColor(const rkcommon::math::vec3f &color);
  rkcommon::math::vec3f flatFillColor() const;
  // Switches perspective/orthographic projection. The change is applied between
  // frames (the OSPRay camera object is recreated with the new type).
  void setProjectionMode(ProjectionMode mode);
  ProjectionMode projectionMode() const;
  // Whether the path-tracer sky/sun environment is drawn behind the scene.
  // Disabling it leaves illumination intact but shows the background color
  // (white) where rays escape - useful for clean offline stills.
  void setEnvironmentVisible(bool visible);
  bool environmentVisible() const;
  std::vector<std::string> listBrlcadObjects(const std::string &path) const;
  std::vector<BrlcadNode> getBrlcadHierarchy(const std::string &path) const;
  std::vector<BrlcadNode> listBrlcadHierarchy(const std::string &path) const;
  void loadTestMesh();

  rkcommon::math::vec3f getBoundsCenter() const;
  float getBoundsRadius() const;

  rkcommon::math::vec3f getBoundsMin() const;
  rkcommon::math::vec3f getBoundsMax() const;
  float getBoundsMaxExtent() const;

  void setRenderer(const std::string &type);
  void setOpaqueBackgroundColor(const rkcommon::math::vec3f &color);
  void setWorldUp(const rkcommon::math::vec3f &up);
  rkcommon::math::vec3f worldUp() const;
  void setAoSamples(int samples);
  void setAoDistance(float distance);
  void setPixelSamples(int samples);
  void setMaxPathLength(int depth);
  void setRoulettePathLength(int depth);

  // Dynamic-quality controls used by the viewport and worker UI.
  void setSettingsMode(SettingsMode mode);
  SettingsMode settingsMode() const;

  void setAutomaticPreset(AutomaticPreset preset);
  AutomaticPreset automaticPreset() const;
  void setAutomaticTargetFrameTimeMs(float ms);
  float automaticTargetFrameTimeMs() const;
  void setAutomaticAccumulationEnabled(bool enabled);
  bool automaticAccumulationEnabled() const;

  void setCustomStartScale(int scale);
  int customStartScale() const;
  void setCustomTargetFrameTimeMs(float ms);
  float customTargetFrameTimeMs() const;
  int customAoSamples() const;
  float customAoDistance() const;
  int customPixelSamples() const;
  int customMaxPathLength() const;
  int customRoulettePathLength() const;
  void setCustomAccumulationEnabled(bool enabled);
  bool customAccumulationEnabled() const;
  void setCustomMaxAccumulationFrames(int frames);
  int customMaxAccumulationFrames() const;
  void setCustomLowQualityWhileInteracting(bool enabled);
  bool customLowQualityWhileInteracting() const;
  void setCustomFullResAccumulationOnly(bool enabled);
  bool customFullResAccumulationOnly() const;
  void setCustomWatchdogTimeoutMs(int ms);
  int customWatchdogTimeoutMs() const;

  // Denoising is a global toggle (independent of Automatic/Custom quality mode):
  // when enabled the full-resolution accumulation buffer gains albedo/normal
  // guide channels and an OIDN denoiser image operation.
  void setDenoiseEnabled(bool enabled);
  bool denoiseEnabled() const;

  void setInteracting(bool interacting);

  const std::string &lastError() const;
  uint64_t accumulatedFrames() const;
  uint64_t watchdogCancelCount() const;
  uint64_t aoAutoReductionCount() const;
  int currentScale() const;
  bool dynamicModeActive() const;
  bool backoffApplied() const;

  int width() const
  {
    return fbW_;
  }
  int height() const
  {
    return fbH_;
  }
    
  int& getAoSamples();

 

  float lastFrameTimeMs() const;
  float renderFPS() const;
  size_t debugSceneInstanceCount() const;

 private:
  // Camera edits may arrive while a frame is still rendering. Pending state is
  // buffered here and applied between completed passes.
  struct PendingCameraState
  {
    rkcommon::math::vec3f eye{0.f, 0.f, 1.f};
    rkcommon::math::vec3f center{0.f, 0.f, 0.f};
    rkcommon::math::vec3f up{0.f, 1.f, 0.f};
    float fovyDeg = 60.f;
  };

  enum class RenderPhase
  {
    Progressive,
    Accumulate
  };

  enum class RenderRequestType
  {
    Preview,
    Progressive,
    Full
  };

  struct RenderRequest
  {
    uint64_t id = 0;
    uint64_t cameraVersion = 0;
    RenderRequestType type = RenderRequestType::Progressive;
  };

  void setError(std::string message);
  void cancelInFlightFrame(const char *reason = "preempted");
  RenderRequestType currentRenderRequestType() const;
  const char *renderRequestTypeName(RenderRequestType type) const;
  void logRenderRequest(const char *event,
      const RenderRequest &request,
      const char *reason = nullptr) const;
  void enqueueLatestRenderRequest(const char *reason);
  void setProgressiveScale(int scale);
  void resetProgressiveState(bool clearDisplay = false);
  void updateCameraCrop(const rkcommon::math::vec2f &imageStart,
      const rkcommon::math::vec2f &imageEnd);
  bool startNextRenderWork();
  bool finishCompletedRender();
  void beginNextProgressivePass();
  void prepareTileFrameBuffer(int tileW, int tileH);
  void rebuildAccumFrameBuffer();
  int edgeFrameBufferChannels() const;
  void applyEdgeRendering(ospray::cpp::FrameBuffer &frameBuffer,
      uint32_t *pixels,
      int width,
      int height);
  // Applies the latest camera pose (cameraState_) to camera_, choosing the
  // projection-specific parameter (perspective "fovy" vs orthographic "height").
  void applyCameraParams();
  // Recreates camera_ with the OSPRay camera type matching projectionMode_ and
  // re-applies the current pose. Used when the projection mode changes.
  void rebuildCameraForProjection();
  void upsamplePassToDisplay();
  void applyAoBackoff(bool forcedByWatchdog);
  void applyPendingState();
  void applyDefaultLights();
  void applyRendererDefaults();
  void applyDefaultMaterial(ospray::cpp::GeometricModel &model);
  void applyWorldInstances();
  bool loadBrlcadWireframe(const std::string &path, const std::string &topObject);

  int fbW_ = 1;
  int fbH_ = 1;

  rkcommon::math::vec3f boundsMin_{0.f, 0.f, 0.f};
  rkcommon::math::vec3f boundsMax_{0.f, 0.f, 0.f};

  ospray::cpp::Renderer renderer_;
  ospray::cpp::Camera camera_;
  ospray::cpp::World world_;
  std::vector<ospray::cpp::Instance> sceneInstances_;
  ospray::cpp::FrameBuffer passFb_;
  ospray::cpp::FrameBuffer accumFb_;
  ospray::cpp::Future currentFrame_;

  std::vector<uint32_t> displayPixels_;
  std::vector<uint32_t> passPixels_;
  // Snapshot of the last ambient-occlusion-free progressive frame, used as the
  // base for the crossfade into the accumulating (AO-enabled) result.
  std::vector<uint32_t> crossfadePixels_;
  // Number of accumulation frames blended so far since the last settle.
  int accumBlendFrame_ = 0;
  std::string lastError_;
  float lastFrameTimeMs_ = 0.0f;
  std::string currentRenderer_ = "scivis";
  rkcommon::math::vec3f worldUp_{0.f, 0.f, 1.f};
  rkcommon::math::vec3f backgroundColor_{
      ibrt::renderappearance::kViewportBackground.r,
      ibrt::renderappearance::kViewportBackground.g,
      ibrt::renderappearance::kViewportBackground.b};
  VisualizationMode visualizationMode_ = VisualizationMode::Solid;
  EdgeRenderMode edgeRenderMode_ = EdgeRenderMode::Disabled;
  rkcommon::math::vec3f edgeColor_{0.0f, 0.0f, 0.0f};
  rkcommon::math::vec3f flatFillColor_{0.78f, 0.78f, 0.78f};
  uint64_t accumulatedFrames_ = 0;
  static constexpr int kMaxSafeAoSamples = 32;
  static constexpr int kMaxSafePixelSamples = 64;

  RenderPhase renderPhase_ = RenderPhase::Progressive;
  bool frameInFlight_ = false;
  int currentScaleIndex_ = 0;
  int passScale_ = 1;
  int passW_ = 1;
  int passH_ = 1;
  int slowPassStreak_ = 0;
  int slowFrameStreak_ = 0;
  bool cameraDirty_ = false;
  int appliedAoSamples_ = -1;
  int appliedPixelSamples_ = -1;
  float appliedAoDistance_ = -1.0f;
  int appliedMaxPathLength_ = -1;
  int appliedRoulettePathLength_ = -1;
  uint64_t watchdogCancelCount_ = 0;
  uint64_t aoAutoReductionCount_ = 0;
  std::chrono::steady_clock::time_point inFlightStart_;
  bool inFlightStartValid_ = false;
  static constexpr std::array<int, 5> kProgressiveScales{{16, 8, 4, 2, 1}};
  static constexpr int kDefaultWatchdogMs = 1500;
  static constexpr int kAoBackoffStreak = 3;
  // Number of accumulation frames over which the first full-resolution result
  // is crossfaded in. Kept short: it only softens the step from the last coarse
  // AO pass to full resolution, then hands off to the live accumulation buffer
  // so its natural convergence stays visible rather than being masked.
  static constexpr int kAccumBlendFrames = 4;

  bool watchdogTriggered_ = false;
  bool denoiserModuleAvailable_ = false;
  bool dynamicModeActive_ = false;
  bool backoffApplied_ = false;
  bool isInteracting_ = false;
  int aoBackoffSteps_ = 0;
  int progressiveFramesAtCurrentScale_ = 0;
  uint64_t cameraVersion_ = 0;
  uint64_t nextRenderRequestId_ = 1;
  std::optional<RenderRequest> pendingRenderRequest_;
  std::optional<RenderRequest> activeRenderRequest_;
  std::optional<PendingCameraState> pendingCameraState_;
  // The most recently applied camera pose, retained so the camera can be
  // re-parameterized (e.g. on a projection-mode change) without waiting for the
  // next SetCamera.
  PendingCameraState cameraState_{};
  std::optional<std::string> pendingRendererType_;
  bool pendingResetAccumulation_ = false;
  int pendingResizeW_ = 1;
  int pendingResizeH_ = 1;
  bool pendingResize_ = false;
  // Deferred rebuild of accumFb_ (e.g. after the denoiser toggle changes which
  // channels / image operations the accumulation buffer needs).
  bool pendingAccumRebuild_ = false;
  bool pendingPassFrameBufferRebuild_ = false;
  bool edgeFrameBuffersReady_ = false;
  bool denoiseEnabled_ = true;
  // Deferred recreation of camera_ after a projection-mode change (the OSPRay
  // camera type is fixed at construction, so switching requires a rebuild).
  bool pendingProjectionRebuild_ = false;
  ProjectionMode projectionMode_ = ProjectionMode::Perspective;
  bool environmentVisible_ = true;

  SettingsMode settingsMode_ = SettingsMode::Automatic;
  AutomaticPreset automaticPreset_ = AutomaticPreset::Balanced;
  float automaticTargetFrameTimeMs_ = 16.0f;
  bool automaticAccumulationEnabled_ = true;

  int customStartScale_ = 8;
  float customTargetFrameTimeMs_ = 16.0f;
  int customAoSamples_ = 1;
  float customAoDistance_ = 1e20f;
  int customPixelSamples_ = 1;
  int customMaxPathLength_ = 20;
  int customRoulettePathLength_ = 5;
  bool customAccumulationEnabled_ = true;
  int customMaxAccumulationFrames_ = 0;
  bool customLowQualityWhileInteracting_ = true;
  bool customFullResAccumulationOnly_ = true;
  int customWatchdogTimeoutMs_ = kDefaultWatchdogMs;

  int sanitizeScale(int scale) const;
  int scaleToIndex(int scale) const;
  int startScaleForCurrentMode() const;
  float targetFrameTimeForCurrentMode() const;
  bool accumulationEnabledForCurrentMode() const;
  int maxAccumulationFramesForCurrentMode() const;
  int watchdogTimeoutForCurrentMode() const;
  int configuredAoSamplesForCurrentMode() const;
  float configuredAoDistanceForCurrentMode() const;
  int configuredPixelSamplesForCurrentMode() const;
  int configuredMaxPathLengthForCurrentMode() const;
  int configuredRoulettePathLengthForCurrentMode() const;
  bool fullResAccumulationOnlyForCurrentMode() const;
  bool lowQualityWhileInteractingForCurrentMode() const;
  void applyRendererSamplingParams(int aoSamples,
      float aoDistance,
      int pixelSamples,
      int maxPathLength,
      int roulettePathLength);
};
