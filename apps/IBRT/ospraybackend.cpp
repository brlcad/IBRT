// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// BRL-CAD's portability and C API headers must precede rkcommon.  In
// particular, bio.h owns the platform-specific unistd/setmode handling, while
// common.h and rkcommon/platform.h define incompatible UNUSED macros.
extern "C" {
#include <brlcad/common.h>
#include <brlcad/raytrace.h>
#include <brlcad/rt/search.h>
#include <brlcad/bv/vlist.h>
}

#undef UNUSED
#include "ospraybackend.h"

#include "ibrt_constants.h"

#include <chrono>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <stdexcept>
#include <unordered_set>

#include <ospray/ospray.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

using rkcommon::math::vec3f;
using rkcommon::math::vec2f;
using rkcommon::math::vec3ui;
using rkcommon::math::vec4f;

namespace {
// The authored light directions below use OSPRay's conventional +Y-up basis.
// They are rotated into the application's active world-up basis before use.
const vec3f kSunLightDirection(-0.3f, -1.0f, -0.2f);
const vec3f kFillLightDirection(0.65f, -0.45f, 0.55f);
const vec3f kRimLightDirection(-0.55f, -0.2f, 0.75f);

// Per-frame diagnostics are useful while tuning the renderer but are far too
// noisy for normal desktop use. Opt in explicitly when collecting a trace.
bool verboseRenderLoggingEnabled()
{
  static const bool enabled = []() {
    const char *value = std::getenv("IBRT_VERBOSE_RENDER_LOG");
    return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

// Normalizes a lighting direction and falls back to a sensible default for degenerate input.
vec3f normalizeDirection(const vec3f &v)
{
  const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len <= 1e-6f)
    return vec3f(0.f, -1.f, 0.f);
  return vec3f(v.x / len, v.y / len, v.z / len);
}

vec3f normalizeWorldUp(const vec3f &up)
{
  const float len = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
  if (len <= 1e-6f)
    return vec3f(0.f, 0.f, 1.f);
  return vec3f(up.x / len, up.y / len, up.z / len);
}

// Rotates a direction authored in a +Y-up coordinate system into the basis
// selected by the viewer. For +Z-up this maps the ground/horizon to the XY plane.
vec3f orientYUpDirection(const vec3f &direction, const vec3f &worldUp)
{
  const vec3f up = normalizeWorldUp(worldUp);
  vec3f right(1.f - up.x * up.x, -up.x * up.y, -up.x * up.z);
  const float rightLength =
      std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rightLength <= 1e-6f)
    right = vec3f(-up.y * up.x, 1.f - up.y * up.y, -up.y * up.z);
  right = normalizeDirection(right);
  const vec3f forward(right.y * up.z - right.z * up.y,
      right.z * up.x - right.x * up.z,
      right.x * up.y - right.y * up.x);

  return normalizeDirection(vec3f(direction.x * right.x + direction.y * up.x
          + direction.z * forward.x,
      direction.x * right.y + direction.y * up.y + direction.z * forward.y,
      direction.x * right.z + direction.y * up.z + direction.z * forward.z));
}

// Returns a copy of the string with surrounding whitespace removed.
std::string trimCopy(const std::string &value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};

  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

// Reports whether OSPRay was already asked to load a module during ospInit().
bool moduleRequestedAtStartup(const char *moduleName)
{
  const char *modules = std::getenv("OSPRAY_LOAD_MODULES");
  if (!modules || !moduleName)
    return false;

  const std::string requested(modules);
  size_t begin = 0;
  while (begin <= requested.size()) {
    const size_t end = requested.find(',', begin);
    const std::string entry = requested.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (trimCopy(entry) == moduleName)
      return true;
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return false;
}

// Loads the custom BRL-CAD OSPRay module once and caches the outcome for future calls.
bool ensureBrlcadModuleLoaded(std::string &errorOut)
{
  // Module loading is cached because both scene enumeration and scene loading
  // may ask for BRL-CAD support repeatedly during a session.
  static bool attempted = false;
  static bool loaded = false;
  static std::string loadError;

  if (!attempted) {
    attempted = true;
    // Calling ospLoadModule() again reruns the module callback even when
    // OSPRAY_LOAD_MODULES already registered the geometry during ospInit().
    loaded = moduleRequestedAtStartup("brl_cad")
        || (ospLoadModule("brl_cad") == OSP_NO_ERROR);
    if (!loaded) {
      loadError =
          "Failed to load BRL-CAD OSPRay module 'brl_cad'. Ensure "
#if defined(_WIN32)
          "'ospray_module_brl_cad.dll'"
#elif defined(__APPLE__)
          "'libospray_module_brl_cad.dylib'"
#else /* linux, bsd */
          "'libospray_module_brl_cad.so'"
#endif
          " is installed as an OSPRay module "
          "or preloaded with OSPRAY_LOAD_MODULES=brl_cad.";
    }
  }

  if (!loaded)
    errorOut = loadError;

  return loaded;
}

// Builds the default light rig used for scenes that have no explicit authored lighting.
std::vector<ospray::cpp::Light> makeDefaultLights(
    const std::string &rendererType, const vec3f &worldUp, bool environmentVisible)
{
  // The viewer supplies a minimal house-light rig so imported scenes remain
  // readable even when the source data has no authored lights.
  std::vector<ospray::cpp::Light> lights;
  const vec3f up = normalizeWorldUp(worldUp);
  const vec3f sunDirection = orientYUpDirection(kSunLightDirection, up);

  if (rendererType == "pathtracer") {
    // Path tracing needs actual illumination from a light or environment. When
    // environmentVisible is false the sky/sun still illuminate the scene but are
    // not drawn behind it, so escaped primary rays show the (white) background
    // color instead of the sky dome.
    ospray::cpp::Light sunSky("sunSky");
    sunSky.setParam("up", up);
    sunSky.setParam("direction", sunDirection);
    sunSky.setParam("intensity", 0.08f);
    sunSky.setParam("albedo", 0.2f);
    sunSky.setParam("turbidity", 5.0f);
    sunSky.setParam("visible", environmentVisible);
    sunSky.commit();
    lights.push_back(sunSky);

    ospray::cpp::Light distant("distant");
    distant.setParam("direction", sunDirection);
    distant.setParam("intensity", 1.8f);
    distant.setParam("visible", environmentVisible);
    distant.setParam("angularDiameter", 1.8f);
    distant.commit();
    lights.push_back(distant);
  } else if (rendererType == "scivis") {
    ospray::cpp::Light ambient("ambient");
    ambient.setParam("intensity", 0.18f);
    ambient.commit();
    lights.push_back(ambient);

    ospray::cpp::Light key("distant");
    key.setParam("direction", sunDirection);
    key.setParam("intensity", 2.2f);
    key.setParam("angularDiameter", 2.4f);
    key.commit();
    lights.push_back(key);

    ospray::cpp::Light fill("distant");
    fill.setParam("direction", orientYUpDirection(kFillLightDirection, up));
    fill.setParam("intensity", 0.65f);
    fill.setParam("angularDiameter", 12.0f);
    fill.commit();
    lights.push_back(fill);

    ospray::cpp::Light rim("distant");
    rim.setParam("direction", orientYUpDirection(kRimLightDirection, up));
    rim.setParam("intensity", 0.18f);
    rim.setParam("angularDiameter", 6.0f);
    rim.commit();
    lights.push_back(rim);
  } else {
    ospray::cpp::Light ambient("ambient");
    ambient.setParam("intensity", 0.05f);
    ambient.commit();
    lights.push_back(ambient);
  }

  return lights;
}
}

// Creates the default renderer, camera, and fallback test scene.
void OsprayBackend::init()
{
  try {
    // The denoiser (Intel OIDN) module is optional and shared by every render
    // path (inline viewer, worker, reference, tests), all of which run through
    // init() after the device is current.  ospLoadModule is idempotent, so it
    // is safe to (re)load here; if it is unavailable the backend still works,
    // only the denoiser toggle has no effect.
    denoiserModuleAvailable_ = (ospLoadModule("denoiser") == OSP_NO_ERROR);
    if (!denoiserModuleAvailable_)
      fprintf(stderr,
          "OsprayBackend: OSPRay denoiser module unavailable; "
          "denoising disabled.\n");

    // Start from a conservative renderer/camera pair so the widget has
    // something valid to show before any external scene is loaded.
    renderer_ = ospray::cpp::Renderer("scivis");
    currentRenderer_ = "scivis";
    applyRendererDefaults();
    appliedAoSamples_ = configuredAoSamplesForCurrentMode();
    appliedPixelSamples_ = configuredPixelSamplesForCurrentMode();

    camera_ = ospray::cpp::Camera("perspective");
    camera_.setParam("fovy", 60.f);
    updateCameraCrop(vec2f(0.f, 0.f), vec2f(1.f, 1.f));
    camera_.commit();
    cameraDirty_ = false;

    loadTestMesh();
  } catch (const std::exception &e) {
    setError(e.what());
  } catch (...) {
    setError("Unknown failure while initializing OSPRay backend.");
  }
}

// Resizes framebuffers and queues a fresh render at the new resolution.
void OsprayBackend::resize(int w, int h)
{
  if (frameInFlight_) {
    // Resize is deferred while a frame is active so framebuffer ownership
    // changes happen only at safe synchronization points.
    pendingResizeW_ = std::max(1, w);
    pendingResizeH_ = std::max(1, h);
    pendingResize_ = true;
    enqueueLatestRenderRequest("resize");
    return;
  }

  fbW_ = std::max(1, w);
  fbH_ = std::max(1, h);

  camera_.setParam("aspect", float(fbW_) / float(fbH_));
  cameraDirty_ = true;

  // Full-resolution accumulation buffer (used once progressive scale reaches 1x).
  rebuildAccumFrameBuffer();
  displayPixels_.assign(size_t(fbW_) * size_t(fbH_), 0u);
  resetProgressiveState(true);
  enqueueLatestRenderRequest("resize");
}

// Updates the active camera parameters and schedules a new render.
void OsprayBackend::setCamera(const vec3f &eye, const vec3f &center, const vec3f &up, float fovyDeg)
{
  ++cameraVersion_;
  if (frameInFlight_) {
    // Interactive camera motion is allowed to preempt non-preview work so the
    // viewport stays responsive while the user drags.
    if (isInteracting_ && activeRenderRequest_
        && activeRenderRequest_->type != RenderRequestType::Preview) {
      cancelInFlightFrame("camera_move_preempt");
    }
  }

  if (frameInFlight_) {
    pendingCameraState_ = PendingCameraState{eye, center, up, fovyDeg};
    enqueueLatestRenderRequest("camera");
    return;
  }

  cameraState_ = PendingCameraState{eye, center, up, fovyDeg};
  applyCameraParams();
  enqueueLatestRenderRequest("camera");
}

// Pushes the retained camera pose (cameraState_) onto the OSPRay camera,
// selecting the parameter appropriate to the active projection: perspective
// takes a vertical field of view, orthographic takes a world-space viewport
// height. The orthographic height is chosen as 2 * distance * tan(fovy/2) so the
// framing matches the perspective view at the pivot plane (distance = |center -
// eye|), which keeps the image stable when the user toggles projection.
void OsprayBackend::applyCameraParams()
{
  const rkcommon::math::vec3f direction =
      cameraState_.center - cameraState_.eye;
  camera_.setParam("position", cameraState_.eye);
  camera_.setParam("direction", direction);
  camera_.setParam("up", cameraState_.up);

  if (projectionMode_ == ProjectionMode::Orthographic) {
    const float distance = std::sqrt(direction.x * direction.x
        + direction.y * direction.y + direction.z * direction.z);
    const float halfFovRad = 0.5f * cameraState_.fovyDeg * ibrt::kPi / 180.f;
    const float height =
        2.f * std::max(distance, 1e-4f) * std::tan(halfFovRad);
    camera_.setParam("height", std::max(height, 1e-4f));
  } else {
    camera_.setParam("fovy", cameraState_.fovyDeg);
  }
  cameraDirty_ = true;
}

// Recreates camera_ with the OSPRay camera type matching the current projection
// mode (the type is immutable after construction) and re-applies the pose and
// aspect ratio. Called between frames from applyPendingState.
void OsprayBackend::rebuildCameraForProjection()
{
  const char *type = projectionMode_ == ProjectionMode::Orthographic
      ? "orthographic"
      : "perspective";
  camera_ = ospray::cpp::Camera(type);
  camera_.setParam("aspect", float(fbW_) / float(fbH_));
  applyCameraParams();
  camera_.commit();
  cameraDirty_ = false;
}

// Switches the camera projection. The OSPRay camera type is fixed at
// construction, so the actual rebuild is deferred to applyPendingState (between
// frames); this also restarts accumulation since every pixel changes.
void OsprayBackend::setProjectionMode(ProjectionMode mode)
{
  if (projectionMode_ == mode)
    return;
  projectionMode_ = mode;
  pendingProjectionRebuild_ = true;
  enqueueLatestRenderRequest("projection");
}

OsprayBackend::ProjectionMode OsprayBackend::projectionMode() const
{
  return projectionMode_;
}

// Clears progressive accumulation so the next render starts from a clean state.
void OsprayBackend::resetAccumulation()
{
  if (frameInFlight_) {
    pendingResetAccumulation_ = true;
    enqueueLatestRenderRequest("resetAccumulation");
    return;
  }

  resetProgressiveState(false);
  enqueueLatestRenderRequest("resetAccumulation");
}

void OsprayBackend::cancelRender()
{
  cancelInFlightFrame("cancelRender");
}

const uint32_t *OsprayBackend::pixels() const
{
  return displayPixels_.empty() ? nullptr : displayPixels_.data();
}

// Advances the progressive render state machine and returns whether the display changed.
bool OsprayBackend::advanceRender(int timeBudgetMs)
{
  try {
    (void)timeBudgetMs;

    if (frameInFlight_) {
      // Poll the current asynchronous OSPRay future until the frame completes.
      if (!currentFrame_.handle()) {
        frameInFlight_ = false;
        inFlightStartValid_ = false;
        return false;
      }

      if (!currentFrame_.isReady(OSP_FRAME_FINISHED))
        return false;

      const bool updatedImage = finishCompletedRender();
      applyPendingState();
      return updatedImage;
    }

    if (!renderer_.handle() || !camera_.handle() || !world_.handle()
        || fbW_ <= 0 || fbH_ <= 0
        || displayPixels_.empty()) {
      return false;
    }

    applyPendingState();

    // Rendering alternates between low-resolution progressive passes and
    // optional full-resolution accumulation once the camera settles.
    const bool accumulationEnabled = accumulationEnabledForCurrentMode();
    const int maxAccumulationFrames = maxAccumulationFramesForCurrentMode();
    const bool fullResAccumOnly = fullResAccumulationOnlyForCurrentMode();
    const int configuredAo = configuredAoSamplesForCurrentMode();
    const int configuredPixel = configuredPixelSamplesForCurrentMode();
    const bool fixedPreviewMode = false;
    const int interactionAo = configuredAo;
    const int interactionPixel = configuredPixel;
    dynamicModeActive_ = (settingsMode_ == SettingsMode::Automatic);
    const bool willAccumulate =
        !fixedPreviewMode && passScale_ <= 1 && accumFb_.handle()
        && accumulationEnabled;
    if (willAccumulate) {
      if (maxAccumulationFrames > 0
          && accumulatedFrames_ >= uint64_t(maxAccumulationFrames)) {
        return false;
      }
      renderPhase_ = RenderPhase::Accumulate;
    } else {
      renderPhase_ = RenderPhase::Progressive;
    }

    const int backoffAo = std::max(0, interactionAo - aoBackoffSteps_);
    // Ramp AO in over the finer progressive passes once the view is settling,
    // so the occlusion shading is established before full resolution instead of
    // appearing all at once. The pass upsample blurs the sparse-sample noise,
    // and the count is capped at 1 so interaction (which holds at the coarse
    // start scale) and high custom AO counts stay cheap until accumulation.
    const int progressiveAo =
        (!isInteracting_ && passScale_ <= 4) ? std::min(backoffAo, 1) : 0;
    const int effectiveAoSamples = (renderPhase_ == RenderPhase::Accumulate)
        ? configuredAo
        : ((passScale_ > 1) ? progressiveAo : backoffAo);
    const float effectiveAoDistance = configuredAoDistanceForCurrentMode();
    const int effectivePixelSamples =
        (renderPhase_ == RenderPhase::Accumulate)
        ? std::max(1, configuredPixel)
        : ((passScale_ > 1) ? 1 : std::max(1, interactionPixel));
    const int effectiveMaxPathLength = configuredMaxPathLengthForCurrentMode();
    const int effectiveRoulettePathLength =
        configuredRoulettePathLengthForCurrentMode();
    applyRendererSamplingParams(effectiveAoSamples,
        effectiveAoDistance,
        effectivePixelSamples,
        effectiveMaxPathLength,
        effectiveRoulettePathLength);

    assert(!frameInFlight_);
    startNextRenderWork();
    return false;
  } catch (const std::exception &e) {
    frameInFlight_ = false;
    setError(e.what());
    return false;
  } catch (...) {
    frameInFlight_ = false;
    setError("Unknown failure while advancing progressive render.");
    return false;
  }
}

// Returns the last completed frame time in milliseconds.
float OsprayBackend::lastFrameTimeMs() const
{
  return lastFrameTimeMs_;
}

// Returns the measured render throughput for the last completed frame sequence.
float OsprayBackend::renderFPS() const
{
  if (lastFrameTimeMs_ <= 0.0001f)
    return 0.0f;
  return 1000.0f / lastFrameTimeMs_;
}

size_t OsprayBackend::debugSceneInstanceCount() const
{
  return sceneInstances_.size();
}

// Returns the minimum corner of the current scene bounds.
rkcommon::math::vec3f OsprayBackend::getBoundsMin() const
{
  return boundsMin_;
}

// Returns the maximum corner of the current scene bounds.
rkcommon::math::vec3f OsprayBackend::getBoundsMax() const
{
  return boundsMax_;
}

// Returns the longest axis length of the current scene bounds.
float OsprayBackend::getBoundsMaxExtent() const
{
  float dx = boundsMax_.x - boundsMin_.x;
  float dy = boundsMax_.y - boundsMin_.y;
  float dz = boundsMax_.z - boundsMin_.z;
  return std::max(dx, std::max(dy, dz));
}

// Returns the center point of the current scene bounds.
rkcommon::math::vec3f OsprayBackend::getBoundsCenter() const
{
  return rkcommon::math::vec3f(0.5f * (boundsMin_.x + boundsMax_.x),
      0.5f * (boundsMin_.y + boundsMax_.y),
      0.5f * (boundsMin_.z + boundsMax_.z));
}

// Returns an approximate radius derived from the current scene bounds.
float OsprayBackend::getBoundsRadius() const
{
  float dx = boundsMax_.x - boundsMin_.x;
  float dy = boundsMax_.y - boundsMin_.y;
  float dz = boundsMax_.z - boundsMin_.z;

  float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  return std::max(0.5f * diag, 0.001f);
}

// Builds a simple fallback triangle mesh used before a real scene is loaded.
void OsprayBackend::loadTestMesh()
{
  std::vector<vec3f> vertex = {vec3f(-1.0f, -1.0f, 3.0f),
      vec3f(-1.0f, 1.0f, 3.0f),
      vec3f(1.0f, -1.0f, 3.0f),
      vec3f(0.1f, 0.1f, 0.3f)};

  std::vector<vec4f> color = {vec4f(0.9f, 0.5f, 0.5f, 1.0f),
      vec4f(0.8f, 0.8f, 0.8f, 1.0f),
      vec4f(0.8f, 0.8f, 0.8f, 1.0f),
      vec4f(0.5f, 0.9f, 0.5f, 1.0f)};

  std::vector<vec3ui> index = {vec3ui(0, 1, 2), vec3ui(1, 2, 3)};

  boundsMin_ = vertex[0];
  boundsMax_ = vertex[0];
  for (const auto &v : vertex) {
    boundsMin_.x = std::min(boundsMin_.x, v.x);
    boundsMin_.y = std::min(boundsMin_.y, v.y);
    boundsMin_.z = std::min(boundsMin_.z, v.z);
    boundsMax_.x = std::max(boundsMax_.x, v.x);
    boundsMax_.y = std::max(boundsMax_.y, v.y);
    boundsMax_.z = std::max(boundsMax_.z, v.z);
  }

  ospray::cpp::Geometry mesh("mesh");
  mesh.setParam("vertex.position", ospray::cpp::CopiedData(vertex));
  mesh.setParam("vertex.color", ospray::cpp::CopiedData(color));
  mesh.setParam("index", ospray::cpp::CopiedData(index));
  mesh.commit();

  ospray::cpp::GeometricModel model(mesh);
  model.commit();

  ospray::cpp::Group group;
  group.setParam("geometry", ospray::cpp::CopiedData(model));
  group.commit();

  ospray::cpp::Instance instance(group);
  instance.commit();

  sceneInstances_ = {instance};
  world_ = ospray::cpp::World();
  applyWorldInstances();
  applyDefaultLights();
  world_.commit();

  resetAccumulation();
}

// Loads an OBJ file into OSPRay scene objects and resets progressive state.
bool OsprayBackend::loadObj(const std::string &path)
{
  if (frameInFlight_) {
    cancelInFlightFrame();
  }

  lastError_.clear();
  try {
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    if (!reader.ParseFromFile(path, config)) {
      setError("Could not parse OBJ file: " + path);
      return false;
    }

    if (!reader.Error().empty()) {
      setError(reader.Error());
      return false;
    }

    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();

    std::vector<vec3f> vertices;
    std::vector<vec4f> colors;
    std::vector<vec3ui> indices;

    for (size_t v = 0; v < attrib.vertices.size() / 3; ++v) {
      vertices.emplace_back(attrib.vertices[3 * v + 0],
          attrib.vertices[3 * v + 1],
          attrib.vertices[3 * v + 2]);
      colors.emplace_back(0.8f, 0.8f, 0.8f, 1.0f);
    }

    for (const auto &shape : shapes) {
      size_t indexOffset = 0;
      for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
        int fv = shape.mesh.num_face_vertices[f];
        if (fv != 3) {
          indexOffset += fv;
          continue;
        }

        const auto &i0 = shape.mesh.indices[indexOffset + 0];
        const auto &i1 = shape.mesh.indices[indexOffset + 1];
        const auto &i2 = shape.mesh.indices[indexOffset + 2];

        if (i0.vertex_index < 0 || i1.vertex_index < 0
            || i2.vertex_index < 0) {
          indexOffset += fv;
          continue;
        }

        indices.emplace_back(static_cast<unsigned>(i0.vertex_index),
            static_cast<unsigned>(i1.vertex_index),
            static_cast<unsigned>(i2.vertex_index));

        indexOffset += fv;
      }
    }

    if (vertices.empty() || indices.empty()) {
      setError("OBJ file did not contain any triangulated mesh data.");
      return false;
    }

    boundsMin_ = vertices[0];
    boundsMax_ = vertices[0];
    for (const auto &v : vertices) {
      boundsMin_.x = std::min(boundsMin_.x, v.x);
      boundsMin_.y = std::min(boundsMin_.y, v.y);
      boundsMin_.z = std::min(boundsMin_.z, v.z);
      boundsMax_.x = std::max(boundsMax_.x, v.x);
      boundsMax_.y = std::max(boundsMax_.y, v.y);
      boundsMax_.z = std::max(boundsMax_.z, v.z);
    }

    ospray::cpp::Geometry mesh("mesh");
    mesh.setParam("vertex.position", ospray::cpp::CopiedData(vertices));
    mesh.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    mesh.setParam("index", ospray::cpp::CopiedData(indices));
    mesh.commit();

    ospray::cpp::GeometricModel model(mesh);
    model.commit();

    ospray::cpp::Group group;
    group.setParam("geometry", ospray::cpp::CopiedData(model));
    group.commit();

    ospray::cpp::Instance instance(group);
    instance.commit();

    sceneInstances_ = {instance};
    world_ = ospray::cpp::World();
    applyWorldInstances();
    applyDefaultLights();
    world_.commit();

    resetAccumulation();
    return true;
  } catch (const std::exception &e) {
    setError(e.what());
    return false;
  } catch (...) {
    setError("Unknown failure while loading OBJ.");
    return false;
  }
}


// Loads a BRL-CAD database/object pair into OSPRay scene objects.
bool OsprayBackend::loadBrlcad(
    const std::string &path, const std::string &topObject)
{
  if (frameInFlight_) {
    cancelInFlightFrame();
  }

  lastError_.clear();
  try {
  FILE *dbFile = std::fopen(path.c_str(), "rb");
  if (!dbFile) {
    setError("BRL-CAD database file does not exist.");
    return false;
  }
  std::fclose(dbFile);

  if (!topObject.empty() && topObject != "all") {
    const auto availableObjects = listBrlcadObjects(path);
    const bool objectExists = std::find(
                                  availableObjects.begin(),
                                  availableObjects.end(),
                                  topObject)
        != availableObjects.end();
    if (!objectExists) {
      setError("Requested BRL-CAD object was not found in the database.");
      return false;
    }
  }

  if (visualizationMode_ == VisualizationMode::Wireframe)
    return loadBrlcadWireframe(path, topObject);

  std::string moduleError;
  if (!ensureBrlcadModuleLoaded(moduleError)) {
    setError(moduleError);
    return false;
  }

  fprintf(stderr, "loadBrlcad: START\n");
  fprintf(stderr, "loadBrlcad: path = %s\n", path.c_str());
  fprintf(stderr, "loadBrlcad: object = %s\n", topObject.c_str());

  // STEP 1: Create geometry
  fprintf(stderr, "STEP 1: Creating OSPRay brlcad geometry\n");

  OSPGeometry rawGeom = ospNewGeometry("brlcad");
  fprintf(stderr, "geom handle = %p\n", (void *)rawGeom);
  fflush(stderr);

  if (!rawGeom) {
    setError("OSPRay could not create geometry type 'brlcad'. "
             "The BRL-CAD module loaded, but the active device did not create the custom geometry.");
    fprintf(stderr, "ERROR: %s\n", lastError_.c_str());
    return false;
  }

  ospray::cpp::Geometry geom(rawGeom);

  fprintf(stderr, "STEP 2: Setting filename param\n");
  geom.setParam("filename", path);

  if (!topObject.empty()) {
    fprintf(stderr, "STEP 3: Setting objects param\n");
    geom.setParam("objects", topObject);
  }

  fprintf(stderr, "STEP 4: Committing geometry\n");
  geom.commit(); // 🔥 VERY LIKELY CRASH POINT
  fprintf(stderr, "STEP 4 DONE\n");

  // STEP 5: Bounds calculation
  fprintf(stderr, "STEP 5: Default bounds\n");
  boundsMin_ = vec3f(-1.f, -1.f, -1.f);
  boundsMax_ = vec3f(1.f, 1.f, 1.f);

  fprintf(stderr, "STEP 11: Creating GeometricModel\n");
  ospray::cpp::GeometricModel gmodel(geom);
  applyDefaultMaterial(gmodel);
  gmodel.commit();

  fprintf(stderr, "STEP 12: Creating Group\n");
  ospray::cpp::Group group;

  std::vector<ospray::cpp::GeometricModel> models = {gmodel};
  group.setParam("geometry", ospray::cpp::CopiedData(models));
  fprintf(stderr, "STEP 12: Creating Group - set param\n");
  group.commit();
  fprintf(stderr, "STEP 12: Creating Group - commit\n");


  fprintf(stderr, "STEP 13: Creating Instance\n");
  ospray::cpp::Instance instance(group);
  instance.commit();

  fprintf(stderr, "STEP 14: Creating World\n");
  sceneInstances_ = {instance};
  world_ = ospray::cpp::World();
  applyWorldInstances();

  fprintf(stderr, "STEP 15: Adding light\n");
  applyDefaultLights();

  fprintf(stderr, "STEP 16: Commit world\n");
  world_.commit();

  fprintf(stderr, "STEP 16B: Reading world bounds\n");
  const OSPBounds worldBounds = ospGetBounds(instance.handle());
  if (std::isfinite(worldBounds.lower[0]) && std::isfinite(worldBounds.lower[1])
      && std::isfinite(worldBounds.lower[2]) && std::isfinite(worldBounds.upper[0])
      && std::isfinite(worldBounds.upper[1]) && std::isfinite(worldBounds.upper[2])) {
    boundsMin_ =
        vec3f(worldBounds.lower[0], worldBounds.lower[1], worldBounds.lower[2]);
    boundsMax_ =
        vec3f(worldBounds.upper[0], worldBounds.upper[1], worldBounds.upper[2]);
    fprintf(stderr,
        "STEP 16B DONE: min=(%f,%f,%f) max=(%f,%f,%f)\n",
        boundsMin_.x,
        boundsMin_.y,
        boundsMin_.z,
        boundsMax_.x,
        boundsMax_.y,
        boundsMax_.z);
  } else {
    fprintf(stderr, "STEP 16B: world bounds unavailable, using defaults\n");
  }

  fprintf(stderr, "STEP 17: Reset accumulation\n");
  resetAccumulation();

  fprintf(stderr, "loadBrlcad: SUCCESS\n");

  return true;
  } catch (const std::exception &e) {
    setError(e.what());
    return false;
  } catch (...) {
    setError("Unknown failure while loading BRL-CAD geometry.");
    return false;
  }
}

// Switches the active renderer type and reapplies renderer-specific defaults.
void OsprayBackend::setRenderer(const std::string &type)
{
  if (frameInFlight_) {
    pendingRendererType_ = type;
    pendingResetAccumulation_ = true;
    enqueueLatestRenderRequest("renderer");
    return;
  }

  try {
    renderer_ = ospray::cpp::Renderer(type);
    currentRenderer_ = type;
    applyRendererDefaults();
    appliedAoSamples_ = configuredAoSamplesForCurrentMode();
    appliedPixelSamples_ = configuredPixelSamplesForCurrentMode();

    if (world_.handle()) {
      applyWorldInstances();
      applyDefaultLights();
      world_.commit();
    }

    resetAccumulation();
    enqueueLatestRenderRequest("renderer");
  } catch (const std::exception &e) {
    setError(e.what());
  } catch (...) {
    setError("Unknown failure while changing renderer.");
  }
}

// Returns the active renderer name.
const std::string &OsprayBackend::currentRenderer() const
{
  return currentRenderer_;
}

// Sets the background baked into rendered pixels. Keeping it opaque prevents
// the presentation layer from compositing anti-aliased silhouettes a second time.
void OsprayBackend::setOpaqueBackgroundColor(const vec3f &color)
{
  const vec3f clamped(std::clamp(color.x, 0.0f, 1.0f),
      std::clamp(color.y, 0.0f, 1.0f),
      std::clamp(color.z, 0.0f, 1.0f));
  if (backgroundColor_ == clamped)
    return;

  cancelInFlightFrame("background");
  backgroundColor_ = clamped;
  if (renderer_.handle()) {
    renderer_.setParam("backgroundColor",
        vec4f(backgroundColor_.x,
            backgroundColor_.y,
            backgroundColor_.z,
            1.0f));
    renderer_.commit();
    resetProgressiveState(false);
    enqueueLatestRenderRequest("background");
  }
}

// Sets the zenith used by procedural environment lighting and rotates the
// authored light rig into the same world-up basis.
void OsprayBackend::setWorldUp(const vec3f &up)
{
  const vec3f normalized = normalizeWorldUp(up);
  const vec3f delta = normalized - worldUp_;
  if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z < 1e-10f)
    return;

  cancelInFlightFrame("world_up");
  worldUp_ = normalized;
  if (world_.handle()) {
    applyDefaultLights();
    world_.commit();
  }
  resetAccumulation();
}

vec3f OsprayBackend::worldUp() const
{
  return worldUp_;
}

// Sets ambient-occlusion sampling for the current rendering mode.
void OsprayBackend::setAoSamples(int samples)
{
  const int clamped = std::clamp(samples, 0, kMaxSafeAoSamples);
  if (customAoSamples_ == clamped)
    return;

  customAoSamples_ = clamped;
  resetAccumulation();
}

// Sets the AO ray distance limit for the current rendering mode.
void OsprayBackend::setAoDistance(float distance)
{
  if (frameInFlight_) {
    setError("AO distance update ignored while render is in flight.");
    return;
  }

  const float clamped = std::clamp(distance, 0.0f, 1e20f);
  if (std::fabs(customAoDistance_ - clamped) < 0.001f)
    return;

  customAoDistance_ = clamped;
  resetAccumulation();
}

// Sets per-pixel sampling for the current rendering mode.
void OsprayBackend::setPixelSamples(int samples)
{
  if (frameInFlight_) {
    setError("Pixel sample update ignored while render is in flight.");
    return;
  }

  const int clamped = std::clamp(samples, 1, kMaxSafePixelSamples);
  if (customPixelSamples_ == clamped)
    return;

  customPixelSamples_ = clamped;
  resetAccumulation();
}

// Sets the hard path-depth cap used by renderers that support recursive rays.
void OsprayBackend::setMaxPathLength(int depth)
{
  if (frameInFlight_) {
    setError("Max path length update ignored while render is in flight.");
    return;
  }

  const int clamped = std::clamp(depth, 0, 64);
  if (customMaxPathLength_ == clamped)
    return;

  customMaxPathLength_ = clamped;
  resetAccumulation();
}

// Sets the depth at which Russian roulette early termination may begin.
void OsprayBackend::setRoulettePathLength(int depth)
{
  if (frameInFlight_) {
    setError("Early-exit depth update ignored while render is in flight.");
    return;
  }

  const int clamped = std::clamp(depth, 0, 64);
  if (customRoulettePathLength_ == clamped)
    return;

  customRoulettePathLength_ = clamped;
  resetAccumulation();
}

// Chooses between automatic and manual render-quality management.
void OsprayBackend::setSettingsMode(SettingsMode mode)
{
  if (settingsMode_ == mode)
    return;
  settingsMode_ = mode;
  resetAccumulation();
}

// Returns the current render-quality control mode.
OsprayBackend::SettingsMode OsprayBackend::settingsMode() const
{
  return settingsMode_;
}

// Selects the automatic preset used for dynamic render quality.
void OsprayBackend::setAutomaticPreset(AutomaticPreset preset)
{
  if (automaticPreset_ == preset)
    return;
  automaticPreset_ = preset;
  resetAccumulation();
}

// Returns the selected automatic quality preset.
OsprayBackend::AutomaticPreset OsprayBackend::automaticPreset() const
{
  return automaticPreset_;
}

// Sets the frame-time target used by automatic quality control.
void OsprayBackend::setAutomaticTargetFrameTimeMs(float ms)
{
  const float clamped = std::clamp(ms, 2.0f, 1000.0f);
  if (std::fabs(automaticTargetFrameTimeMs_ - clamped) < 0.001f)
    return;
  automaticTargetFrameTimeMs_ = clamped;
  resetAccumulation();
}

// Returns the automatic frame-time target.
float OsprayBackend::automaticTargetFrameTimeMs() const
{
  return automaticTargetFrameTimeMs_;
}

// Enables or disables accumulation in automatic mode.
void OsprayBackend::setAutomaticAccumulationEnabled(bool enabled)
{
  if (automaticAccumulationEnabled_ == enabled)
    return;
  automaticAccumulationEnabled_ = enabled;
  resetAccumulation();
}

// Reports whether accumulation is enabled in automatic mode.
bool OsprayBackend::automaticAccumulationEnabled() const
{
  return automaticAccumulationEnabled_;
}

// Sets the initial progressive render scale for custom mode.
void OsprayBackend::setCustomStartScale(int scale)
{
  const int sanitized = sanitizeScale(scale);
  if (customStartScale_ == sanitized)
    return;
  customStartScale_ = sanitized;
  resetAccumulation();
}

// Returns the custom mode starting render scale.
int OsprayBackend::customStartScale() const
{
  return customStartScale_;
}

// Sets the desired frame-time budget for custom mode.
void OsprayBackend::setCustomTargetFrameTimeMs(float ms)
{
  const float clamped = std::clamp(ms, 2.0f, 1000.0f);
  if (std::fabs(customTargetFrameTimeMs_ - clamped) < 0.001f)
    return;
  customTargetFrameTimeMs_ = clamped;
  resetAccumulation();
}

// Returns the custom mode frame-time target.
float OsprayBackend::customTargetFrameTimeMs() const
{
  return customTargetFrameTimeMs_;
}

// Returns the configured AO samples for custom mode.
int OsprayBackend::customAoSamples() const
{
  return customAoSamples_;
}

// Returns the AO distance limit configured for custom mode.
float OsprayBackend::customAoDistance() const
{
  return customAoDistance_;
}

// Returns the configured pixel samples for custom mode.
int OsprayBackend::customPixelSamples() const
{
  return customPixelSamples_;
}

// Returns the maximum recursive path depth configured for custom mode.
int OsprayBackend::customMaxPathLength() const
{
  return customMaxPathLength_;
}

// Returns the Russian-roulette start depth configured for custom mode.
int OsprayBackend::customRoulettePathLength() const
{
  return customRoulettePathLength_;
}

// Enables or disables accumulation in custom mode.
void OsprayBackend::setCustomAccumulationEnabled(bool enabled)
{
  if (customAccumulationEnabled_ == enabled)
    return;
  customAccumulationEnabled_ = enabled;
  resetAccumulation();
}

// Reports whether accumulation is enabled in custom mode.
bool OsprayBackend::customAccumulationEnabled() const
{
  return customAccumulationEnabled_;
}

// Sets the accumulation frame cap used in custom mode.
void OsprayBackend::setCustomMaxAccumulationFrames(int frames)
{
  const int clamped = std::clamp(frames, 0, 1000000);
  if (customMaxAccumulationFrames_ == clamped)
    return;
  customMaxAccumulationFrames_ = clamped;
  resetAccumulation();
}

// Returns the accumulation frame cap used in custom mode.
int OsprayBackend::customMaxAccumulationFrames() const
{
  return customMaxAccumulationFrames_;
}

// Controls whether interaction temporarily drops quality in custom mode.
void OsprayBackend::setCustomLowQualityWhileInteracting(bool enabled)
{
  if (customLowQualityWhileInteracting_ == enabled)
    return;
  customLowQualityWhileInteracting_ = enabled;
  resetAccumulation();
}

// Reports whether custom mode lowers quality while interacting.
bool OsprayBackend::customLowQualityWhileInteracting() const
{
  return customLowQualityWhileInteracting_;
}

// Controls whether accumulation is limited to full-resolution passes in custom mode.
void OsprayBackend::setCustomFullResAccumulationOnly(bool enabled)
{
  if (customFullResAccumulationOnly_ == enabled)
    return;
  customFullResAccumulationOnly_ = enabled;
  resetAccumulation();
}

// Reports whether custom mode accumulates only at full resolution.
bool OsprayBackend::customFullResAccumulationOnly() const
{
  return customFullResAccumulationOnly_;
}

// Sets the render watchdog timeout used to preempt overly slow frames.
void OsprayBackend::setCustomWatchdogTimeoutMs(int ms)
{
  const int clamped = std::clamp(ms, 10, 60000);
  if (customWatchdogTimeoutMs_ == clamped)
    return;
  customWatchdogTimeoutMs_ = clamped;
  resetAccumulation();
}

// Returns the render watchdog timeout used in custom mode.
int OsprayBackend::customWatchdogTimeoutMs() const
{
  return customWatchdogTimeoutMs_;
}

// Enables or disables OIDN denoising of the full-resolution accumulation pass.
void OsprayBackend::setDenoiseEnabled(bool enabled)
{
  if (denoiseEnabled_ == enabled)
    return;
  denoiseEnabled_ = enabled;
  // The accumulation buffer must be rebuilt to add/remove the guide channels
  // and denoiser image operation; defer it to applyPendingState so it happens
  // between frames rather than mid-flight.
  pendingAccumRebuild_ = true;
  enqueueLatestRenderRequest("denoiseToggle");
}

// Reports whether OIDN denoising is currently enabled.
bool OsprayBackend::denoiseEnabled() const
{
  return denoiseEnabled_;
}

// Updates the backend's notion of whether the user is actively interacting.
void OsprayBackend::setInteracting(bool interacting)
{
  if (isInteracting_ == interacting)
    return;
  isInteracting_ = interacting;
  enqueueLatestRenderRequest(interacting ? "interaction.begin" : "interaction.end");
  resetAccumulation();
}

// Returns the currently active progressive render scale.
int OsprayBackend::currentScale() const
{
  return passScale_;
}

// Reports whether automatic quality adaptation is currently active.
bool OsprayBackend::dynamicModeActive() const
{
  return dynamicModeActive_;
}

// Reports whether AO backoff was applied to recover interactivity.
bool OsprayBackend::backoffApplied() const
{
  return backoffApplied_;
}

// Pushes the current AO/pixel sample settings into the OSPRay renderer object.
void OsprayBackend::applyRendererSamplingParams(int aoSamples,
    float aoDistance,
    int pixelSamples,
    int maxPathLength,
    int roulettePathLength)
{
  const int clampedAo = std::clamp(aoSamples, 0, kMaxSafeAoSamples);
  const float clampedAoDistance = std::clamp(aoDistance, 0.0f, 1e20f);
  const int clampedPixel = std::clamp(pixelSamples, 1, kMaxSafePixelSamples);
  const int clampedMaxPathLength = std::clamp(maxPathLength, 0, 64);
  const int clampedRoulettePathLength = std::clamp(roulettePathLength, 0, 64);
  if (clampedAo == appliedAoSamples_
      && std::fabs(clampedAoDistance - appliedAoDistance_) < 0.001f
      && clampedPixel == appliedPixelSamples_
      && clampedMaxPathLength == appliedMaxPathLength_
      && clampedRoulettePathLength == appliedRoulettePathLength_) {
    return;
  }

  renderer_.setParam("aoSamples", clampedAo);
  renderer_.setParam("aoDistance", clampedAoDistance);
  renderer_.setParam("pixelSamples", clampedPixel);
  renderer_.setParam("maxPathLength", clampedMaxPathLength);
  renderer_.setParam("roulettePathLength", clampedRoulettePathLength);
  renderer_.commit();
  appliedAoSamples_ = clampedAo;
  appliedAoDistance_ = clampedAoDistance;
  appliedPixelSamples_ = clampedPixel;
  appliedMaxPathLength_ = clampedMaxPathLength;
  appliedRoulettePathLength_ = clampedRoulettePathLength;
}

// Clamps a requested render scale to the supported progressive scale ladder.
int OsprayBackend::sanitizeScale(int scale) const
{
  int bestScale = kProgressiveScales.front();
  int bestDistance = std::abs(scale - bestScale);
  for (const int candidate : kProgressiveScales) {
    const int distance = std::abs(scale - candidate);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestScale = candidate;
    }
  }
  return bestScale;
}

// Converts a scale value into its corresponding progressive ladder index.
int OsprayBackend::scaleToIndex(int scale) const
{
  const int sanitized = sanitizeScale(scale);
  for (size_t i = 0; i < kProgressiveScales.size(); ++i) {
    if (kProgressiveScales[i] == sanitized)
      return int(i);
  }
  return int(kProgressiveScales.size()) - 1;
}

// Returns the initial progressive scale for the current quality mode.
int OsprayBackend::startScaleForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customStartScale_;

  switch (automaticPreset_) {
  case AutomaticPreset::Fast:
    return 16;
  case AutomaticPreset::Balanced:
    return 8;
  case AutomaticPreset::Quality:
    return 4;
  }
  return 8;
}

// Returns the target frame time for the current quality mode.
float OsprayBackend::targetFrameTimeForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom) ? customTargetFrameTimeMs_
                                                 : automaticTargetFrameTimeMs_;
}

// Reports whether accumulation is enabled in the active quality mode.
bool OsprayBackend::accumulationEnabledForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom) ? customAccumulationEnabled_
                                                 : automaticAccumulationEnabled_;
}

// Returns the maximum accumulation frame count for the active quality mode.
int OsprayBackend::maxAccumulationFramesForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom) ? customMaxAccumulationFrames_ : 0;
}

// Returns the watchdog timeout for the active quality mode.
int OsprayBackend::watchdogTimeoutForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom) ? customWatchdogTimeoutMs_
                                                 : kDefaultWatchdogMs;
}

// Returns the AO sample count currently requested by the active quality mode.
int OsprayBackend::configuredAoSamplesForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customAoSamples_;

  switch (automaticPreset_) {
  case AutomaticPreset::Fast:
    return 0;
  case AutomaticPreset::Balanced:
    return 1;
  case AutomaticPreset::Quality:
    return 2;
  }
  return 1;
}

// Returns the AO distance requested by the active quality mode.
float OsprayBackend::configuredAoDistanceForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customAoDistance_;
  return 1e20f;
}

// Returns the pixel sample count currently requested by the active quality mode.
int OsprayBackend::configuredPixelSamplesForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customPixelSamples_;

  switch (automaticPreset_) {
  case AutomaticPreset::Fast:
    return 1;
  case AutomaticPreset::Balanced:
    return 1;
  case AutomaticPreset::Quality:
    return 2;
  }
  return 1;
}

// Returns the maximum recursive path depth requested by the active quality mode.
int OsprayBackend::configuredMaxPathLengthForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customMaxPathLength_;
  return 20;
}

// Returns the Russian-roulette start depth requested by the active quality mode.
int OsprayBackend::configuredRoulettePathLengthForCurrentMode() const
{
  if (settingsMode_ == SettingsMode::Custom)
    return customRoulettePathLength_;
  return 5;
}

// Reports whether only full-resolution passes may accumulate in the active mode.
bool OsprayBackend::fullResAccumulationOnlyForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom) ? customFullResAccumulationOnly_
                                                 : true;
}

// Reports whether the active mode should drop quality during interaction.
bool OsprayBackend::lowQualityWhileInteractingForCurrentMode() const
{
  return (settingsMode_ == SettingsMode::Custom)
      ? customLowQualityWhileInteracting_
      : true;
}

// Cancels the currently running frame future and records why it was interrupted.
void OsprayBackend::cancelInFlightFrame(const char *reason)
{
  if (frameInFlight_) {
    if (activeRenderRequest_)
      logRenderRequest("cancel", *activeRenderRequest_, reason);
    if (currentFrame_.handle())
      currentFrame_.cancel();
  }

  frameInFlight_ = false;
  currentFrame_ = ospray::cpp::Future();
  inFlightStartValid_ = false;
  activeRenderRequest_.reset();
}

// Chooses the next render request type based on interaction and progressive state.
OsprayBackend::RenderRequestType OsprayBackend::currentRenderRequestType() const
{
  if (isInteracting_)
    return RenderRequestType::Preview;
  return renderPhase_ == RenderPhase::Accumulate ? RenderRequestType::Full
                                                 : RenderRequestType::Progressive;
}

const char *OsprayBackend::renderRequestTypeName(RenderRequestType type) const
{
  switch (type) {
  case RenderRequestType::Preview:
    return "preview";
  case RenderRequestType::Progressive:
    return "progressive";
  case RenderRequestType::Full:
    return "full";
  }
  return "unknown";
}

// Emits a diagnostic log line for render request scheduling and completion.
void OsprayBackend::logRenderRequest(const char *event,
    const RenderRequest &request,
    const char *reason) const
{
  if (!verboseRenderLoggingEnabled())
    return;

  std::fprintf(stderr,
      "IBRT render %s: id=%llu type=%s camera=%llu phase=%s scale=%d%s%s\n",
      event,
      static_cast<unsigned long long>(request.id),
      renderRequestTypeName(request.type),
      static_cast<unsigned long long>(request.cameraVersion),
      renderPhase_ == RenderPhase::Accumulate ? "accumulate" : "progressive",
      passScale_,
      reason ? " reason=" : "",
      reason ? reason : "");
}

// Stores the latest requested render work, replacing any older pending request.
void OsprayBackend::enqueueLatestRenderRequest(const char *reason)
{
  RenderRequest request;
  request.id = nextRenderRequestId_++;
  request.cameraVersion = cameraVersion_;
  request.type = isInteracting_ ? RenderRequestType::Preview
                                : RenderRequestType::Progressive;
  pendingRenderRequest_ = request;
  logRenderRequest("request", request, reason);
}

// Sets the current progressive scale and updates derived pass dimensions.
void OsprayBackend::setProgressiveScale(int scale)
{
  renderPhase_ = RenderPhase::Progressive;
  currentScaleIndex_ = scaleToIndex(scale);
  progressiveFramesAtCurrentScale_ = 0;
  passScale_ = kProgressiveScales[currentScaleIndex_];
  const int targetW = std::max(1, (fbW_ + passScale_ - 1) / passScale_);
  const int targetH = std::max(1, (fbH_ + passScale_ - 1) / passScale_);
  if (targetW != passW_ || targetH != passH_) {
    passW_ = targetW;
    passH_ = targetH;
    passPixels_.assign(size_t(passW_) * size_t(passH_), 0u);
    passFb_ = ospray::cpp::FrameBuffer();
  } else if (passPixels_.size() != size_t(passW_) * size_t(passH_)) {
    passPixels_.assign(size_t(passW_) * size_t(passH_), 0u);
    passFb_ = ospray::cpp::FrameBuffer();
  }
}

// Rebuilds the default light list for the current world and renderer.
void OsprayBackend::applyDefaultLights()
{
  world_.setParam("light",
      ospray::cpp::CopiedData(
          makeDefaultLights(currentRenderer_, environmentVisible_, worldUp_)));
}

// Controls whether the path-tracer sky/sun environment is drawn behind the
// scene. Illumination is unaffected; hiding it yields a clean (white) background
// for offline stills and presentation renders.
void OsprayBackend::setEnvironmentVisible(bool visible)
{
  if (environmentVisible_ == visible)
    return;
  environmentVisible_ = visible;
  applyDefaultLights();
  world_.commit();
  resetAccumulation();
}

bool OsprayBackend::environmentVisible() const
{
  return environmentVisible_;
}

// Applies renderer-specific defaults such as AO and sampling parameters.
void OsprayBackend::applyRendererDefaults()
{
  renderer_.setParam("backgroundColor",
      vec4f(backgroundColor_.x,
          backgroundColor_.y,
          backgroundColor_.z,
          1.0f));
  renderer_.setParam("pixelSamples", configuredPixelSamplesForCurrentMode());
  renderer_.setParam("aoSamples", configuredAoSamplesForCurrentMode());
  renderer_.setParam("aoDistance", configuredAoDistanceForCurrentMode());
  renderer_.setParam("maxPathLength", configuredMaxPathLengthForCurrentMode());
  renderer_.setParam(
      "roulettePathLength", configuredRoulettePathLengthForCurrentMode());

  if (currentRenderer_ == "scivis") {
    renderer_.setParam("shadows", true);
    renderer_.setParam("visibleLights", false);
  } else if (currentRenderer_ == "ao") {
    renderer_.setParam("aoIntensity", 1.0f);
    renderer_.setParam(
        "lightDirection", -orientYUpDirection(kSunLightDirection, worldUp_));
    renderer_.setParam("ambientIntensity", 0.18f);
    renderer_.setParam("directionalIntensity", 0.82f);
  }

  renderer_.commit();
  appliedAoDistance_ = configuredAoDistanceForCurrentMode();
  appliedMaxPathLength_ = configuredMaxPathLengthForCurrentMode();
  appliedRoulettePathLength_ = configuredRoulettePathLengthForCurrentMode();
}

// Assigns a fallback material to geometry that does not provide one.
void OsprayBackend::applyDefaultMaterial(ospray::cpp::GeometricModel &model)
{
  ospray::cpp::Material material("obj");
  material.setParam("kd", vec3f(0.8f, 0.8f, 0.8f));
  material.commit();
  model.setParam("material", material);
}

// Commits the current instance list to the OSPRay world object.
void OsprayBackend::applyWorldInstances()
{
  world_.setParam("instance", ospray::cpp::CopiedData(sceneInstances_));
}

// Resets progressive counters, framebuffers, and optionally the display image.
void OsprayBackend::resetProgressiveState(bool clearDisplay)
{
  setProgressiveScale(startScaleForCurrentMode());
  slowPassStreak_ = 0;
  accumulatedFrames_ = 0;
  slowFrameStreak_ = 0;
  aoBackoffSteps_ = 0;
  backoffApplied_ = false;
  watchdogTriggered_ = false;
  progressiveFramesAtCurrentScale_ = 0;
  accumBlendFrame_ = 0;

  if (clearDisplay)
    std::fill(displayPixels_.begin(), displayPixels_.end(), 0u);
  if (accumFb_.handle())
    accumFb_.resetAccumulation();
}

// Updates the camera crop window for tiled/progressive rendering passes.
void OsprayBackend::updateCameraCrop(const vec2f &imageStart, const vec2f &imageEnd)
{
  camera_.setParam("imageStart", imageStart);
  camera_.setParam("imageEnd", imageEnd);
}

// Advances the progressive state machine to the next scale or accumulation phase.
void OsprayBackend::beginNextProgressivePass()
{
  const int lastIndex = int(kProgressiveScales.size()) - 1;
  currentScaleIndex_ = std::min(currentScaleIndex_ + 1, lastIndex);
  progressiveFramesAtCurrentScale_ = 0;
  if (currentScaleIndex_ >= lastIndex)
    renderPhase_ = RenderPhase::Accumulate;

  passScale_ = kProgressiveScales[currentScaleIndex_];
  const int targetW = std::max(1, (fbW_ + passScale_ - 1) / passScale_);
  const int targetH = std::max(1, (fbH_ + passScale_ - 1) / passScale_);
  if (targetW != passW_ || targetH != passH_) {
    passW_ = targetW;
    passH_ = targetH;
    passPixels_.assign(size_t(passW_) * size_t(passH_), 0u);
    passFb_ = ospray::cpp::FrameBuffer();
  } else if (passPixels_.size() != size_t(passW_) * size_t(passH_)) {
    passPixels_.assign(size_t(passW_) * size_t(passH_), 0u);
    passFb_ = ospray::cpp::FrameBuffer();
  }
}

// Allocates the per-pass framebuffer used for the current progressive scale.
void OsprayBackend::prepareTileFrameBuffer(int tileW, int tileH)
{
  if (!passFb_.handle() || tileW != passW_ || tileH != passH_)
    passFb_ = ospray::cpp::FrameBuffer(tileW, tileH, OSP_FB_SRGBA, OSP_FB_COLOR);
}

// (Re)creates the full-resolution accumulation framebuffer.  When denoising is
// enabled (and the OIDN module loaded) it adds the albedo/normal guide channels
// and attaches the "denoiser" image operation.  The mapped color output stays
// OSP_FB_SRGBA - the denoiser runs on OSPRay's internal float buffers, so the
// rest of the display pipeline is unaffected.  Only the full-res accumulation
// pass is denoised; the low-res progressive preview passes are left untouched.
void OsprayBackend::rebuildAccumFrameBuffer()
{
  int channels = OSP_FB_COLOR | OSP_FB_ACCUM;
  const bool useDenoiser = denoiseEnabled_ && denoiserModuleAvailable_;
  if (useDenoiser)
    channels |= OSP_FB_ALBEDO | OSP_FB_NORMAL;

  accumFb_ = ospray::cpp::FrameBuffer(fbW_, fbH_, OSP_FB_SRGBA, channels);

  if (useDenoiser) {
    ospray::cpp::ImageOperation denoiser("denoiser");
    accumFb_.setParam("imageOperation",
        ospray::cpp::CopiedData(
            std::vector<ospray::cpp::ImageOperation>{denoiser}));
    accumFb_.commit();
  }
}

// Starts the next asynchronous OSPRay frame render based on pending state.
bool OsprayBackend::startNextRenderWork()
{
  RenderRequest request = pendingRenderRequest_.value_or(RenderRequest{
      nextRenderRequestId_++, cameraVersion_, currentRenderRequestType()});
  request.type = currentRenderRequestType();
  request.cameraVersion = cameraVersion_;

  if (renderPhase_ == RenderPhase::Accumulate) {
    if (!accumFb_.handle())
      return false;
    activeRenderRequest_ = request;
    pendingRenderRequest_.reset();
    logRenderRequest("start", request);
    updateCameraCrop(vec2f(0.f, 0.f), vec2f(1.f, 1.f));
    camera_.commit();
    currentFrame_ = accumFb_.renderFrame(renderer_, camera_, world_);
    frameInFlight_ = true;
    inFlightStart_ = std::chrono::steady_clock::now();
    inFlightStartValid_ = true;
    return true;
  }

  prepareTileFrameBuffer(passW_, passH_);
  activeRenderRequest_ = request;
  pendingRenderRequest_.reset();
  logRenderRequest("start", request);
  updateCameraCrop(vec2f(0.f, 0.f), vec2f(1.f, 1.f));
  camera_.commit();
  currentFrame_ = passFb_.renderFrame(renderer_, camera_, world_);
  frameInFlight_ = true;
  inFlightStart_ = std::chrono::steady_clock::now();
  inFlightStartValid_ = true;
  return true;
}

// Blends two packed sRGBA8 pixels. weight is in [0,256]; 0 yields base, 256
// yields next. Interpolation happens in sRGB byte space, which is an
// approximation but is visually smooth enough for a transition crossfade.
static inline uint32_t blendSrgba(uint32_t base, uint32_t next, int weight)
{
  const int iw = 256 - weight;
  const uint32_t r = (((base) & 0xFF) * iw + ((next) & 0xFF) * weight) >> 8;
  const uint32_t g =
      (((base >> 8) & 0xFF) * iw + ((next >> 8) & 0xFF) * weight) >> 8;
  const uint32_t b =
      (((base >> 16) & 0xFF) * iw + ((next >> 16) & 0xFF) * weight) >> 8;
  const uint32_t a =
      (((base >> 24) & 0xFF) * iw + ((next >> 24) & 0xFF) * weight) >> 8;
  return r | (g << 8) | (b << 16) | (a << 24);
}

// Finalizes a completed frame, copies pixels, and updates quality heuristics.
bool OsprayBackend::finishCompletedRender()
{
  if (!frameInFlight_ || !currentFrame_.handle())
    return false;

  currentFrame_.wait(OSP_FRAME_FINISHED);
  lastFrameTimeMs_ = currentFrame_.duration() * 1000.0f;
  watchdogTriggered_ = false;

  bool updatedImage = false;

  if (renderPhase_ == RenderPhase::Progressive) {
    void *mapped = passFb_.map(OSP_FB_COLOR);
    std::memcpy(passPixels_.data(),
        mapped,
        size_t(passW_) * size_t(passH_) * sizeof(uint32_t));
    passFb_.unmap(mapped);

    upsamplePassToDisplay();
    ++accumulatedFrames_;
    updatedImage = true;
    if (!isInteracting_)
      beginNextProgressivePass();
  } else {
    void *mapped = accumFb_.map(OSP_FB_COLOR);
    const uint32_t *accum = static_cast<const uint32_t *>(mapped);
    const size_t pixelCount = displayPixels_.size();

    if (accumBlendFrame_ == 0) {
      // displayPixels_ still holds the last AO-free progressive frame; capture
      // it as the crossfade base before the accumulating result overwrites it.
      crossfadePixels_ = displayPixels_;
    }

    if (accumBlendFrame_ < kAccumBlendFrames
        && crossfadePixels_.size() == pixelCount) {
      // Ramp the weight 0 -> 256 across kAccumBlendFrames frames so the AO
      // shading fades in rather than popping. Because early accumulation frames
      // are mostly hidden behind the base, their single-sample noise is
      // attenuated while the buffer converges.
      const int weight =
          std::min(256, 256 * (accumBlendFrame_ + 1) / kAccumBlendFrames);
      for (size_t i = 0; i < pixelCount; ++i)
        displayPixels_[i] = blendSrgba(crossfadePixels_[i], accum[i], weight);
    } else {
      std::memcpy(displayPixels_.data(), accum, pixelCount * sizeof(uint32_t));
    }

    accumFb_.unmap(mapped);
    ++accumBlendFrame_;
    ++accumulatedFrames_;
    updatedImage = true;
  }

  frameInFlight_ = false;
  inFlightStartValid_ = false;
  currentFrame_ = ospray::cpp::Future();
  if (renderPhase_ == RenderPhase::Progressive)
    applyAoBackoff(false);
  if (activeRenderRequest_) {
    char reason[64];
    std::snprintf(reason, sizeof(reason), "frameMs=%.2f", lastFrameTimeMs_);
    logRenderRequest("finish", *activeRenderRequest_, reason);
  }
  activeRenderRequest_.reset();
  return updatedImage;
}

// Applies queued camera, resize, renderer, and reset changes between frames.
void OsprayBackend::applyPendingState()
{
  if (frameInFlight_)
    return;

  if (pendingResize_) {
    fbW_ = std::max(1, pendingResizeW_);
    fbH_ = std::max(1, pendingResizeH_);
    camera_.setParam("aspect", float(fbW_) / float(fbH_));
    cameraDirty_ = true;
    rebuildAccumFrameBuffer();
    displayPixels_.assign(size_t(fbW_) * size_t(fbH_), 0u);
    resetProgressiveState(true);
    pendingResize_ = false;
    pendingAccumRebuild_ = false;
  }

  if (pendingAccumRebuild_) {
    // The denoiser toggle changed which channels / image operations the
    // accumulation buffer needs, so rebuild it and restart accumulation.
    rebuildAccumFrameBuffer();
    resetProgressiveState(false);
    pendingAccumRebuild_ = false;
  }

  if (pendingRendererType_) {
    renderer_ = ospray::cpp::Renderer(*pendingRendererType_);
    currentRenderer_ = *pendingRendererType_;
    applyRendererDefaults();
    appliedAoSamples_ = configuredAoSamplesForCurrentMode();
    appliedPixelSamples_ = configuredPixelSamplesForCurrentMode();
    if (world_.handle()) {
      applyWorldInstances();
      applyDefaultLights();
      world_.commit();
    }
    pendingRendererType_.reset();
    pendingResetAccumulation_ = true;
  }

  if (pendingCameraState_) {
    cameraState_ = *pendingCameraState_;
    applyCameraParams();
    pendingCameraState_.reset();
    pendingResetAccumulation_ = true;
  }

  if (pendingProjectionRebuild_) {
    // The camera object must be recreated for the new projection type; do it
    // here so it never races an in-flight frame.
    rebuildCameraForProjection();
    pendingProjectionRebuild_ = false;
    pendingResetAccumulation_ = true;
  }

  if (pendingResetAccumulation_) {
    resetProgressiveState(false);
    pendingResetAccumulation_ = false;
  }
}

// Upsamples the latest progressive pass into the display-sized pixel buffer.
void OsprayBackend::upsamplePassToDisplay()
{
  if (passPixels_.empty() || displayPixels_.empty())
    return;

  for (int y = 0; y < fbH_; ++y) {
    const int srcY = std::min(passH_ - 1, y / passScale_);
    uint32_t *dstRow = displayPixels_.data() + size_t(y) * size_t(fbW_);
    const uint32_t *srcRow = passPixels_.data() + size_t(srcY) * size_t(passW_);

    for (int x = 0; x < fbW_; ++x)
      dstRow[x] = srcRow[std::min(passW_ - 1, x / passScale_)];
  }
}

// Reduces AO cost after repeated slow frames or watchdog-triggered stalls.
void OsprayBackend::applyAoBackoff(bool forcedByWatchdog)
{
  const int configuredAo = configuredAoSamplesForCurrentMode();
  if (configuredAo <= 0)
    return;

  const float frameThreshold = std::max(30.0f, targetFrameTimeForCurrentMode() * 1.8f);

  if (forcedByWatchdog) {
    slowFrameStreak_ = kAoBackoffStreak;
  } else if (lastFrameTimeMs_ > frameThreshold) {
    ++slowFrameStreak_;
  } else {
    slowFrameStreak_ = 0;
    return;
  }

  if (slowFrameStreak_ < kAoBackoffStreak)
    return;

  if (aoBackoffSteps_ >= configuredAo) {
    slowFrameStreak_ = 0;
    return;
  }

  ++aoBackoffSteps_;
  backoffApplied_ = true;
  ++aoAutoReductionCount_;
  slowFrameStreak_ = 0;
}

int& OsprayBackend::getAoSamples()
{
  return customAoSamples_;
}

// Returns the last backend error string.
const std::string &OsprayBackend::lastError() const
{
  return lastError_;
}

// Stores the latest backend error string.
void OsprayBackend::setError(std::string message)
{
  lastError_ = std::move(message);
}

// Returns the number of accumulated frames produced for the current view.
uint64_t OsprayBackend::accumulatedFrames() const
{
  return accumulatedFrames_;
}

// Returns how many frames were actually cancelled by the render watchdog.
uint64_t OsprayBackend::watchdogCancelCount() const
{
  return watchdogCancelCount_;
}

// Returns how many times AO quality was reduced automatically.
uint64_t OsprayBackend::aoAutoReductionCount() const
{
  return aoAutoReductionCount_;
}

// Enumerates selectable BRL-CAD object names from a database file.
std::vector<std::string> OsprayBackend::listBrlcadObjects(
    const std::string &path) const
{
  std::vector<std::string> names;
  if (path.empty())
    return names;

  rt_i *tmpRtip = rt_dirbuild(path.c_str(), nullptr, 0);
  if (!tmpRtip || !tmpRtip->rti_dbip)
    return names;

  directory **dpv = nullptr;
  const size_t count = db_ls(tmpRtip->rti_dbip, DB_LS_TOPS, nullptr, &dpv);

  for (size_t i = 0; i < count; ++i) {
    if (dpv[i] && dpv[i]->d_namep && *dpv[i]->d_namep)
      names.emplace_back(dpv[i]->d_namep);
  }

  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());

  if (dpv)
    bu_free(dpv, "db_ls object list");
  rt_free_rti(tmpRtip);
  return names;
}

void OsprayBackend::setVisualizationMode(VisualizationMode mode)
{
  visualizationMode_ = mode;
}

OsprayBackend::VisualizationMode OsprayBackend::visualizationMode() const
{
  return visualizationMode_;
}

// Builds OSPRay linear curves from the line segments emitted by each
// primitive's BRL-CAD ft_plot implementation.
bool OsprayBackend::loadBrlcadWireframe(
    const std::string &path, const std::string &topObject)
{
  rt_i *rtip = rt_dirbuild(path.c_str(), nullptr, 0);
  if (!rtip || !rtip->rti_dbip) {
    if (rtip) rt_free_rti(rtip);
    setError("Unable to open the BRL-CAD database for wireframe plotting.");
    return false;
  }

  struct WireframeBatch
  {
    vec3f color;
    std::vector<vec3f> points;
  };
  std::vector<WireframeBatch> batches;
  std::unordered_set<std::string> ancestry;
  bg_tess_tol ttol{};
  ttol.magic = BG_TESS_TOL_MAGIC;
  ttol.rel = 0.01;
  bn_tol tol = BN_TOL_INIT_TOL;
  std::function<void(const directory *, const mat_t, db_full_path *)> plotDirectory;
  std::function<void(const union tree *, const mat_t, db_full_path *)> plotTree;

  plotDirectory = [&](const directory *dp, const mat_t matrix, db_full_path *pathp) {
    if (!dp || !dp->d_namep || !ancestry.insert(dp->d_namep).second) return;
    db_add_node_to_full_path(pathp, const_cast<directory *>(dp));
    rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    // Combination transforms live on their leaf nodes.  Apply the accumulated
    // matrix only when importing a primitive, otherwise nested transforms
    // would be folded into the tree and then applied a second time below.
    const fastf_t *importMatrix = (dp->d_flags & RT_DIR_COMB) ? nullptr : matrix;
    if (rt_db_get_internal(&intern, dp, rtip->rti_dbip, importMatrix) < 0) {
      ancestry.erase(dp->d_namep);
      DB_FULL_PATH_POP(pathp);
      return;
    }
    if (intern.idb_type == ID_COMBINATION) {
      const auto *comb = static_cast<const rt_comb_internal *>(intern.idb_ptr);
      if (comb) plotTree(comb->tree, matrix, pathp);
    } else if (intern.idb_meth && intern.idb_meth->ft_plot) {
      WireframeBatch batch;
      bu_color pathColor = BU_COLOR_INIT_ZERO;
      fastf_t rgb[3] = {1.0, 0.0, 0.0};
      db_full_path_color(&pathColor, pathp, rtip->rti_dbip);
      bu_color_to_rgb_floats(&pathColor, rgb);
      batch.color = vec3f(float(rgb[0]), float(rgb[1]), float(rgb[2]));
      bu_list vhead;
      BU_LIST_INIT(&vhead);
      if (intern.idb_meth->ft_plot(&vhead, &intern, &ttol, &tol, nullptr) >= 0) {
        bool havePrevious = false;
        vec3f previous;
        bv_vlist *vp;
        for (BU_LIST_FOR(vp, bv_vlist, &vhead)) {
          for (size_t i = 0; i < vp->nused; ++i) {
            const int cmd = vp->cmd[i];
            const vec3f p(float(vp->pt[i][0]), float(vp->pt[i][1]), float(vp->pt[i][2]));
            if (cmd == BV_VLIST_LINE_MOVE || cmd == BV_VLIST_POLY_MOVE
                || cmd == BV_VLIST_TRI_MOVE) {
              previous = p;
              havePrevious = true;
            } else if (havePrevious && (cmd == BV_VLIST_LINE_DRAW
                           || cmd == BV_VLIST_POLY_DRAW || cmd == BV_VLIST_POLY_END
                           || cmd == BV_VLIST_TRI_DRAW || cmd == BV_VLIST_TRI_END)) {
              batch.points.push_back(previous);
              batch.points.push_back(p);
              previous = p;
            }
          }
        }
      }
      bv_vlist_cleanup(&vhead);
      if (!batch.points.empty())
        batches.push_back(std::move(batch));
    }
    rt_db_free_internal(&intern);
    ancestry.erase(dp->d_namep);
    DB_FULL_PATH_POP(pathp);
  };
  plotTree = [&](const union tree *tree, const mat_t parent, db_full_path *pathp) {
    if (!tree) return;
    if (tree->tr_op == OP_DB_LEAF) {
      mat_t combined;
      if (tree->tr_l.tl_mat) bn_mat_mul(combined, parent, tree->tr_l.tl_mat);
      else MAT_COPY(combined, parent);
      plotDirectory(
          db_lookup(rtip->rti_dbip, tree->tr_l.tl_name, LOOKUP_QUIET), combined, pathp);
      return;
    }
    plotTree(tree->tr_b.tb_left, parent, pathp);
    if (tree->tr_op == OP_UNION || tree->tr_op == OP_INTERSECT
        || tree->tr_op == OP_SUBTRACT || tree->tr_op == OP_XOR)
      plotTree(tree->tr_b.tb_right, parent, pathp);
  };

  mat_t identity;
  MAT_IDN(identity);
  db_full_path pathState = DB_FULL_PATH_INIT_ZERO;
  if (!topObject.empty() && topObject != "all") {
    plotDirectory(
        db_lookup(rtip->rti_dbip, topObject.c_str(), LOOKUP_QUIET), identity, &pathState);
  } else {
    directory **tops = nullptr;
    const size_t count = db_ls(rtip->rti_dbip, DB_LS_TOPS, nullptr, &tops);
    for (size_t i = 0; i < count; ++i) plotDirectory(tops[i], identity, &pathState);
    if (tops) bu_free(tops, "wireframe top objects");
  }
  db_free_full_path(&pathState);
  rt_free_rti(rtip);
  if (batches.empty()) {
    setError("BRL-CAD ft_plot produced no wireframe segments.");
    return false;
  }

  boundsMin_ = boundsMax_ = batches.front().points.front();
  for (const WireframeBatch &batch : batches) for (const vec3f &p : batch.points) {
    boundsMin_.x = std::min(boundsMin_.x, p.x); boundsMin_.y = std::min(boundsMin_.y, p.y); boundsMin_.z = std::min(boundsMin_.z, p.z);
    boundsMax_.x = std::max(boundsMax_.x, p.x); boundsMax_.y = std::max(boundsMax_.y, p.y); boundsMax_.z = std::max(boundsMax_.z, p.z);
  }
  const float radius = std::max(getBoundsMaxExtent() * 0.00075f, 1e-5f);
  std::vector<ospray::cpp::GeometricModel> models;
  models.reserve(batches.size());
  for (const WireframeBatch &batch : batches) {
    std::vector<vec4f> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(batch.points.size());
    for (size_t i = 0; i < batch.points.size(); i += 2) {
      indices.push_back(uint32_t(i));
      vertices.emplace_back(batch.points[i].x, batch.points[i].y, batch.points[i].z, radius);
      vertices.emplace_back(batch.points[i + 1].x, batch.points[i + 1].y,
          batch.points[i + 1].z, radius);
    }
    ospray::cpp::Geometry curves("curve");
    curves.setParam("vertex.position_radius", ospray::cpp::CopiedData(vertices));
    curves.setParam("index", ospray::cpp::CopiedData(indices));
    curves.setParam("type", OSP_ROUND);
    curves.setParam("basis", OSP_LINEAR);
    curves.commit();
    ospray::cpp::GeometricModel model(curves);
    ospray::cpp::Material material("obj");
    material.setParam("kd", batch.color);
    material.commit();
    model.setParam("material", material);
    model.commit();
    models.push_back(model);
  }
  ospray::cpp::Group group;
  group.setParam("geometry", ospray::cpp::CopiedData(models));
  group.commit();
  ospray::cpp::Instance instance(group);
  instance.commit();
  sceneInstances_ = {instance};
  world_ = ospray::cpp::World();
  applyWorldInstances();
  applyDefaultLights();
  world_.commit();
  resetAccumulation();
  return true;
}

// Builds a BRL-CAD object hierarchy suitable for UI browsing.
std::vector<OsprayBackend::BrlcadNode> OsprayBackend::getBrlcadHierarchy(
    const std::string &path) const
{
  std::vector<BrlcadNode> roots;
  if (path.empty())
    return roots;

  rt_i *tmpRtip = rt_dirbuild(path.c_str(), nullptr, 0);
  if (!tmpRtip || !tmpRtip->rti_dbip)
    return roots;

  struct CleanupGuard
  {
    rt_i *rtip = nullptr;
    directory **dpv = nullptr;
    ~CleanupGuard()
    {
      if (dpv)
        bu_free(dpv, "db_ls hierarchy roots");
      if (rtip)
        rt_free_rti(rtip);
    }
  } cleanup{tmpRtip, nullptr};

  directory **dpv = nullptr;
  cleanup.dpv = dpv;
  const size_t count =
      db_ls(tmpRtip->rti_dbip, DB_LS_TOPS | DB_LS_COMB | DB_LS_REGION, nullptr, &dpv);
  cleanup.dpv = dpv;

  std::unordered_set<std::string> referencedNames;
  std::function<BrlcadNode(const directory *, std::unordered_set<std::string> &)> buildDirectoryNode;
  std::function<void(const union tree *, BrlcadNode &, std::unordered_set<std::string> &)> appendTreeChildren;
  std::function<void(const union tree *)> collectReferencedNames;

  collectReferencedNames = [&](const union tree *tree) {
    if (!tree)
      return;

    switch (tree->tr_op) {
    case OP_DB_LEAF:
      if (tree->tr_l.tl_name && *tree->tr_l.tl_name)
        referencedNames.insert(tree->tr_l.tl_name);
      return;
    case OP_UNION:
    case OP_INTERSECT:
    case OP_SUBTRACT:
    case OP_XOR:
      collectReferencedNames(tree->tr_b.tb_left);
      collectReferencedNames(tree->tr_b.tb_right);
      return;
    case OP_NOT:
    case OP_GUARD:
    case OP_XNOP:
      collectReferencedNames(tree->tr_b.tb_left);
      return;
    default:
      return;
    }
  };

  buildDirectoryNode = [&](const directory *dp,
                           std::unordered_set<std::string> &ancestry) -> BrlcadNode {
    BrlcadNode node;
    if (!dp || !dp->d_namep)
      return node;

    node.name = dp->d_namep;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, tmpRtip->rti_dbip, nullptr) < 0) {
      node.isPrimitive = true;
      return node;
    }

    const bool isCombination = intern.idb_type == ID_COMBINATION;
    node.isCombination = isCombination;
    node.isPrimitive = !isCombination;

    if (isCombination) {
      const auto *comb = static_cast<const rt_comb_internal *>(intern.idb_ptr);
      if (comb) {
        node.isRegion = comb->region_flag != 0;
        if (ancestry.insert(node.name).second) {
          appendTreeChildren(comb->tree, node, ancestry);
          ancestry.erase(node.name);
        }
      }
    }

    rt_db_free_internal(&intern);
    return node;
  };

  appendTreeChildren = [&](const union tree *tree,
                           BrlcadNode &parent,
                           std::unordered_set<std::string> &ancestry) {
    if (!tree)
      return;

    switch (tree->tr_op) {
    case OP_DB_LEAF: {
      if (!tree->tr_l.tl_name || !*tree->tr_l.tl_name)
        return;
      const directory *leafDp = db_lookup(tmpRtip->rti_dbip, tree->tr_l.tl_name, LOOKUP_QUIET);
      if (!leafDp)
        return;
      parent.children.push_back(buildDirectoryNode(leafDp, ancestry));
      return;
    }
    case OP_UNION:
    case OP_INTERSECT:
    case OP_SUBTRACT:
    case OP_XOR:
      appendTreeChildren(tree->tr_b.tb_left, parent, ancestry);
      appendTreeChildren(tree->tr_b.tb_right, parent, ancestry);
      return;
    case OP_NOT:
    case OP_GUARD:
    case OP_XNOP:
      appendTreeChildren(tree->tr_b.tb_left, parent, ancestry);
      return;
    default:
      return;
    }
  };

  for (size_t i = 0; i < count; ++i) {
    if (!dpv[i] || !dpv[i]->d_namep || !*dpv[i]->d_namep)
      continue;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dpv[i], tmpRtip->rti_dbip, nullptr) < 0)
      continue;

    if (intern.idb_type == ID_COMBINATION) {
      const auto *comb = static_cast<const rt_comb_internal *>(intern.idb_ptr);
      if (comb)
        collectReferencedNames(comb->tree);
    }

    rt_db_free_internal(&intern);
  }

  std::unordered_set<std::string> rootNames;
  for (size_t i = 0; i < count; ++i) {
    if (!dpv[i] || !dpv[i]->d_namep || !*dpv[i]->d_namep)
      continue;
    const std::string rootName = dpv[i]->d_namep;
    if (referencedNames.find(rootName) != referencedNames.end())
      continue;
    if (!rootNames.insert(rootName).second)
      continue;
    std::unordered_set<std::string> ancestry;
    roots.push_back(buildDirectoryNode(dpv[i], ancestry));
  }

  std::sort(roots.begin(), roots.end(), [](const BrlcadNode &a, const BrlcadNode &b) {
    return a.name < b.name;
  });

  return roots;
}

std::vector<OsprayBackend::BrlcadNode> OsprayBackend::listBrlcadHierarchy(
    const std::string &path) const
{
  return getBrlcadHierarchy(path);
}
