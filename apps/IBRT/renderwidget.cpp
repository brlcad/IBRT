// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "renderwidget.h"

#include "renderworkerclient.h"
#include "qualitysettings.h"
#include "renderreplaylogic.h"
#include "renderworkflowlogic.h"
#include "renderworkerqueuelogic.h"

#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui_impl_opengl3.h"

#include <QTimer>

using rkcommon::math::vec3f;

// Initializes widget state, timers, and input tracking for progressive rendering.
RenderWidget::RenderWidget(QWidget *parent) : QOpenGLWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);

  // The widget drives progressive rendering with a lightweight UI timer rather
  // than blocking inside paintGL().
  renderTimer_ = new QTimer(this);
  connect(renderTimer_, &QTimer::timeout, this, &RenderWidget::advanceRender);
  renderTimer_->start(16); // progressive render at ~60 Hz

  interactionDebounceTimer_ = new QTimer(this);
  interactionDebounceTimer_->setSingleShot(true);
  connect(interactionDebounceTimer_,
      &QTimer::timeout,
      this,
      &RenderWidget::finishInteraction);

  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  workerRequestStart_ = std::chrono::steady_clock::now();
  workerPendingCommands_.settingsState = workerSettings_;
  startWorkerPolling();
}

// Shuts down background loading and destroys the ImGui/OpenGL state owned by the widget.
RenderWidget::~RenderWidget()
{
  stopWorkerPolling();
  if (sceneLoadThread_.joinable())
    sceneLoadThread_.join();

  makeCurrent();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui::DestroyContext();
  doneCurrent();
}

// Applies a classified interaction gesture to the camera/view state.
void RenderWidget::applyViewAction(
    const InteractionController::Result &result, const QPoint &delta)
{
  using Action = InteractionController::Action;
  using Axis = InteractionController::AxisConstraint;

  if (result.action == Action::None)
    return;

  vec3f forward = currentCameraForward();
  vec3f right = orbitRight();
  vec3f upCam = currentCameraUp();
  // delta is already expressed in interaction coordinates: positive Y is up.
  const int verticalDelta = delta.y();

  if (result.action == Action::Translate) {
    // Free translation pans in camera space; constrained translation moves along
    // world axes so placement stays predictable.
    float sx = float(delta.x()) * panSpeed_ * dist_;
    float sy = float(verticalDelta) * panSpeed_ * dist_;

    vec3f move(0.f, 0.f, 0.f);

    if (result.axis == Axis::Free) {
      move = vec3f(-right.x * sx + upCam.x * sy,
          -right.y * sx + upCam.y * sy,
          -right.z * sx + upCam.z * sy);
    } else {
      float axisDelta = float(delta.x() + verticalDelta) * panSpeed_ * dist_;

      if (result.axis == Axis::X)
        move = vec3f(axisDelta, 0.f, 0.f);
      else if (result.axis == Axis::Y)
        move = vec3f(0.f, axisDelta, 0.f);
      else if (result.axis == Axis::Z)
        move = vec3f(0.f, 0.f, axisDelta);
    }

    center_ = vec3f(center_.x + move.x, center_.y + move.y, center_.z + move.z);
  }

  else if (result.action == Action::Rotate) {
    // Free rotation is orbit camera motion. Axis-constrained rotation spins the
    // eye position around a fixed world axis.
    float dx = delta.x() * orbitSpeed_;
    float dy = verticalDelta * orbitSpeed_;

    if (result.axis == Axis::Free) {
      rotateOrbit(dx, dy);
    } else if (result.axis == Axis::X) {
      const vec3f eye = currentCameraPosition();
      const vec3f offset = rotateAroundAxis(
          vec3f(eye.x - center_.x, eye.y - center_.y, eye.z - center_.z),
          vec3f(1.f, 0.f, 0.f),
          dy);
      setOrbitFromEyePosition(
          vec3f(center_.x + offset.x, center_.y + offset.y, center_.z + offset.z));
      syncFlyFromOrbit();
    } else if (result.axis == Axis::Y) {
      const vec3f eye = currentCameraPosition();
      const vec3f offset = rotateAroundAxis(
          vec3f(eye.x - center_.x, eye.y - center_.y, eye.z - center_.z),
          vec3f(0.f, 1.f, 0.f),
          dx);
      setOrbitFromEyePosition(
          vec3f(center_.x + offset.x, center_.y + offset.y, center_.z + offset.z));
      syncFlyFromOrbit();
    } else if (result.axis == Axis::Z) {
      const vec3f eye = currentCameraPosition();
      const vec3f offset = rotateAroundAxis(
          vec3f(eye.x - center_.x, eye.y - center_.y, eye.z - center_.z),
          vec3f(0.f, 0.f, 1.f),
          dx);
      setOrbitFromEyePosition(
          vec3f(center_.x + offset.x, center_.y + offset.y, center_.z + offset.z));
      syncFlyFromOrbit();
    }
  }

  else if (result.action == Action::Scale) {
    // Scale maps to dolly distance for the view target.
    float amount = float(verticalDelta) * 0.01f;

    float maxExtent = sceneBoundsMaxExtent();
    if (maxExtent < 0.001f)
      maxExtent = 1.0f;

    float minDist = std::max(maxExtent * 1e-8f, 1e-8f);
    float maxDist = std::max(maxExtent * 100.0f, 10.0f);

    // Dragging up moves the view in (smaller distance); dragging down moves out.
    dist_ *= std::pow(1.05f, -amount * 10.0f);
    dist_ = clampf(dist_, minDist, maxDist);
  }

  beginInteraction();
  syncCameraToBackend();
  renderOnce();
}

// Computes a camera distance that frames the scene bounds at the current field of view.
float RenderWidget::fitDistanceFromBounds(float maxExtent, float fovyDeg)
{
  if (maxExtent < 0.001f)
    maxExtent = 1.0f;

  float halfAngle = 0.5f * fovyDeg * 3.14159265f / 180.0f;
  halfAngle = std::max(halfAngle, 0.05f);

  return (0.5f * maxExtent) / std::tan(halfAngle) * 1.3f;
}

// Copies the current orbit camera pose into the fly-camera representation.
void RenderWidget::syncFlyFromOrbit()
{
  const vec3f eye = currentCameraPosition();
  const vec3f forward = currentCameraForward();
  flyPos_ = eye;
  anglesFromForward(forward, flyYaw_, flyPitch_);
}

// Reconstructs orbit camera state from the current fly-camera pose.
void RenderWidget::syncOrbitFromFly()
{
  vec3f eye = flyPos_;
  vec3f target = center_;
  vec3f offset(
      eye.x - target.x, eye.y - target.y, eye.z - target.z);
  const float radius =
      std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);

  // Preserve the existing orbit target when leaving fly mode. If it is
  // degenerate, fall back to the scene center instead of adopting the current
  // fly look direction as a new pivot.
  if (radius <= 1e-4f) {
    center_ = sceneBoundsCenter();
    offset = vec3f(eye.x - center_.x, eye.y - center_.y, eye.z - center_.z);
    const float fallbackRadius =
        std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    if (fallbackRadius <= 1e-4f) {
      const float maxExtent = std::max(sceneBoundsMaxExtent(), 1.0f);
      center_ = vec3f(eye.x, eye.y, eye.z - maxExtent);
    }
  }

  setOrbitFromEyePosition(eye);
}

vec3f RenderWidget::currentCameraPosition() const
{
  if (inputMode_ == InputMode::Orbit) {
    const vec3f eyeDir = orbitEyeDirection();
    return vec3f(center_.x + dist_ * eyeDir.x,
        center_.y + dist_ * eyeDir.y,
        center_.z + dist_ * eyeDir.z);
  }

  return flyPos_;
}

vec3f RenderWidget::currentCameraForward() const
{
  if (inputMode_ == InputMode::Orbit)
    return normalizeVec(vec3f(-orbitEyeDirection().x, -orbitEyeDirection().y, -orbitEyeDirection().z));

  return normalizeVec(forwardFromAngles(flyYaw_, flyPitch_));
}

vec3f RenderWidget::currentCameraUp() const
{
  if (inputMode_ == InputMode::Orbit) {
    const vec3f forward = currentCameraForward();
    vec3f projectedUp = projectOntoPlane(worldUp(), forward);
    projectedUp = normalizeVec(projectedUp);
    if (std::fabs(projectedUp.x) < 1e-6f && std::fabs(projectedUp.y) < 1e-6f
        && std::fabs(projectedUp.z) < 1e-6f) {
      projectedUp =
          normalizeVec(projectOntoPlane(worldForwardReference(), forward));
    }
    return normalizeVec(projectedUp);
  }

  return normalizeVec(worldUp());
}

// Projects a world-space point into widget pixel coordinates for overlay drawing.
bool RenderWidget::projectWorldToScreen(const vec3f &worldPos, QPointF &screenPos) const
{
  const vec3f eye = currentCameraPosition();
  vec3f forward = currentCameraForward();
  vec3f up = currentCameraUp();
  vec3f right = normalizeVec(crossVec(forward, up));
  up = normalizeVec(crossVec(right, forward));

  const vec3f rel(worldPos.x - eye.x, worldPos.y - eye.y, worldPos.z - eye.z);
  const float viewX = rel.x * right.x + rel.y * right.y + rel.z * right.z;
  const float viewY = rel.x * up.x + rel.y * up.y + rel.z * up.z;
  const float viewZ = rel.x * forward.x + rel.y * forward.y + rel.z * forward.z;

  if (viewZ <= 1e-4f)
    return false;

  const float aspect = std::max(1.0f, float(width())) / std::max(1.0f, float(height()));
  const float tanHalfY = std::tan(0.5f * fovy_ * 3.14159265f / 180.0f);
  const float tanHalfX = tanHalfY * aspect;
  if (tanHalfX <= 0.0f || tanHalfY <= 0.0f)
    return false;

  const float ndcX = viewX / (viewZ * tanHalfX);
  const float ndcY = viewY / (viewZ * tanHalfY);

  screenPos.setX((ndcX * 0.5f + 0.5f) * float(width()));
  screenPos.setY((1.0f - (ndcY * 0.5f + 0.5f)) * float(height()));
  return true;
}

// Draws axis guides around the orbit pivot to show constrained rotation context.
void RenderWidget::drawRotationAxisOverlay(QPainter &p)
{
  if (inputMode_ != InputMode::Orbit)
    return;

  QPointF origin;
  if (!projectWorldToScreen(center_, origin))
    return;

  float axisLength = sceneBoundsMaxExtent() * 0.12f;
  axisLength = std::max(axisLength, 0.1f);
  axisLength = std::min(axisLength, std::max(1.0f, dist_ * 0.25f));

  const struct AxisLine
  {
    vec3f dir;
    QColor color;
    const char *label;
  } axes[] = {
      {vec3f(1.f, 0.f, 0.f), QColor(220, 80, 80), "X"},
      {vec3f(0.f, 1.f, 0.f), QColor(80, 210, 110), "Y"},
      {vec3f(0.f, 0.f, 1.f), QColor(80, 140, 255), "Z"},
  };

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setBrush(QColor(245, 245, 245));
  p.setPen(Qt::NoPen);
  p.drawEllipse(origin, 3.5, 3.5);

  for (const auto &axis : axes) {
    QPointF tip;
    const vec3f end(center_.x + axis.dir.x * axisLength,
        center_.y + axis.dir.y * axisLength,
        center_.z + axis.dir.z * axisLength);
    if (!projectWorldToScreen(end, tip))
      continue;

    QPen pen(axis.color, 2.0);
    p.setPen(pen);
    p.drawLine(origin, tip);

    p.setPen(axis.color.lighter(120));
    p.drawText(tip + QPointF(4.0, -4.0), QString::fromLatin1(axis.label));
  }
}

// Clamps a scalar value to the requested numeric range.
float RenderWidget::clampf(float v, float lo, float hi)
{
  return std::max(lo, std::min(hi, v));
}

vec3f RenderWidget::normalizeVec(const vec3f &v)
{
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len <= 1e-8f)
    return vec3f(0.f, 0.f, 1.f);

  return vec3f(v.x / len, v.y / len, v.z / len);
}

vec3f RenderWidget::crossVec(const vec3f &a, const vec3f &b)
{
  return vec3f(
      a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// Sets up the OpenGL and ImGui state used by the widget.
void RenderWidget::initializeGL()
{
  initializeOpenGLFunctions();

  backend_.init();
  backendReady_ = true;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(float(width()), float(height()));

#ifdef __APPLE__
  ImGui_ImplOpenGL3_Init("#version 150");
#else
  ImGui_ImplOpenGL3_Init("#version 130");
#endif
}

// Resizes the render targets to match the widget dimensions.
void RenderWidget::resizeGL(int w, int h)
{
  if (sceneLoadInProgress_.load()) {
    ImGui::GetIO().DisplaySize = ImVec2(float(w), float(h));
    return;
  }

  if (usingWorkerRenderPath())
    queueWorkerResize(w, h);

  backend_.resize(w, h);
  ImGui::GetIO().DisplaySize = ImVec2(float(w), float(h));

  resetView();
}

// Pushes the current camera pose into either the worker or local backend.
void RenderWidget::syncCameraToBackend()
{
  if (inputMode_ == InputMode::Orbit) {
    const vec3f eye = currentCameraPosition();
    const vec3f up = currentCameraUp();
    backend_.setCamera(eye, center_, up, fovy_);
    if (usingWorkerRenderPath()) {
      if (interactionActive_
          && ibrt::renderworkflow::shouldPreemptWorkerInteractiveCamera(
              true, workerBusySeconds())) {
        restartWorkerAndReplayState();
        return;
      }
      queueWorkerCameraUpdate(eye, center_, up, fovy_);
    }
  } else {
    flyPitch_ = clampf(flyPitch_, -1.4f, 1.4f);

    vec3f forward = forwardFromAngles(flyYaw_, flyPitch_);

    backend_.setCamera(flyPos_, flyPos_ + forward, worldUp(), fovy_);
    if (usingWorkerRenderPath()) {
      if (interactionActive_
          && ibrt::renderworkflow::shouldPreemptWorkerInteractiveCamera(
              true, workerBusySeconds())) {
        restartWorkerAndReplayState();
        return;
      }
      queueWorkerCameraUpdate(flyPos_, flyPos_ + forward, worldUp(), fovy_);
    }
  }
}

// Returns true when rendering is delegated to the external worker process.
bool RenderWidget::usingWorkerRenderPath() const
{
  return renderWorkerClient_ && renderWorkerClient_->isConnected();
}

// Clears accumulation state after any camera, renderer, or scene change.
void RenderWidget::resetAccumulationTargets()
{
  backend_.resetAccumulation();
  if (usingWorkerRenderPath())
    queueWorkerResetAccumulation();
}

// Marks the start of an interactive manipulation and lowers render quality if needed.
void RenderWidget::beginInteraction()
{
  if (!interactionActive_) {
    interactionActive_ = true;
    backend_.setInteracting(true);
    if (usingWorkerRenderPath())
      queueWorkerInteracting(true);

    // The worker pipe is synchronous. If a stale heavy frame is already
    // blocking the worker, restart once at interaction start so the latest
    // camera state can replace it instead of waiting for completion.
    if (usingWorkerRenderPath() && workerBusySeconds() > 0.1f)
      restartWorkerAndReplayState();
  }

  interactionDebounceTimer_->start(kInteractionDebounceMs);
}

// Defers the transition out of interaction mode to avoid flickering state changes.
void RenderWidget::scheduleInteractionEnd()
{
  if (!interactionActive_)
    return;
  interactionDebounceTimer_->start(kInteractionDebounceMs);
}

// Ends interaction mode and restores normal progressive rendering behavior.
void RenderWidget::finishInteraction()
{
  if (!interactionActive_)
    return;

  interactionActive_ = false;
  backend_.setInteracting(false);
  if (usingWorkerRenderPath())
    queueWorkerInteracting(false);
}

// Reports whether a key participates in fly-camera movement.
bool RenderWidget::isMovementKey(int key) const
{
  return key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S
      || key == Qt::Key_D || key == Qt::Key_Q || key == Qt::Key_E;
}

// Updates pressed state for the fly-camera movement keys.
void RenderWidget::setMovementKeyState(int key, bool pressed)
{
  switch (key) {
  case Qt::Key_W:
    moveForwardKeyDown_ = pressed;
    break;
  case Qt::Key_A:
    moveLeftKeyDown_ = pressed;
    break;
  case Qt::Key_S:
    moveBackwardKeyDown_ = pressed;
    break;
  case Qt::Key_D:
    moveRightKeyDown_ = pressed;
    break;
  case Qt::Key_Q:
    moveDownKeyDown_ = pressed;
    break;
  case Qt::Key_E:
    moveUpKeyDown_ = pressed;
    break;
  default:
    break;
  }
}

// Returns true when any fly-navigation movement key is currently held.
bool RenderWidget::anyMovementKeysDown() const
{
  return moveForwardKeyDown_ || moveLeftKeyDown_ || moveBackwardKeyDown_
      || moveRightKeyDown_ || moveDownKeyDown_ || moveUpKeyDown_;
}

vec3f RenderWidget::sceneBoundsCenter() const
{
  if (usingWorkerRenderPath()) {
    return vec3f(0.5f * (sceneBoundsMin_.x + sceneBoundsMax_.x),
        0.5f * (sceneBoundsMin_.y + sceneBoundsMax_.y),
        0.5f * (sceneBoundsMin_.z + sceneBoundsMax_.z));
  }
  return backend_.getBoundsCenter();
}

// Computes the maximum scene extent used for view framing and motion scaling.
float RenderWidget::sceneBoundsMaxExtent() const
{
  if (usingWorkerRenderPath()) {
    const float dx = sceneBoundsMax_.x - sceneBoundsMin_.x;
    const float dy = sceneBoundsMax_.y - sceneBoundsMin_.y;
    const float dz = sceneBoundsMax_.z - sceneBoundsMin_.z;
    return std::max(dx, std::max(dy, dz));
  }
  return backend_.getBoundsMaxExtent();
}

// Advances rendering by one step and updates the displayed image if a new frame is ready.
void RenderWidget::renderOnce()
{
  if (!backendReady_ || sceneLoadInProgress_.load())
    return;

  if (usingWorkerRenderPath()) {
    applyLatestWorkerFrame();
    return;
  }

  const bool updatedImage = backend_.advanceRender(renderBudgetMs_);
  const uint32_t *px = backend_.pixels();
  const int w = backend_.width();
  const int h = backend_.height();

  if (!px || w <= 0 || h <= 0)
    return;

  if (image_.width() != w || image_.height() != h)
    image_ = QImage(w, h, QImage::Format_RGBA8888);

  if (updatedImage) {
    std::memcpy(image_.bits(), px, size_t(w) * size_t(h) * 4);
    update();

    const float frameMs = backend_.lastFrameTimeMs();
    if (frameMs < 10.0f)
      renderBudgetMs_ = std::min(10, renderBudgetMs_ + 1);
    else if (frameMs > 22.0f)
      renderBudgetMs_ = std::max(3, renderBudgetMs_ - 1);
  }
}

// Timer callback that keeps progressive rendering moving while the widget is visible.
void RenderWidget::advanceRender()
{
  if (!backendReady_ || !isVisible() || sceneLoadInProgress_.load())
    return;

  renderOnce();
}

// Draws the latest rendered image and the ImGui control overlay.
void RenderWidget::paintGL()
{
  glViewport(0, 0, width() * devicePixelRatioF(), height() * devicePixelRatioF());
  glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Draw OSPRay image with Qt
  QPainter p(this);
  if (!image_.isNull()) {
    // OSPRay's OSP_FB_SRGBA maps to byte-ordered RGBA pixels; only a vertical
    // flip is needed because OSPRay's image origin is the lower-left corner.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    QImage img = image_.flipped(Qt::Vertical);
#else
    QImage img = image_.mirrored(false, true);
#endif
    p.drawImage(rect(), img);
  }
  drawRotationAxisOverlay(p);
  p.end();

  ImGuiIO &io = ImGui::GetIO();

  // IMPORTANT: logical widget coordinates
  io.DisplaySize = ImVec2(float(width()), float(height()));

  // IMPORTANT: framebuffer scale for HiDPI
  const float dpr = float(devicePixelRatioF());
  io.DisplayFramebufferScale = ImVec2(dpr, dpr);

  io.DeltaTime = 1.0f / 60.0f;

  io.AddFocusEvent(imguiHasFocus_);
  io.AddMousePosEvent(float(imguiMousePos_.x()), float(imguiMousePos_.y()));
  io.AddMouseButtonEvent(0, imguiMouseDown_[0]);
  io.AddMouseButtonEvent(1, imguiMouseDown_[1]);
  io.AddMouseButtonEvent(2, imguiMouseDown_[2]);

  if (imguiMouseWheel_ != 0.0f) {
    io.AddMouseWheelEvent(0.0f, imguiMouseWheel_);
    imguiMouseWheel_ = 0.0f;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();

  if (!imguiVisible_) {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  const ImVec2 displaySize = io.DisplaySize;
  const float overlayMargin = 16.0f;
  const float minOverlayWidth = 360.0f;
  const float minOverlayHeight = 320.0f;
  const float maxOverlayWidth =
      std::max(minOverlayWidth, displaySize.x - overlayMargin * 2.0f);
  const float maxOverlayHeight =
      std::max(minOverlayHeight, displaySize.y - overlayMargin * 2.0f);
  const float defaultOverlayWidth = std::min(420.0f, maxOverlayWidth);
  const float defaultOverlayHeight = std::min(680.0f, maxOverlayHeight);

  ImGui::SetNextWindowPos(ImVec2(overlayMargin, overlayMargin), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(
      ImVec2(defaultOverlayWidth, defaultOverlayHeight), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(minOverlayWidth, minOverlayHeight),
      ImVec2(maxOverlayWidth, maxOverlayHeight));

  ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Interactive BRL-CAD Raytracer", nullptr, overlayFlags);
  ImGui::BeginChild("overlay_content", ImVec2(0.0f, 0.0f), false,
      ImGuiWindowFlags_AlwaysVerticalScrollbar);

  if (sceneLoadInProgress_.load()) {
    // While a background load runs, the overlay switches into a simple status
    // panel and rendering remains paused on the last completed frame.
    ImGui::Separator();
    ImGui::Text("Status");
    ImGui::TextWrapped("%s", loadStatusText_.toStdString().c_str());
    const float pulse = 0.2f + 0.8f * (0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 4.0f));
    ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Working...");
    ImGui::Text("Renderer work is paused until the scene load completes.");
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  ImGui::Separator();
  ImGui::Text("Stats");

  ImGui::Text("UI FPS: %.1f", ImGui::GetIO().Framerate);
  if (usingWorkerRenderPath()) {
    ImGui::Text("Render path: Worker");
    ImGui::Text("Render time: %.2f ms", workerLastFrameTimeMs_);
    ImGui::Text("Render FPS: %.3f", workerRenderFPS_);
    ImGui::Text("Current scale: %dx", workerCurrentScale_);
    ImGui::Text("Accumulated frames: %llu",
        static_cast<unsigned long long>(workerAccumulatedFrames_));
    ImGui::Text("AO auto-reductions: %llu",
        static_cast<unsigned long long>(workerAoAutoReductions_));
    const float busySeconds = workerBusySeconds();
    if (workerRestartInProgress_.load()) {
      const float pulse =
          0.2f + 0.8f * (0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 5.0f));
      ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Restarting worker...");
    } else if (busySeconds > 0.25f) {
      const float pulse =
          0.2f + 0.8f * (0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 5.0f));
      ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Worker busy...");
      ImGui::Text("Current request age: %.1f s", busySeconds);
    }
  } else {
    ImGui::Text("Render time: %.2f ms", backend_.lastFrameTimeMs());
    ImGui::Text("Render FPS: %.3f", backend_.renderFPS());
    ImGui::Text("Accumulated frames: %llu",
        static_cast<unsigned long long>(backend_.accumulatedFrames()));
    ImGui::Text("AO auto-reductions: %llu",
        static_cast<unsigned long long>(backend_.aoAutoReductionCount()));
  }
  ImGui::Text("Up Axis: %s", upAxis_ == UpAxis::Z ? "Z" : "Y");
  ImGui::Text("Resolution: %d x %d", width(), height());
  const vec3f statsCenter = sceneBoundsCenter();
  float azimuthDeg = 0.0f;
  float elevationDeg = 0.0f;
  currentViewAngles(azimuthDeg, elevationDeg);
  ImGui::Text("Center: %.3f, %.3f, %.3f", statsCenter.x, statsCenter.y, statsCenter.z);
  ImGui::Text("Azimuth/Elevation: %.2f / %.2f deg", azimuthDeg, elevationDeg);

  ImGui::Separator();
  ImGui::Text("Visualization");
  int visualizationMode = wireframeVisualization_ ? 1 : 0;
  if (ImGui::RadioButton("Solid", visualizationMode == 0)) {
    wireframeVisualization_ = false;
    backend_.setVisualizationMode(OsprayBackend::VisualizationMode::Solid);
    if (!currentBrlcadPath_.isEmpty())
      loadBrlcadModel(currentBrlcadPath_, currentBrlcadObject_);
  }
  if (ImGui::RadioButton("Wireframe", visualizationMode == 1)) {
    wireframeVisualization_ = true;
    backend_.setVisualizationMode(OsprayBackend::VisualizationMode::Wireframe);
    if (!currentBrlcadPath_.isEmpty())
      loadBrlcadModel(currentBrlcadPath_, currentBrlcadObject_);
  }

  ImGui::Separator();
  ImGui::Text("Renderer");

  // Keep the UI selection in sync with whichever render path is active.
  int rendererMode = 0;
  const std::string rendererName =
      usingWorkerRenderPath() ? currentRenderer_.toStdString() : backend_.currentRenderer();
  if (rendererName == "scivis")
    rendererMode = 1;
  else if (rendererName == "pathtracer")
    rendererMode = 2;
  else
    rendererMode = 0;

  const auto applyRendererSelection = [this](const QString &rendererName) {
    currentRenderer_ = rendererName;
    if (usingWorkerRenderPath()) {
      if (workerRequestInFlight_.load()) {
        restartWorkerAndReplayState();
        update();
        return;
      }
      queueWorkerRenderer(currentRenderer_);
      resetAccumulationTargets();
      renderOnce();
    } else {
      backend_.setRenderer(rendererName.toStdString());
      resetAccumulationTargets();
      renderOnce();
    }
    update();
  };

  if (ImGui::RadioButton("ao", rendererMode == 0)) {
    rendererMode = 0;
    applyRendererSelection(QStringLiteral("ao"));
  }
  if (ImGui::RadioButton("SciVis", rendererMode == 1)) {
    rendererMode = 1;
    applyRendererSelection(QStringLiteral("scivis"));
  }
  if (ImGui::RadioButton("PathTracer", rendererMode == 2)) {
    rendererMode = 2;
    applyRendererSelection(QStringLiteral("pathtracer"));
  }

  ImGui::Separator();
  ImGui::Text("Render Settings");
  auto pushWidthForInlineLabel = [](const char *label) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float itemWidth =
        std::max(1.0f, ImGui::GetContentRegionAvail().x - labelWidth - style.ItemInnerSpacing.x);
    ImGui::PushItemWidth(itemWidth);
  };
  bool settingsChanged = false;
  const RenderWorkerClient::RenderSettingsState defaultSettings;
  int settingsMode = usingWorkerRenderPath()
      ? workerSettings_.settingsMode
      : (backend_.settingsMode() == OsprayBackend::SettingsMode::Automatic ? 0 : 1);
  if (ImGui::RadioButton("Automatic", settingsMode == 0)) {
    if (usingWorkerRenderPath())
      workerSettings_.settingsMode = 0;
    else {
      backend_.setSettingsMode(OsprayBackend::SettingsMode::Automatic);
      mirrorBackendSettingsToWorkerState();
    }
    settingsMode = 0;
    settingsChanged = true;
  }
  if (ImGui::RadioButton("Custom", settingsMode == 1)) {
    seedCustomSettingsFromCurrentAutomatic();
    if (usingWorkerRenderPath())
      workerSettings_.settingsMode = 1;
    else {
      backend_.setSettingsMode(OsprayBackend::SettingsMode::Custom);
      mirrorBackendSettingsToWorkerState();
    }
    settingsMode = 1;
    settingsChanged = true;
  }

  if (settingsMode == 0) {
    ImGui::SeparatorText("Automatic");

    int preset = usingWorkerRenderPath() ? workerSettings_.automaticPreset : 1;
    if (!usingWorkerRenderPath()) {
      if (backend_.automaticPreset() == OsprayBackend::AutomaticPreset::Fast)
        preset = 0;
      else if (backend_.automaticPreset() == OsprayBackend::AutomaticPreset::Balanced)
        preset = 1;
      else
        preset = 2;
    }
    const char *presetLabels[] = {"Fast", "Balanced", "Quality"};
    pushWidthForInlineLabel("Preset");
    if (ImGui::Combo("Preset", &preset, presetLabels, IM_ARRAYSIZE(presetLabels))) {
      if (usingWorkerRenderPath()) {
        workerSettings_.automaticPreset = preset;
        seedCustomSettingsFromCurrentAutomatic();
      } else {
        if (preset == 0)
          backend_.setAutomaticPreset(OsprayBackend::AutomaticPreset::Fast);
        else if (preset == 1)
          backend_.setAutomaticPreset(OsprayBackend::AutomaticPreset::Balanced);
        else
          backend_.setAutomaticPreset(OsprayBackend::AutomaticPreset::Quality);
        seedCustomSettingsFromCurrentAutomatic();
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    float targetMs = usingWorkerRenderPath() ? workerSettings_.automaticTargetFrameTimeMs
                                             : backend_.automaticTargetFrameTimeMs();
    pushWidthForInlineLabel("Target Frame Time (ms)");
    if (ImGui::DragFloat("Target Frame Time (ms)", &targetMs, 0.1f, 2.0f, 1000.0f, "%.1f")) {
      if (usingWorkerRenderPath())
        workerSettings_.automaticTargetFrameTimeMs = targetMs;
      else {
        backend_.setAutomaticTargetFrameTimeMs(targetMs);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    bool accumEnabled = usingWorkerRenderPath() ? workerSettings_.automaticAccumulationEnabled
                                                : backend_.automaticAccumulationEnabled();
    if (ImGui::Checkbox("Accumulation", &accumEnabled)) {
      if (usingWorkerRenderPath())
        workerSettings_.automaticAccumulationEnabled = accumEnabled;
      else {
        backend_.setAutomaticAccumulationEnabled(accumEnabled);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }

    const bool workerBusy = usingWorkerRenderPath() && workerBusySeconds() > 0.5f;
    if (ImGui::Button(workerBusy ? "Restart Worker" : "Reset Render")) {
      if (workerBusy || preemptWorkerControlIfBusy())
        restartWorkerAndReplayState();
      else
        resetAccumulationTargets();
      settingsChanged = !workerBusy;
    }
  } else {
    ImGui::SeparatorText("Custom");

    const int effectiveScale =
        usingWorkerRenderPath() ? workerCurrentScale_ : backend_.currentScale();
    int startScale = usingWorkerRenderPath() ? workerSettings_.customStartScale
                                             : backend_.customStartScale();
    pushWidthForInlineLabel("Configured Start Scale");
    if (ImGui::SliderInt("Configured Start Scale", &startScale, 1, 16)) {
      if (usingWorkerRenderPath())
        workerSettings_.customStartScale = startScale;
      else {
        backend_.setCustomStartScale(startScale);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();
    ImGui::Text("Effective current scale: %dx", effectiveScale);

    float targetMs = usingWorkerRenderPath() ? workerSettings_.customTargetFrameTimeMs
                                             : backend_.customTargetFrameTimeMs();
    pushWidthForInlineLabel("Target Frame Time (ms)");
    if (ImGui::DragFloat("Target Frame Time (ms)", &targetMs, 0.1f, 2.0f, 1000.0f, "%.1f")) {
      if (usingWorkerRenderPath())
        workerSettings_.customTargetFrameTimeMs = targetMs;
      else {
        backend_.setCustomTargetFrameTimeMs(targetMs);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    int aoSamples = usingWorkerRenderPath() ? workerSettings_.customAoSamples
                                            : backend_.customAoSamples();
    pushWidthForInlineLabel("AO Samples");
    if (ImGui::SliderInt("AO Samples", &aoSamples, 0, 32)) {
      if (usingWorkerRenderPath()) {
        workerSettings_.customAoSamples = aoSamples;
      } else {
        backend_.setAoSamples(aoSamples);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    float aoDistance = usingWorkerRenderPath()
        ? workerSettings_.customAoDistance
        : backend_.customAoDistance();

    pushWidthForInlineLabel("AO Distance Limit");
    if (ImGui::DragFloat(
            "AO Distance Limit", &aoDistance, 0.1f, 0.0f, 1000000.0f, "%.1f")) {
      if (usingWorkerRenderPath()) {
        workerSettings_.customAoDistance = aoDistance;
      } else {
        backend_.setAoDistance(aoDistance);
        mirrorBackendSettingsToWorkerState();
      }

      settingsChanged = true;
    }

    ImGui::PopItemWidth();

    int pixelSamples = usingWorkerRenderPath() ? workerSettings_.customPixelSamples
                                               : backend_.customPixelSamples();
    pushWidthForInlineLabel("Pixel Samples");
    if (ImGui::SliderInt("Pixel Samples", &pixelSamples, 1, 64)) {
      if (usingWorkerRenderPath())
        workerSettings_.customPixelSamples = pixelSamples;
      else {
        backend_.setPixelSamples(pixelSamples);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    int maxPathLength = usingWorkerRenderPath() ? workerSettings_.customMaxPathLength
                                                : backend_.customMaxPathLength();
    pushWidthForInlineLabel("Max Depth");
    if (ImGui::SliderInt("Max Depth", &maxPathLength, 0, 64)) {
      if (usingWorkerRenderPath())
        workerSettings_.customMaxPathLength = maxPathLength;
      else {
        backend_.setMaxPathLength(maxPathLength);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    int roulettePathLength = usingWorkerRenderPath()
        ? workerSettings_.customRoulettePathLength
        : backend_.customRoulettePathLength();
    pushWidthForInlineLabel("Early Exit Depth");
    if (ImGui::SliderInt("Early Exit Depth", &roulettePathLength, 0, 64)) {
      if (usingWorkerRenderPath())
        workerSettings_.customRoulettePathLength = roulettePathLength;
      else {
        backend_.setRoulettePathLength(roulettePathLength);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    bool accumEnabled = usingWorkerRenderPath() ? workerSettings_.customAccumulationEnabled
                                                : backend_.customAccumulationEnabled();
    if (ImGui::Checkbox("Accumulation Enabled", &accumEnabled)) {
      if (usingWorkerRenderPath())
        workerSettings_.customAccumulationEnabled = accumEnabled;
      else {
        backend_.setCustomAccumulationEnabled(accumEnabled);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }

    int maxAccumFrames = usingWorkerRenderPath() ? workerSettings_.customMaxAccumulationFrames
                                                 : backend_.customMaxAccumulationFrames();
    pushWidthForInlineLabel("Max Accumulation Frames");
    if (ImGui::InputInt("Max Accumulation Frames", &maxAccumFrames)) {
      if (usingWorkerRenderPath())
        workerSettings_.customMaxAccumulationFrames = maxAccumFrames;
      else {
        backend_.setCustomMaxAccumulationFrames(maxAccumFrames);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }
    ImGui::PopItemWidth();

    bool fullResAccumOnly = usingWorkerRenderPath()
        ? workerSettings_.customFullResAccumulationOnly
        : backend_.customFullResAccumulationOnly();
    if (ImGui::Checkbox("Full-res Accumulation Only", &fullResAccumOnly)) {
      if (usingWorkerRenderPath())
        workerSettings_.customFullResAccumulationOnly = fullResAccumOnly;
      else {
        backend_.setCustomFullResAccumulationOnly(fullResAccumOnly);
        mirrorBackendSettingsToWorkerState();
      }
      settingsChanged = true;
    }

    if (ImGui::Button("Reset Custom Settings")) {
      if (usingWorkerRenderPath()) {
        workerSettings_.customStartScale = defaultSettings.customStartScale;
        workerSettings_.customTargetFrameTimeMs = defaultSettings.customTargetFrameTimeMs;
        workerSettings_.customAoSamples = defaultSettings.customAoSamples;
        workerSettings_.customAoDistance = defaultSettings.customAoDistance;
        workerSettings_.customPixelSamples = defaultSettings.customPixelSamples;
        workerSettings_.customMaxPathLength = defaultSettings.customMaxPathLength;
        workerSettings_.customRoulettePathLength =
            defaultSettings.customRoulettePathLength;
        workerSettings_.customAccumulationEnabled = defaultSettings.customAccumulationEnabled;
        workerSettings_.customMaxAccumulationFrames =
            defaultSettings.customMaxAccumulationFrames;
        workerSettings_.customLowQualityWhileInteracting =
            defaultSettings.customLowQualityWhileInteracting;
        workerSettings_.customFullResAccumulationOnly =
            defaultSettings.customFullResAccumulationOnly;
        workerSettings_.customWatchdogTimeoutMs =
            defaultSettings.customWatchdogTimeoutMs;
      } else {
        backend_.setCustomStartScale(defaultSettings.customStartScale);
        backend_.setCustomTargetFrameTimeMs(defaultSettings.customTargetFrameTimeMs);
        backend_.setAoSamples(defaultSettings.customAoSamples);
        backend_.setAoDistance(defaultSettings.customAoDistance);
        backend_.setPixelSamples(defaultSettings.customPixelSamples);
        backend_.setMaxPathLength(defaultSettings.customMaxPathLength);
        backend_.setRoulettePathLength(defaultSettings.customRoulettePathLength);
        backend_.setCustomAccumulationEnabled(defaultSettings.customAccumulationEnabled);
        backend_.setCustomMaxAccumulationFrames(
            defaultSettings.customMaxAccumulationFrames);
        backend_.setCustomLowQualityWhileInteracting(
            defaultSettings.customLowQualityWhileInteracting);
        backend_.setCustomFullResAccumulationOnly(
            defaultSettings.customFullResAccumulationOnly);
        backend_.setCustomWatchdogTimeoutMs(defaultSettings.customWatchdogTimeoutMs);
        mirrorBackendSettingsToWorkerState();
      }
      resetAccumulationTargets();
      settingsChanged = true;
    }

    ImGui::SameLine();
    const bool workerBusy = usingWorkerRenderPath() && workerBusySeconds() > 0.5f;
    if (ImGui::Button(workerBusy ? "Restart Worker" : "Reset Render")) {
      if (workerBusy || preemptWorkerControlIfBusy())
        restartWorkerAndReplayState();
      else
        resetAccumulationTargets();
      settingsChanged = !workerBusy;
    }
  }

  ImGui::SeparatorText("Diagnostics");
  if (!usingWorkerRenderPath()) {
    ImGui::Text("Current scale: %dx", backend_.currentScale());
    ImGui::Text("Last render time: %.2f ms", backend_.lastFrameTimeMs());
    ImGui::Text("Accumulation frames: %llu",
        static_cast<unsigned long long>(backend_.accumulatedFrames()));
    ImGui::Text("Dynamic mode active: %s",
        backend_.dynamicModeActive() ? "Yes" : "No");
    ImGui::Text("Backoff applied: %s", backend_.backoffApplied() ? "Yes" : "No");
  } else {
    ImGui::Text("Current renderer: %s", currentRenderer_.toStdString().c_str());
  }

  if (settingsChanged) {
    if (usingWorkerRenderPath()) {
      if (!preemptWorkerControlIfBusy())
        queueWorkerSettings(workerSettings_);
    }
    renderOnce();
    update();
  }

  if (ImGui::Button("Reset View")) {
    rebuildSceneAndResetView();
  }

  const bool orbitMode = inputMode_ == InputMode::Orbit;
  if (ImGui::RadioButton("Orbit", orbitMode)) {
    setInputMode(InputMode::Orbit);
  }
  if (ImGui::RadioButton("Fly", !orbitMode)) {
    setInputMode(InputMode::Fly);
  }

  if (!orbitMode) {
    const float defaultFlyStep = defaultFlyMoveStep();
    const float minFlyStep = std::max(defaultFlyStep * 0.1f, 0.0001f);
    const float maxFlyStep = std::max(defaultFlyStep * 10.0f, minFlyStep * 2.0f);
    float flySpeed = flyMoveStep();
    if (ImGui::SliderFloat("Fly speed", &flySpeed, minFlyStep, maxFlyStep, "%.4f")) {
      flyMoveStep_ = clampf(flySpeed, minFlyStep, maxFlyStep);
      renderOnce();
      update();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset fly speed")) {
      resetFlySpeed();
      renderOnce();
      update();
    }
    ImGui::Text("Move step: %.4f", flyMoveStep());
    ImGui::Text("Default step: %.4f", defaultFlyStep);
  }

  ImGui::Separator();
  ImGui::Text("Controls");
  ImGui::PushTextWrapPos(0.0f);
  if (orbitMode) {
    ImGui::BulletText("Orbit: LMB rotate, RMB pan, wheel zoom");
    ImGui::BulletText("Shift + drag: Translate");
    ImGui::BulletText("Ctrl + drag: Rotate");
    ImGui::BulletText("Shift + Ctrl + drag: Scale");
    ImGui::BulletText("Alt + Left: X axis");
    ImGui::BulletText("Alt + Shift + Left: Y axis");
    ImGui::BulletText("Alt + Right: Z translate");
    ImGui::BulletText("G: Toggle overlay");
  }
  else {
    ImGui::BulletText("Fly: WASD move, LMB look, Tab toggle");
    ImGui::BulletText("G: Toggle overlay");
  }
  ImGui::PopTextWrapPos();

  ImGui::EndChild();
  ImGui::End();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Starts an asynchronous OBJ scene load and updates widget state on completion.
bool RenderWidget::loadModel(const QString &path)
{
  if (sceneLoadInProgress_.load()) {
    lastError_ = QStringLiteral("A scene load is already in progress.");
    return false;
  }

  startAsyncLoad(
      [this, path]() {
        if (usingWorkerRenderPath()) {
          // Worker mode does scene IO and OSPRay object creation out of process,
          // then ships back bounds and status for UI-side camera reset.
          const auto result = renderWorkerClient_->loadObj(path);
          QMetaObject::invokeMethod(this, [this, result, path]() {
            sceneLoadInProgress_.store(false);
            lastError_ = result.errorMessage;
            if (result.success) {
              sceneBoundsMin_ = result.boundsMin;
              sceneBoundsMax_ = result.boundsMax;
              currentSceneIsObj_ = true;
              currentModelPath_ = path;
              currentBrlcadPath_.clear();
              currentBrlcadObject_.clear();
              currentBrlcadObjects_.clear();
              resetFlySpeed();
              resetView();
            } else {
              update();
            }
            emit sceneLoadFinished(result.success, result.errorMessage);
          }, Qt::QueuedConnection);
          return;
        }

        const bool ok = backend_.loadObj(path.toStdString());
        const QString error =
            ok ? QString() : QString::fromStdString(backend_.lastError());

        QMetaObject::invokeMethod(this, [this, ok, error, path]() {
          sceneLoadInProgress_.store(false);
          lastError_ = error;
          if (ok) {
            currentSceneIsObj_ = true;
            currentModelPath_ = path;
            backend_.resize(width(), height());
            currentBrlcadPath_.clear();
            currentBrlcadObject_.clear();
            currentBrlcadObjects_.clear();
            resetFlySpeed();
            resetView();
          } else {
            update();
          }
          emit sceneLoadFinished(ok, error);
        }, Qt::QueuedConnection);
      },
      QStringLiteral("Loading OBJ scene..."));
  return true;
}

// Starts an asynchronous BRL-CAD scene load for the selected top-level object.
bool RenderWidget::loadBrlcadModel(const QString &path, const QString &topObject)
{
  if (sceneLoadInProgress_.load()) {
    lastError_ = QStringLiteral("A scene load is already in progress.");
    return false;
  }

  const QStringList availableObjects = listBrlcadObjects(path);
  const QString resolvedObject = topObject.trimmed().isEmpty()
      ? (availableObjects.isEmpty() ? QString() : availableObjects.front())
      : topObject.trimmed();

  if (resolvedObject.isEmpty()) {
    lastError_ = QStringLiteral("No selectable BRL-CAD objects were found in this .g file.");
    currentBrlcadObjects_ = availableObjects;
    return false;
  }

  currentBrlcadObjects_ = availableObjects;
  const bool requestedWireframe = wireframeVisualization_;

  startAsyncLoad(
      [this, path, resolvedObject, availableObjects, requestedWireframe]() {
        if (usingWorkerRenderPath()) {
          // BRL-CAD scene loading can be expensive, so it follows the same async
          // worker flow as OBJ loading when the worker is available.
          const auto result = renderWorkerClient_->loadBrlcad(
              path, resolvedObject, requestedWireframe);
          QMetaObject::invokeMethod(this,
              [this, result, path, resolvedObject, availableObjects]() {
            sceneLoadInProgress_.store(false);
            lastError_ = result.errorMessage;
            if (result.success) {
              sceneBoundsMin_ = result.boundsMin;
              sceneBoundsMax_ = result.boundsMax;
              currentSceneIsObj_ = false;
              currentModelPath_.clear();
              currentBrlcadPath_ = path;
              currentBrlcadObject_ = resolvedObject;
              currentBrlcadObjects_ = availableObjects;
              resetFlySpeed();
              resetView();
            } else {
              update();
            }
            emit sceneLoadFinished(result.success, result.errorMessage);
          },
              Qt::QueuedConnection);
          return;
        }

        backend_.setVisualizationMode(requestedWireframe
                ? OsprayBackend::VisualizationMode::Wireframe
                : OsprayBackend::VisualizationMode::Solid);
        const bool ok = backend_.loadBrlcad(
            path.toStdString(), resolvedObject.toStdString());
        const QString error =
            ok ? QString() : QString::fromStdString(backend_.lastError());

        QMetaObject::invokeMethod(this,
            [this, ok, error, path, resolvedObject, availableObjects]() {
              sceneLoadInProgress_.store(false);
              lastError_ = error;
              if (ok) {
                currentSceneIsObj_ = false;
                currentModelPath_.clear();
                backend_.resize(width(), height());
                currentBrlcadPath_ = path;
                currentBrlcadObject_ = resolvedObject;
                currentBrlcadObjects_ = availableObjects;
                resetFlySpeed();
                resetView();
              } else {
                update();
              }
              emit sceneLoadFinished(ok, error);
            },
            Qt::QueuedConnection);
      },
      QStringLiteral("Loading BRL-CAD scene..."));
  return true;
}

// Queries the available BRL-CAD object names from the active render path.
QStringList RenderWidget::listBrlcadObjects(const QString &path) const
{
  if (renderWorkerClient_ && renderWorkerClient_->isConnected())
    return renderWorkerClient_->listBrlcadObjects(path);

  QStringList out;
  try {
    const auto names = backend_.listBrlcadObjects(path.toStdString());
    for (const auto &name : names)
      out << QString::fromStdString(name);
  } catch (...) {
  }
  return out;
}

std::vector<OsprayBackend::BrlcadNode> RenderWidget::brlcadHierarchy(const QString &path) const
{
  try {
    return backend_.getBrlcadHierarchy(path.toStdString());
  } catch (...) {
  }
  return {};
}

// Reloads the current BRL-CAD database with a different selected top-level object.
bool RenderWidget::reloadBrlcadObject(const QString &topObject)
{
  if (currentBrlcadPath_.isEmpty())
    return false;
  return loadBrlcadModel(currentBrlcadPath_, topObject);
}

// Returns the most recent scene-load or worker error message.
QString RenderWidget::lastError() const
{
  return lastError_;
}

// Resets the camera to a default view that frames the current scene bounds.
void RenderWidget::resetView()
{
  if (sceneLoadInProgress_.load())
    return;

  if (inputMode_ != InputMode::Orbit) {
    inputMode_ = InputMode::Orbit;
    emit inputModeChanged(inputMode_);
  }

  // Reframe around the current scene bounds and rebuild both orbit and fly
  // camera state from the same canonical view.
  center_ = sceneBoundsCenter();

  float maxExtent = sceneBoundsMaxExtent();
  if (maxExtent < 0.001f)
    maxExtent = 1.0f;

  fovy_ = 60.0f;
  dist_ = fitDistanceFromBounds(maxExtent, fovy_);
  orbitTheta_ = 0.3f;
  orbitPhi_ = 1.77079633f;
  syncFlyFromOrbit();

  resetAccumulationTargets();
  syncCameraToBackend();
  renderOnce();
  update();
}

void RenderWidget::rebuildSceneAndResetView()
{
  const auto decision = ibrt::renderworkflow::decideRebuildAction(
      {sceneLoadInProgress_.load(),
          usingWorkerRenderPath(),
          renderWorkerClient_ && renderWorkerClient_->isConnected(),
          currentSceneIsObj_,
          currentModelPath_,
          currentBrlcadPath_,
          currentBrlcadObject_});

  if (decision.action == ibrt::renderworkflow::RebuildAction::None)
    return;

  if (decision.action == ibrt::renderworkflow::RebuildAction::RestartWorker) {
    restartWorkerAndReplayState();
    if (!renderWorkerClient_ || !renderWorkerClient_->isConnected())
      return;
  } else if (decision.action == ibrt::renderworkflow::RebuildAction::ReloadObj) {
    const bool ok = backend_.loadObj(currentModelPath_.toStdString());
    if (!ok) {
      lastError_ = QString::fromStdString(backend_.lastError());
      update();
      return;
    }
    backend_.resize(width(), height());
  } else if (decision.action == ibrt::renderworkflow::RebuildAction::ReloadBrlcad) {
    backend_.setVisualizationMode(wireframeVisualization_
            ? OsprayBackend::VisualizationMode::Wireframe
            : OsprayBackend::VisualizationMode::Solid);
    const bool ok = backend_.loadBrlcad(
        currentBrlcadPath_.toStdString(), decision.brlcadObjectName.toStdString());
    if (!ok) {
      lastError_ = QString::fromStdString(backend_.lastError());
      update();
      return;
    }
    backend_.resize(width(), height());
  }

  if (decision.shouldResetView)
    resetView();
}

// Switches between orbit and fly navigation while preserving the visible view.
void RenderWidget::setInputMode(InputMode mode)
{
  if (sceneLoadInProgress_.load()) {
    inputMode_ = mode;
    emit inputModeChanged(inputMode_);
    update();
    return;
  }

  if (mode == inputMode_)
    return;

  if (mode == InputMode::Fly && inputMode_ == InputMode::Orbit) {
    // Preserve the current view when switching navigation paradigms.
    syncFlyFromOrbit();
  } else if (mode == InputMode::Orbit && inputMode_ == InputMode::Fly) {
    syncOrbitFromFly();
  }

  inputMode_ = mode;
  emit inputModeChanged(inputMode_);

  resetAccumulationTargets();
  syncCameraToBackend();
  renderOnce();
  update();
}

// Starts tracking a mouse interaction and forwards button state to ImGui.
void RenderWidget::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton)
    imguiMouseDown_[0] = true;
  if (e->button() == Qt::RightButton)
    imguiMouseDown_[1] = true;
  if (e->button() == Qt::MiddleButton)
    imguiMouseDown_[2] = true;

  imguiMousePos_ = e->position();
  update();

  if (sceneLoadInProgress_.load())
    return;

  // Let camera/object interaction happen only when the ImGui overlay is not
  // actively consuming the mouse event.
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  beginInteraction();
  lastMouse_ = e->pos();
}

// Ends or tapers off a mouse interaction and forwards button state to ImGui.
void RenderWidget::mouseReleaseEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton)
    imguiMouseDown_[0] = false;
  if (e->button() == Qt::RightButton)
    imguiMouseDown_[1] = false;
  if (e->button() == Qt::MiddleButton)
    imguiMouseDown_[2] = false;

  imguiMousePos_ = e->position();
  if (sceneLoadInProgress_.load()) {
    update();
    return;
  }

  if (!(imguiMouseDown_[0] || imguiMouseDown_[1] || imguiMouseDown_[2]))
    scheduleInteractionEnd();
  update();
}

// Updates ImGui hover state and applies camera/object dragging when appropriate.
void RenderWidget::mouseMoveEvent(QMouseEvent *e)
{
  imguiMousePos_ = e->position();
  update();

  if (sceneLoadInProgress_.load())
    return;

  if (ImGui::GetIO().WantCaptureMouse)
    return;

  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(float(e->position().x()), float(e->position().y()));

  update();

  if (io.WantCaptureMouse)
    return;

  if (e->buttons() != Qt::NoButton)
    beginInteraction();

  const QPoint screenDelta = e->pos() - lastMouse_;
  lastMouse_ = e->pos();
  const QPoint d = InteractionController::controlDelta(screenDelta);
  const int verticalDelta = d.y();

  auto result = InteractionController::classify(e->buttons(), e->modifiers());

  if (inputMode_ == InputMode::Orbit) {
    if (result.action != InteractionController::Action::None) {
      applyViewAction(result, d);
      return;
    }

    // Fallback to original behavior when no modifiers are pressed
    if (e->buttons() & Qt::LeftButton) {
      rotateOrbit(d.x() * orbitSpeed_, verticalDelta * orbitSpeed_);

      syncCameraToBackend();
      renderOnce();
      return;
    }

    if (e->buttons() & Qt::RightButton) {
      vec3f right = orbitRight();
      vec3f upCam = currentCameraUp();

      float sx = float(d.x()) * panSpeed_ * dist_;
      float sy = float(verticalDelta) * panSpeed_ * dist_;

      center_ = vec3f(center_.x - right.x * sx + upCam.x * sy,
          center_.y - right.y * sx + upCam.y * sy,
          center_.z - right.z * sx + upCam.z * sy);

      syncCameraToBackend();
      renderOnce();
      return;
    }

  } else {
    if (e->buttons() & Qt::LeftButton) {
      flyYaw_ += d.x() * orbitSpeed_;
      flyPitch_ += verticalDelta * orbitSpeed_;

      syncCameraToBackend();
      renderOnce();
      return;
    }
  }
}

// Applies zoom/dolly input from the mouse wheel.
void RenderWidget::wheelEvent(QWheelEvent *e)
{
  imguiMouseWheel_ += e->angleDelta().y() / 120.0f;
  update();

  if (sceneLoadInProgress_.load())
    return;

  if (ImGui::GetIO().WantCaptureMouse)
    return;

  beginInteraction();

  float steps = e->angleDelta().y() / 120.f;
  if (steps == 0.f)
    return;

  if (inputMode_ == InputMode::Orbit) {
    float maxExtent = sceneBoundsMaxExtent();
    if (maxExtent < 0.001f)
      maxExtent = 1.0f;

    float minDist = std::max(maxExtent * 1e-8f, 1e-8f);
    float maxDist = std::max(maxExtent * 100.0f, 10.0f);

    dist_ *= std::pow(zoomFactor_, steps);
    dist_ = clampf(dist_, minDist, maxDist);
  } else {
    fovy_ *= std::pow(0.95f, steps);
    fovy_ = clampf(fovy_, 20.f, 90.f);
  }

  syncCameraToBackend();
  renderOnce();
  scheduleInteractionEnd();
}

// Handles keyboard shortcuts, fly-camera movement, and overlay toggles.
void RenderWidget::keyPressEvent(QKeyEvent *e)
{
  ImGuiIO &io = ImGui::GetIO();

  io.AddKeyEvent(ImGuiMod_Ctrl, e->modifiers().testFlag(Qt::ControlModifier));
  io.AddKeyEvent(ImGuiMod_Shift, e->modifiers().testFlag(Qt::ShiftModifier));
  io.AddKeyEvent(ImGuiMod_Alt, e->modifiers().testFlag(Qt::AltModifier));
  io.AddKeyEvent(ImGuiMod_Super, e->modifiers().testFlag(Qt::MetaModifier));

  auto sendKey = [&](int qtKey, ImGuiKey imguiKey) {
    if (e->key() == qtKey)
      io.AddKeyEvent(imguiKey, true);
  };

  sendKey(Qt::Key_Enter, ImGuiKey_Enter);
  sendKey(Qt::Key_Return, ImGuiKey_Enter);
  sendKey(Qt::Key_Backspace, ImGuiKey_Backspace);
  sendKey(Qt::Key_Delete, ImGuiKey_Delete);
  sendKey(Qt::Key_Left, ImGuiKey_LeftArrow);
  sendKey(Qt::Key_Right, ImGuiKey_RightArrow);
  sendKey(Qt::Key_Up, ImGuiKey_UpArrow);
  sendKey(Qt::Key_Down, ImGuiKey_DownArrow);
  sendKey(Qt::Key_Home, ImGuiKey_Home);
  sendKey(Qt::Key_End, ImGuiKey_End);
  sendKey(Qt::Key_Tab, ImGuiKey_Tab);
  sendKey(Qt::Key_W, ImGuiKey_W);
  sendKey(Qt::Key_A, ImGuiKey_A);
  sendKey(Qt::Key_S, ImGuiKey_S);
  sendKey(Qt::Key_D, ImGuiKey_D);
  sendKey(Qt::Key_Q, ImGuiKey_Q);
  sendKey(Qt::Key_E, ImGuiKey_E);

  if (!e->text().isEmpty()
      && !e->modifiers().testFlag(Qt::ControlModifier)
      && !e->modifiers().testFlag(Qt::AltModifier)
      && !e->modifiers().testFlag(Qt::MetaModifier)) {
    io.AddInputCharactersUTF8(e->text().toUtf8().constData());
  }

  if (sceneLoadInProgress_.load())
    return;

  if (e->key() == Qt::Key_G && !e->isAutoRepeat()) {
    imguiVisible_ = !imguiVisible_;
    update();
    return;
  }

  if (io.WantCaptureKeyboard)
    return;

  if (isMovementKey(e->key()))
    setMovementKeyState(e->key(), true);

  if (isMovementKey(e->key()) || e->key() == Qt::Key_Tab)
    beginInteraction();

  if (e->key() == Qt::Key_Tab) {
    setInputMode(
        (inputMode_ == InputMode::Orbit) ? InputMode::Fly : InputMode::Orbit);
    scheduleInteractionEnd();
    return;
  }

  if (inputMode_ != InputMode::Fly)
    return;

  vec3f forward = forwardFromAngles(flyYaw_, flyPitch_);
  vec3f right = normalizeVec(crossVec(forward, worldUp()));
  const float step = flyMoveStep();

  if (e->key() == Qt::Key_W)
    flyPos_ = vec3f(flyPos_.x + forward.x * step,
        flyPos_.y + forward.y * step,
        flyPos_.z + forward.z * step);
  if (e->key() == Qt::Key_S)
    flyPos_ = vec3f(flyPos_.x - forward.x * step,
        flyPos_.y - forward.y * step,
        flyPos_.z - forward.z * step);
  if (e->key() == Qt::Key_A)
    flyPos_ = vec3f(flyPos_.x - right.x * step,
        flyPos_.y - right.y * step,
        flyPos_.z - right.z * step);
  if (e->key() == Qt::Key_D)
    flyPos_ = vec3f(flyPos_.x + right.x * step,
        flyPos_.y + right.y * step,
        flyPos_.z + right.z * step);
  if (e->key() == Qt::Key_Q)
    flyPos_ = vec3f(flyPos_.x - worldUp().x * step,
        flyPos_.y - worldUp().y * step,
        flyPos_.z - worldUp().z * step);
  if (e->key() == Qt::Key_E)
    flyPos_ = vec3f(flyPos_.x + worldUp().x * step,
        flyPos_.y + worldUp().y * step,
        flyPos_.z + worldUp().z * step);

  syncCameraToBackend();
  renderOnce();
}

// Clears key state when navigation keys are released.
void RenderWidget::keyReleaseEvent(QKeyEvent *e)
{
  ImGuiIO &io = ImGui::GetIO();

  io.AddKeyEvent(ImGuiMod_Ctrl, e->modifiers().testFlag(Qt::ControlModifier));
  io.AddKeyEvent(ImGuiMod_Shift, e->modifiers().testFlag(Qt::ShiftModifier));
  io.AddKeyEvent(ImGuiMod_Alt, e->modifiers().testFlag(Qt::AltModifier));
  io.AddKeyEvent(ImGuiMod_Super, e->modifiers().testFlag(Qt::MetaModifier));

  auto sendKey = [&](int qtKey, ImGuiKey imguiKey) {
    if (e->key() == qtKey)
      io.AddKeyEvent(imguiKey, false);
  };

  sendKey(Qt::Key_Enter, ImGuiKey_Enter);
  sendKey(Qt::Key_Return, ImGuiKey_Enter);
  sendKey(Qt::Key_Backspace, ImGuiKey_Backspace);
  sendKey(Qt::Key_Delete, ImGuiKey_Delete);
  sendKey(Qt::Key_Left, ImGuiKey_LeftArrow);
  sendKey(Qt::Key_Right, ImGuiKey_RightArrow);
  sendKey(Qt::Key_Up, ImGuiKey_UpArrow);
  sendKey(Qt::Key_Down, ImGuiKey_DownArrow);
  sendKey(Qt::Key_Home, ImGuiKey_Home);
  sendKey(Qt::Key_End, ImGuiKey_End);
  sendKey(Qt::Key_Tab, ImGuiKey_Tab);
  sendKey(Qt::Key_W, ImGuiKey_W);
  sendKey(Qt::Key_A, ImGuiKey_A);
  sendKey(Qt::Key_S, ImGuiKey_S);
  sendKey(Qt::Key_D, ImGuiKey_D);
  sendKey(Qt::Key_Q, ImGuiKey_Q);
  sendKey(Qt::Key_E, ImGuiKey_E);

  if (sceneLoadInProgress_.load())
    return;

  if (io.WantCaptureKeyboard)
    return;

  if (isMovementKey(e->key()))
    setMovementKeyState(e->key(), false);

  if (e->key() == Qt::Key_Tab || !anyMovementKeysDown())
    scheduleInteractionEnd();
}

// Notifies ImGui that the widget has gained focus.
void RenderWidget::focusInEvent(QFocusEvent *e)
{
  Q_UNUSED(e);
  imguiHasFocus_ = true;
  update();
}

// Notifies ImGui that the widget has lost focus and ends active movement.
void RenderWidget::focusOutEvent(QFocusEvent *e)
{
  Q_UNUSED(e);
  imguiHasFocus_ = false;
  interactionDebounceTimer_->stop();
  interactionActive_ = false;
  moveForwardKeyDown_ = false;
  moveLeftKeyDown_ = false;
  moveBackwardKeyDown_ = false;
  moveRightKeyDown_ = false;
  moveDownKeyDown_ = false;
  moveUpKeyDown_ = false;
  if (!sceneLoadInProgress_.load()) {
    backend_.setInteracting(false);
    if (usingWorkerRenderPath())
      queueWorkerInteracting(false);
  }
  update();
}

// Changes the world up-axis convention and realigns the current camera.
void RenderWidget::setUpAxis(UpAxis axis)
{
  if (sceneLoadInProgress_.load()) {
    upAxis_ = axis;
    update();
    return;
  }

  if (axis == upAxis_)
    return;

  upAxis_ = axis;
  resetView();
}

// Returns the currently selected world up-axis convention.
RenderWidget::UpAxis RenderWidget::upAxis() const
{
  return upAxis_;
}

// Returns the path of the currently loaded BRL-CAD database, if any.
QString RenderWidget::currentBrlcadPath() const
{
  return currentBrlcadPath_;
}

// Returns the currently selected BRL-CAD top-level object name.
QString RenderWidget::currentBrlcadObject() const
{
  return currentBrlcadObject_;
}

// Returns the cached list of selectable BRL-CAD objects for the current scene.
QStringList RenderWidget::currentBrlcadObjects() const
{
  return currentBrlcadObjects_;
}

// Reports whether the active scene came from a BRL-CAD database.
bool RenderWidget::hasBrlcadScene() const
{
  return !currentBrlcadPath_.isEmpty();
}

// Attaches the optional render worker client used for out-of-process rendering.
void RenderWidget::setRenderWorkerClient(RenderWorkerClient *client)
{
  renderWorkerClient_ = client;
  if (renderWorkerClient_)
    queueWorkerSettings(workerSettings_);
}

// Re-sends current viewport state to a newly connected worker.
void RenderWidget::replayWorkerState()
{
  const auto plan = ibrt::renderreplay::buildReplayPlan(
      {usingWorkerRenderPath(),
          width(),
          height(),
          currentRenderer_,
          currentSceneIsObj_,
          currentModelPath_,
          currentBrlcadPath_,
          currentBrlcadObject_});

  if (!plan.shouldReplay)
    return;

  queueWorkerResize(plan.width, plan.height);
  queueWorkerRenderer(plan.renderer);
  queueWorkerInteracting(interactionActive_);
  queueWorkerSettings(workerSettings_);

  if (plan.sceneType == ibrt::renderreplay::SceneReplayType::Obj) {
    const auto result = renderWorkerClient_->loadObj(plan.scenePath);
    if (result.success) {
      sceneBoundsMin_ = result.boundsMin;
      sceneBoundsMax_ = result.boundsMax;
    }
  } else if (plan.sceneType == ibrt::renderreplay::SceneReplayType::Brlcad) {
    const auto result =
        renderWorkerClient_->loadBrlcad(
            plan.scenePath, plan.brlcadObjectName, wireframeVisualization_);
    if (result.success) {
      sceneBoundsMin_ = result.boundsMin;
      sceneBoundsMax_ = result.boundsMax;
    }
  }

  if (plan.shouldSyncCamera)
    syncCameraToBackend();
  if (plan.shouldResetAccumulation)
    resetAccumulationTargets();
  if (plan.shouldRenderOnce)
    renderOnce();
}

// Runs a scene-loading task on a background thread and updates UI status text.
void RenderWidget::startAsyncLoad(
    const std::function<void()> &loader, const QString &statusText)
{
  // Scene loading is pushed to a background thread so large OBJ / BRL-CAD
  // databases do not stall input or repaint handling.
  if (sceneLoadThread_.joinable())
    sceneLoadThread_.join();

  sceneLoadInProgress_.store(true);
  loadStatusText_ = statusText;
  lastError_.clear();
  update();

  sceneLoadThread_ = std::thread([loader]() { loader(); });
}

void RenderWidget::startWorkerPolling()
{
  if (workerPollRunning_.exchange(true))
    return;
  workerPollThread_ = std::thread([this]() { workerPollingLoop(); });
}

void RenderWidget::stopWorkerPolling()
{
  if (!workerPollRunning_.exchange(false))
    return;
  if (workerPollThread_.joinable())
    workerPollThread_.join();
}


/*
WorkerpollingLoop
Runs in a background thread.
Sends queued commands to the worker.
Requests frames.
Stores returned images.
*/
void RenderWidget::workerPollingLoop()
{
  while (workerPollRunning_.load()) {
    if (sceneLoadInProgress_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      continue;
    }

    if (!renderWorkerClient_ || !renderWorkerClient_->isConnected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      continue;
    }

    ibrt::renderworkerqueue::PendingCommands pendingCommands;

    {
      std::lock_guard<std::mutex> lock(workerStateMutex_);
      pendingCommands = ibrt::renderworkerqueue::drain(workerPendingCommands_);
    }

    if (pendingCommands.resize)
      renderWorkerClient_->resize(pendingCommands.width, pendingCommands.height);
    if (pendingCommands.renderer)
      renderWorkerClient_->setRenderer(pendingCommands.rendererType);
    if (pendingCommands.interacting)
      renderWorkerClient_->setInteracting(pendingCommands.interactingState);
    if (pendingCommands.settings)
      renderWorkerClient_->setRenderSettings(pendingCommands.settingsState);
    if (pendingCommands.camera) {
      renderWorkerClient_->setCamera(pendingCommands.eye,
          pendingCommands.center,
          pendingCommands.up,
          pendingCommands.fovyDeg);
    }
    if (pendingCommands.resetAccumulation)
      renderWorkerClient_->resetAccumulation();

    if (!workerPollRunning_.load() || !renderWorkerClient_->isConnected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    workerRequestStart_ = std::chrono::steady_clock::now();
    workerRequestInFlight_.store(true);
    const auto frame = renderWorkerClient_->requestFrame();
    workerRequestInFlight_.store(false);

    if (!workerPollRunning_.load())
      break;

    if (frame.image.isNull()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(workerStateMutex_);
      pendingWorkerImage_ = frame.image;
      pendingWorkerFrameTimeMs_ = frame.frameTimeMs;
      pendingWorkerRenderFPS_ = frame.renderFPS;
      pendingWorkerCurrentScale_ = frame.currentScale;
      pendingWorkerAccumulatedFrames_ = frame.accumulatedFrames;
      pendingWorkerWatchdogCancels_ = frame.watchdogCancels;
      pendingWorkerAoAutoReductions_ = frame.aoAutoReductions;
      pendingWorkerRenderer_ = frame.renderer;
      workerFrameReady_.store(true);
    }

    if (!workerFrameNotifyPending_.exchange(true)) {
      QMetaObject::invokeMethod(this, [this]() {
        workerFrameNotifyPending_.store(false);
        applyLatestWorkerFrame();
      }, Qt::QueuedConnection);
    }
  }
}

void RenderWidget::queueWorkerResize(int w, int h)
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueResize(workerPendingCommands_, w, h);
}

void RenderWidget::queueWorkerCameraUpdate(
    const vec3f &eye, const vec3f &center, const vec3f &up, float fovyDeg)
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueCamera(
      workerPendingCommands_, eye, center, up, fovyDeg);
}

void RenderWidget::queueWorkerResetAccumulation()
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueResetAccumulation(workerPendingCommands_);
}

void RenderWidget::queueWorkerRenderer(const QString &rendererType)
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueRenderer(workerPendingCommands_, rendererType);
}

void RenderWidget::queueWorkerInteracting(bool interacting)
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueInteracting(workerPendingCommands_, interacting);
}

void RenderWidget::queueWorkerSettings(const RenderWorkerClient::RenderSettingsState &settings)
{
  std::lock_guard<std::mutex> lock(workerStateMutex_);
  ibrt::renderworkerqueue::queueSettings(workerPendingCommands_, settings);
}

void RenderWidget::applyLatestWorkerFrame()
{
  if (!workerFrameReady_.exchange(false))
    return;

  QImage image;
  float frameTimeMs = 0.0f;
  float renderFPS = 0.0f;
  int currentScale = 1;
  uint64_t accumulatedFrames = 0;
  uint64_t watchdogCancels = 0;
  uint64_t aoAutoReductions = 0;
  QString renderer;

  {
    std::lock_guard<std::mutex> lock(workerStateMutex_);
    image = pendingWorkerImage_;
    frameTimeMs = pendingWorkerFrameTimeMs_;
    renderFPS = pendingWorkerRenderFPS_;
    currentScale = pendingWorkerCurrentScale_;
    accumulatedFrames = pendingWorkerAccumulatedFrames_;
    watchdogCancels = pendingWorkerWatchdogCancels_;
    aoAutoReductions = pendingWorkerAoAutoReductions_;
    renderer = pendingWorkerRenderer_;
  }

  if (image.isNull())
    return;

  image_ = image;
  workerLastFrameTimeMs_ = frameTimeMs;
  workerRenderFPS_ = renderFPS;
  workerCurrentScale_ = currentScale;
  workerAccumulatedFrames_ = accumulatedFrames;
  workerWatchdogCancels_ = watchdogCancels;
  workerAoAutoReductions_ = aoAutoReductions;
  if (!renderer.isEmpty())
    currentRenderer_ = renderer;
  update();

  if (frameTimeMs < 10.0f)
    renderBudgetMs_ = std::min(10, renderBudgetMs_ + 1);
  else if (frameTimeMs > 22.0f)
    renderBudgetMs_ = std::max(3, renderBudgetMs_ - 1);
}

float RenderWidget::workerBusySeconds() const
{
  if (!workerRequestInFlight_.load())
    return 0.0f;
  const auto elapsed =
      std::chrono::steady_clock::now() - workerRequestStart_;
  return std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count();
}

bool RenderWidget::preemptWorkerControlIfBusy()
{
  if (!ibrt::renderworkflow::shouldPreemptWorkerControl(
          usingWorkerRenderPath(), workerBusySeconds())) {
    return false;
  }

  restartWorkerAndReplayState();
  return true;
}

void RenderWidget::seedCustomSettingsFromCurrentAutomatic()
{
  if (usingWorkerRenderPath()) {
    ibrt::qualitysettings::seedCustomSettingsFromAutomatic(workerSettings_);
    return;
  }

  ibrt::qualitysettings::seedBackendCustomSettingsFromAutomatic(backend_);
}

void RenderWidget::currentViewAngles(float &azimuthDeg, float &elevationDeg) const
{
  const vec3f forward = normalizeVec(currentCameraForward());
  const vec3f up = worldUp();
  const vec3f north = worldForwardReference();
  const vec3f east = normalizeVec(crossVec(north, up));

  const float upDot = std::clamp(
      forward.x * up.x + forward.y * up.y + forward.z * up.z, -1.0f, 1.0f);
  elevationDeg = std::asin(upDot) * 180.0f / 3.14159265f;

  const vec3f projected = normalizeVec(projectOntoPlane(forward, up));
  const float northComp =
      projected.x * north.x + projected.y * north.y + projected.z * north.z;
  const float eastComp =
      projected.x * east.x + projected.y * east.y + projected.z * east.z;
  azimuthDeg = std::atan2(eastComp, northComp) * 180.0f / 3.14159265f;
  if (azimuthDeg < 0.0f)
    azimuthDeg += 360.0f;
}

void RenderWidget::mirrorBackendSettingsToWorkerState()
{
  ibrt::qualitysettings::mirrorBackendSettingsToWorkerState(
      backend_, workerSettings_);
}

void RenderWidget::restartWorkerAndReplayState()
{
  if (!renderWorkerClient_ || workerRestartInProgress_.exchange(true))
    return;

  if (workerRequestInFlight_.load())
    renderWorkerClient_->stop();
  stopWorkerPolling();
  workerRequestInFlight_.store(false);
  workerFrameReady_.store(false);
  workerFrameNotifyPending_.store(false);

  const bool restarted = renderWorkerClient_->restart();
  if (restarted) {
    startWorkerPolling();
    replayWorkerState();
  } else {
    lastError_ = renderWorkerClient_->lastError();
  }
  workerRestartInProgress_.store(false);
  update();
}

vec3f RenderWidget::worldUp() const
{
  return upAxis_ == UpAxis::Z ? vec3f(0.f, 0.f, 1.f) : vec3f(0.f, 1.f, 0.f);
}

vec3f RenderWidget::worldForwardReference() const
{
  return upAxis_ == UpAxis::Z ? vec3f(0.f, 1.f, 0.f) : vec3f(0.f, 0.f, 1.f);
}

vec3f RenderWidget::forwardFromAngles(float yaw, float pitch) const
{
  const vec3f up = worldUp();
  const vec3f forwardRef = worldForwardReference();
  const vec3f rightRef = normalizeVec(crossVec(forwardRef, up));

  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);

  const vec3f dir(rightRef.x * sy * cp + up.x * sp + forwardRef.x * cy * cp,
      rightRef.y * sy * cp + up.y * sp + forwardRef.y * cy * cp,
      rightRef.z * sy * cp + up.z * sp + forwardRef.z * cy * cp);
  return normalizeVec(dir);
}

// Converts a forward direction vector into yaw/pitch angles for fly mode.
void RenderWidget::anglesFromForward(const vec3f &forward, float &yaw, float &pitch) const
{
  const vec3f dir = normalizeVec(forward);
  const vec3f up = worldUp();
  const vec3f forwardRef = worldForwardReference();
  const vec3f rightRef = normalizeVec(crossVec(forwardRef, up));

  const float upDot = std::clamp(
      dir.x * up.x + dir.y * up.y + dir.z * up.z, -1.f, 1.f);
  pitch = std::asin(upDot);

  const float fwdComp = dir.x * forwardRef.x + dir.y * forwardRef.y + dir.z * forwardRef.z;
  const float rightComp = dir.x * rightRef.x + dir.y * rightRef.y + dir.z * rightRef.z;
  yaw = std::atan2(rightComp, fwdComp);
}

vec3f RenderWidget::projectOntoPlane(const vec3f &v, const vec3f &normal) const
{
  const float dot = v.x * normal.x + v.y * normal.y + v.z * normal.z;
  return vec3f(v.x - normal.x * dot, v.y - normal.y * dot, v.z - normal.z * dot);
}

vec3f RenderWidget::orbitEyeDirection() const
{
  const vec3f up = worldUp();
  const vec3f forwardRef = worldForwardReference();
  const vec3f rightRef = normalizeVec(crossVec(forwardRef, up));

  const float sinPhi = std::sin(orbitPhi_);
  const float cosPhi = std::cos(orbitPhi_);
  const float cosTheta = std::cos(orbitTheta_);
  const float sinTheta = std::sin(orbitTheta_);

  return normalizeVec(vec3f(forwardRef.x * cosTheta * sinPhi
                                + rightRef.x * sinTheta * sinPhi + up.x * cosPhi,
      forwardRef.y * cosTheta * sinPhi + rightRef.y * sinTheta * sinPhi
          + up.y * cosPhi,
      forwardRef.z * cosTheta * sinPhi + rightRef.z * sinTheta * sinPhi
          + up.z * cosPhi));
}

// Updates orbit angles and distance so the camera eye lands at the requested position.
void RenderWidget::setOrbitFromEyePosition(const vec3f &eye)
{
  vec3f offset(
      eye.x - center_.x, eye.y - center_.y, eye.z - center_.z);
  float radius = std::sqrt(
      offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
  if (radius <= 1e-6f)
    radius = 1e-6f;
  dist_ = radius;

  const vec3f dir = vec3f(offset.x / radius, offset.y / radius, offset.z / radius);
  const vec3f up = worldUp();
  const vec3f forwardRef = worldForwardReference();
  const vec3f rightRef = normalizeVec(crossVec(forwardRef, up));

  const float upDot = std::clamp(
      dir.x * up.x + dir.y * up.y + dir.z * up.z, -1.f, 1.f);
  orbitPhi_ = std::acos(upDot);
  orbitPhi_ = clampf(orbitPhi_, 0.001f, 3.14159265f - 0.001f);

  const float sinPhi = std::sin(orbitPhi_);
  if (std::fabs(sinPhi) > 1e-6f) {
    const float forwardComp =
        dir.x * forwardRef.x + dir.y * forwardRef.y + dir.z * forwardRef.z;
    const float rightComp =
        dir.x * rightRef.x + dir.y * rightRef.y + dir.z * rightRef.z;
    orbitTheta_ = std::atan2(rightComp, forwardComp);
  }
}

vec3f RenderWidget::orbitRight() const
{
  vec3f right = crossVec(currentCameraForward(), currentCameraUp());
  right = normalizeVec(right);
  if (std::fabs(right.x) < 1e-6f && std::fabs(right.y) < 1e-6f
      && std::fabs(right.z) < 1e-6f) {
    right = normalizeVec(crossVec(currentCameraForward(), worldForwardReference()));
  }
  return right;
}

// Nudges orbit angles away from singularities to keep a stable up direction.
void RenderWidget::alignOrbitUpToReference()
{
  setOrbitFromEyePosition(currentCameraPosition());
}

vec3f RenderWidget::rotateAroundAxis(const vec3f &v, const vec3f &axis, float angle) const
{
  const vec3f n = normalizeVec(axis);
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const vec3f cross = crossVec(n, v);
  const float dot = n.x * v.x + n.y * v.y + n.z * v.z;

  return vec3f(v.x * c + cross.x * s + n.x * dot * (1.f - c),
      v.y * c + cross.y * s + n.y * dot * (1.f - c),
      v.z * c + cross.z * s + n.z * dot * (1.f - c));
}

// Applies free orbit rotation deltas and keeps the camera in a valid range.
void RenderWidget::rotateOrbit(float yawDelta, float pitchDelta)
{
  orbitTheta_ += yawDelta;
  orbitPhi_ = clampf(orbitPhi_ + pitchDelta, 0.001f, 3.14159265f - 0.001f);
  syncFlyFromOrbit();
}

// Returns the current fly movement step, including any temporary speed changes.
float RenderWidget::flyMoveStep() const
{
  if (flyMoveStep_ > 0.0f)
    return flyMoveStep_;

  return defaultFlyMoveStep();
}

// Computes a baseline fly movement step from the current scene size.
float RenderWidget::defaultFlyMoveStep() const
{
  float modelScale = sceneBoundsMaxExtent();
  if (modelScale < 0.001f)
    modelScale = 1.0f;

  return modelScale * 0.005f;
}

// Resets the fly-camera movement speed to its scene-scaled default.
void RenderWidget::resetFlySpeed()
{
  flyMoveStep_ = defaultFlyMoveStep();
}
