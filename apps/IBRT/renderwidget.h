// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QKeyEvent>
#include <QStringList>
#include <QMouseEvent>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QTimer>
#include <QWheelEvent>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <ospray/ospray_cpp/ext/rkcommon.h>
#include "renderworkerclient.h"
#include "renderworkerqueuelogic.h"
#include "imgui.h"
#include "ospraybackend.h"
#include "interactioncontroller.h"

class RenderWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
  Q_OBJECT

 public:
  // Orbit keeps a pivot around the scene; Fly treats the camera as a free-moving observer.
  enum class InputMode
  {
    Orbit,
    Fly
  };

  enum class ManipulationTarget
  {
    View,
    Object
  };

  enum class UpAxis
  {
    Y,
    Z
  };

  // Mouse gestures can act on the camera or on the loaded object transform.
  ManipulationTarget manipulationTarget_ = ManipulationTarget::View;
  explicit RenderWidget(QWidget *parent = nullptr);
  ~RenderWidget() override;

  // Scene-loading and viewport control API used by MainWindow.
  bool loadModel(const QString &path);
  bool loadBrlcadModel(const QString &path, const QString &topObject = QString());
  QStringList listBrlcadObjects(const QString &path) const;
  std::vector<OsprayBackend::BrlcadNode> brlcadHierarchy(const QString &path) const;
  bool reloadBrlcadObject(const QString &topObject);
  QString lastError() const;
  void resetView();
  void rebuildSceneAndResetView();
  void setInputMode(InputMode mode);
  void setUpAxis(UpAxis axis);
  UpAxis upAxis() const;
  QString currentBrlcadPath() const;
  QString currentBrlcadObject() const;
  QStringList currentBrlcadObjects() const;
  bool hasBrlcadScene() const;
  void setRenderWorkerClient(RenderWorkerClient *client);
  void replayWorkerState();

  void setObjectTransform(const rkcommon::math::affine3f &xfm);
  rkcommon::math::affine3f objectTransform() const;
  rkcommon::math::affine3f objectTransform_{rkcommon::math::one};

 signals:
  void sceneLoadFinished(bool success, const QString &errorMessage);
  void inputModeChanged(RenderWidget::InputMode mode);

 protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void keyReleaseEvent(QKeyEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;
 private:
  // Camera helpers shared by orbit mode, fly mode, and object-manipulation overlays.
  static float fitDistanceFromBounds(float maxExtent, float fovyDeg);
  void syncFlyFromOrbit();
  void syncOrbitFromFly();
  rkcommon::math::vec3f currentCameraPosition() const;
  rkcommon::math::vec3f currentCameraForward() const;
  rkcommon::math::vec3f currentCameraUp() const;
  bool projectWorldToScreen(const rkcommon::math::vec3f &worldPos, QPointF &screenPos) const;
  void drawRotationAxisOverlay(QPainter &p);
  rkcommon::math::vec3f worldUp() const;
  rkcommon::math::vec3f worldForwardReference() const;
  rkcommon::math::vec3f forwardFromAngles(float yaw, float pitch) const;
  void anglesFromForward(
      const rkcommon::math::vec3f &forward, float &yaw, float &pitch) const;
  rkcommon::math::vec3f projectOntoPlane(
      const rkcommon::math::vec3f &v, const rkcommon::math::vec3f &normal) const;
  rkcommon::math::vec3f orbitEyeDirection() const;
  void setOrbitFromEyePosition(const rkcommon::math::vec3f &eye);
  rkcommon::math::vec3f orbitRight() const;
  void alignOrbitUpToReference();
  rkcommon::math::vec3f rotateAroundAxis(
      const rkcommon::math::vec3f &v, const rkcommon::math::vec3f &axis, float angle) const;
  void rotateOrbit(float yawDelta, float pitchDelta);
  float defaultFlyMoveStep() const;
  float flyMoveStep() const;
  void resetFlySpeed();
  float flyMoveStep_ = 0.0f;
  void syncCameraToBackend();
  bool usingWorkerRenderPath() const;
  void resetAccumulationTargets();
  void beginInteraction();
  void scheduleInteractionEnd();
  void finishInteraction();
  bool isMovementKey(int key) const;
  void setMovementKeyState(int key, bool pressed);
  bool anyMovementKeysDown() const;
  rkcommon::math::vec3f sceneBoundsCenter() const;
  float sceneBoundsMaxExtent() const;
  void renderOnce();
  void advanceRender();
  void startAsyncLoad(const std::function<void()> &loader, const QString &statusText);
  void startWorkerPolling();
  void stopWorkerPolling();
  void workerPollingLoop();
  void queueWorkerResize(int w, int h);
  void queueWorkerCameraUpdate(
      const rkcommon::math::vec3f &eye,
      const rkcommon::math::vec3f &center,
      const rkcommon::math::vec3f &up,
      float fovyDeg);
  void queueWorkerResetAccumulation();
  void queueWorkerRenderer(const QString &rendererType);
  void queueWorkerInteracting(bool interacting);
  void queueWorkerSettings(const RenderWorkerClient::RenderSettingsState &settings);
  void applyLatestWorkerFrame();
  float workerBusySeconds() const;
  bool preemptWorkerControlIfBusy();
  void seedCustomSettingsFromCurrentAutomatic();
  void currentViewAngles(float &azimuthDeg, float &elevationDeg) const;
  void mirrorBackendSettingsToWorkerState();
  void restartWorkerAndReplayState();

  static float clampf(float v, float lo, float hi);
  static rkcommon::math::vec3f normalizeVec(const rkcommon::math::vec3f &v);
  static rkcommon::math::vec3f crossVec(
      const rkcommon::math::vec3f &a, const rkcommon::math::vec3f &b);

  void applyViewAction(const InteractionController::Result &result, const QPoint &delta);
  void applyObjectAction(const InteractionController::Result &result, const QPoint &delta);

  OsprayBackend backend_;
  QImage image_;
  QPoint lastMouse_;
  QTimer *renderTimer_ = nullptr;
  QTimer *interactionDebounceTimer_ = nullptr;
  bool backendReady_ = false;
  int renderBudgetMs_ = 6;
  bool interactionActive_ = false;
  static constexpr int kInteractionDebounceMs = 150;

  InputMode inputMode_ = InputMode::Orbit;
  UpAxis upAxis_ = UpAxis::Z;

  // Orbit camera state: spherical coordinates around center_.
  rkcommon::math::vec3f center_{0.f, 0.f, 1.5f};
  float dist_ = 4.0f;
  float orbitTheta_ = 0.3f;
  float orbitPhi_ = 1.77079633f;
  float fovy_ = 60.0f;

  float orbitSpeed_ = 0.01f;
  float panSpeed_ = 0.0025f;
  float zoomFactor_ = 0.9f;

  // Fly camera state: free position plus yaw/pitch orientation.
  rkcommon::math::vec3f flyPos_ = rkcommon::math::vec3f(0.f, 0.f, 5.f);
  float flyYaw_ = 0.f;
  float flyPitch_ = 0.f;

  bool imguiMouseDown_[3] = {false, false, false};
  float imguiMouseWheel_ = 0.0f;
  QPointF imguiMousePos_{0.0, 0.0};
  bool imguiHasFocus_ = false;
  bool imguiVisible_ = true;
  QString currentBrlcadPath_;
  QString currentBrlcadObject_;
  QStringList currentBrlcadObjects_;
  QString currentModelPath_;
  bool currentSceneIsObj_ = false;
  QString currentRenderer_ = QStringLiteral("scivis");
  RenderWorkerClient::RenderSettingsState workerSettings_;
  bool moveForwardKeyDown_ = false;
  bool moveLeftKeyDown_ = false;
  bool moveBackwardKeyDown_ = false;
  bool moveRightKeyDown_ = false;
  bool moveDownKeyDown_ = false;
  bool moveUpKeyDown_ = false;
  rkcommon::math::vec3f sceneBoundsMin_{-1.f, -1.f, -1.f};
  rkcommon::math::vec3f sceneBoundsMax_{1.f, 1.f, 1.f};
  QString lastError_;
  QString loadStatusText_;
  float workerLastFrameTimeMs_ = 0.0f;
  float workerRenderFPS_ = 0.0f;
  int workerCurrentScale_ = 1;
  uint64_t workerAccumulatedFrames_ = 0;
  uint64_t workerWatchdogCancels_ = 0;
  uint64_t workerAoAutoReductions_ = 0;
  std::atomic<bool> sceneLoadInProgress_{false};
  std::thread sceneLoadThread_;
  std::thread workerPollThread_;
  std::atomic<bool> workerPollRunning_{false};
  std::atomic<bool> workerRequestInFlight_{false};
  std::atomic<bool> workerFrameReady_{false};
  std::atomic<bool> workerFrameNotifyPending_{false};
  std::atomic<bool> workerRestartInProgress_{false};
  mutable std::mutex workerStateMutex_;
  QImage pendingWorkerImage_;
  float pendingWorkerFrameTimeMs_ = 0.0f;
  float pendingWorkerRenderFPS_ = 0.0f;
  int pendingWorkerCurrentScale_ = 1;
  uint64_t pendingWorkerAccumulatedFrames_ = 0;
  uint64_t pendingWorkerWatchdogCancels_ = 0;
  uint64_t pendingWorkerAoAutoReductions_ = 0;
  QString pendingWorkerRenderer_;
  std::chrono::steady_clock::time_point workerRequestStart_;
  ibrt::renderworkerqueue::PendingCommands workerPendingCommands_;
  // When connected, heavy rendering and scene loading happen out of process.
  RenderWorkerClient *renderWorkerClient_ = nullptr;
};
