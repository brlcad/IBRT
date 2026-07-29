// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Pure, testable camera math extracted from RenderWidget.
// These are the up-axis-dependent conversions that the
// Qt6.5 image-flip and Y-flip fixes kept having to touch, plus the standard
// view directions needed for the "orthographic / standard views" feature
// (improvement-plan #1) and the framing distance used by Reset View.
//
// The functions are templated on a 3-component vector type V that exposes public
// .x/.y/.z members and a V(x,y,z) constructor. That matches rkcommon::math::vec3f
// (used throughout the app) as well as a trivial test vector, so this header has
// no dependency on OSPRay/rkcommon and can be unit-tested standalone.
//
// Conventions (identical to the current RenderWidget code):
//   * Up axis is either Y or Z. worldUp() is the up vector; worldForwardReference()
//     is the in-plane "north" reference; rightReference() = normalize(cross(north, up)).
//   * Orbit: eye direction = north*cosθ*sinφ + right*sinθ*sinφ + up*cosφ, where φ is
//     the polar angle from the up axis and θ is the azimuth in the north/right plane.
//   * Fly: forward = right*sinYaw*cosPitch + up*sinPitch + north*cosYaw*cosPitch.

#pragma once

#include <cmath>

#include "ibrt_constants.h"

namespace ibrt::cameramath {

enum class UpAxis
{
  Y,
  Z
};

enum class StandardView
{
  Front,
  Back,
  Left,
  Right,
  Top,
  Bottom,
  Iso
};

// ---- small vector helpers (work on any V with .x/.y/.z and V(x,y,z)) --------
//
// Deliberately v-prefixed so they never collide (via argument-dependent lookup)
// with the same-named helpers rkcommon::math defines for its vector types, which
// would make unqualified calls here ambiguous when V == rkcommon::math::vec3f.

template <class V>
inline float vdot(const V &a, const V &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <class V>
inline V vcross(const V &a, const V &b)
{
  return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

template <class V>
inline float vlen(const V &v)
{
  return std::sqrt(vdot(v, v));
}

template <class V>
inline V vnorm(const V &v)
{
  const float len = vlen(v);
  if (len <= 1e-20f)
    return V(0.0f, 0.0f, 0.0f);
  const float inv = 1.0f / len;
  return V(v.x * inv, v.y * inv, v.z * inv);
}

inline float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

// ---- reference frame --------------------------------------------------------

template <class V>
inline V worldUp(UpAxis axis)
{
  return axis == UpAxis::Z ? V(0.f, 0.f, 1.f) : V(0.f, 1.f, 0.f);
}

template <class V>
inline V worldForwardReference(UpAxis axis)
{
  return axis == UpAxis::Z ? V(0.f, 1.f, 0.f) : V(0.f, 0.f, 1.f);
}

template <class V>
inline V rightReference(UpAxis axis)
{
  return vnorm(vcross(worldForwardReference<V>(axis), worldUp<V>(axis)));
}

// ---- fly: yaw/pitch <-> forward --------------------------------------------

template <class V>
inline V forwardFromAngles(float yaw, float pitch, UpAxis axis)
{
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V right = rightReference<V>(axis);

  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);

  return vnorm(V(right.x * sy * cp + up.x * sp + north.x * cy * cp,
      right.y * sy * cp + up.y * sp + north.y * cy * cp,
      right.z * sy * cp + up.z * sp + north.z * cy * cp));
}

template <class V>
inline void anglesFromForward(const V &forward, UpAxis axis, float &yaw, float &pitch)
{
  const V dir = vnorm(forward);
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V right = rightReference<V>(axis);

  pitch = std::asin(clampf(vdot(dir, up), -1.f, 1.f));
  yaw = std::atan2(vdot(dir, right), vdot(dir, north));
}

// ---- orbit: theta/phi <-> eye direction ------------------------------------

// Unit direction from the orbit center to the eye.
template <class V>
inline V orbitEyeDirection(float theta, float phi, UpAxis axis)
{
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V right = rightReference<V>(axis);

  const float sinPhi = std::sin(phi);
  const float cosPhi = std::cos(phi);
  const float cosTheta = std::cos(theta);
  const float sinTheta = std::sin(theta);

  return vnorm(
      V(north.x * cosTheta * sinPhi + right.x * sinTheta * sinPhi + up.x * cosPhi,
          north.y * cosTheta * sinPhi + right.y * sinTheta * sinPhi + up.y * cosPhi,
          north.z * cosTheta * sinPhi + right.z * sinTheta * sinPhi + up.z * cosPhi));
}

// Inverse of orbitEyeDirection: recover theta/phi from a center->eye direction.
// phi is clamped away from the poles by kOrbitPhiEpsilon to keep the basis
// stable (matches RenderWidget::setOrbitFromEyePosition).
template <class V>
inline void orbitAnglesFromEyeDirection(
    const V &eyeDir, UpAxis axis, float &theta, float &phi)
{
  const V dir = vnorm(eyeDir);
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V right = rightReference<V>(axis);

  phi = std::acos(clampf(vdot(dir, up), -1.f, 1.f));
  phi = clampf(phi, kOrbitPhiEpsilon, kPi - kOrbitPhiEpsilon);

  const float sinPhi = std::sin(phi);
  if (std::fabs(sinPhi) > 1e-6f)
    theta = std::atan2(vdot(dir, right), vdot(dir, north));
}

// Unit direction from the model center to the eye for a given azimuth/elevation
// viewing angle (degrees). Elevation is measured above the horizon (the up/north
// plane) and azimuth is measured from the "north" reference toward "east"
// (= normalize(cross(north, up))), matching RenderWidget::currentViewAngles. So
// (az=0, el=0) looks along north, (az=90, el=0) along east, and (el=90) from
// straight above. Place the eye at center + eyeDirectionFromAzEl(...) * distance.
template <class V>
inline V eyeDirectionFromAzEl(float azimuthDeg, float elevationDeg, UpAxis axis)
{
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V east = rightReference<V>(axis); // normalize(cross(north, up))

  const float az = azimuthDeg * kPi / 180.f;
  const float el = elevationDeg * kPi / 180.f;
  const float ce = std::cos(el);
  const float se = std::sin(el);
  const float ca = std::cos(az);
  const float sa = std::sin(az);

  return vnorm(V(north.x * ce * ca + east.x * ce * sa + up.x * se,
      north.y * ce * ca + east.y * ce * sa + up.y * se,
      north.z * ce * ca + east.z * ce * sa + up.z * se));
}

// ---- framing ---------------------------------------------------------------

// Distance from the target at which the model's bounding extent fits the
// vertical field of view. Uses the half-FOV tangent (fitting the extent to the
// view plane) times an optional framing margin, matching the long-standing
// RenderWidget::fitDistanceFromBounds behavior (margin 1.3 there). A degenerate
// extent falls back to 1.0 and the half-angle is floored so a tiny FOV cannot
// blow up the distance.
inline float fitDistanceFromBounds(float maxExtent, float fovyDeg, float margin = 1.0f)
{
  const float extent = maxExtent > 0.001f ? maxExtent : 1.0f;
  float halfAngle = 0.5f * fovyDeg * kPi / 180.0f;
  if (halfAngle < 0.05f)
    halfAngle = 0.05f;
  return (0.5f * extent) / std::tan(halfAngle) * margin;
}

// ---- standard views --------------------------------------------------------

// Result of a standard-view request: a unit center->eye direction and the
// screen-up vector to use for that view (kept orthogonal to eyeDir).
template <class V>
struct ViewOrientation
{
  V eyeDir;
  V up;
};

// Canonical axis-aligned (and iso) camera orientations for the given up axis.
// eyeDir points from the model center toward the eye; the caller places the eye
// at center + eyeDir * distance. For Top/Bottom the screen-up is the "north"
// reference (so the model's forward reads up-screen); otherwise it is world up.
template <class V>
inline ViewOrientation<V> standardView(StandardView view, UpAxis axis)
{
  const V up = worldUp<V>(axis);
  const V north = worldForwardReference<V>(axis);
  const V right = rightReference<V>(axis);
  const auto neg = [](const V &v) { return V(-v.x, -v.y, -v.z); };

  switch (view) {
    case StandardView::Front:
      return {north, up};
    case StandardView::Back:
      return {neg(north), up};
    case StandardView::Right:
      return {right, up};
    case StandardView::Left:
      return {neg(right), up};
    case StandardView::Top:
      return {up, north};
    case StandardView::Bottom:
      return {neg(up), north};
    case StandardView::Iso:
    default: {
      const V dir = vnorm(V(north.x + right.x + up.x, north.y + right.y + up.y,
          north.z + right.z + up.z));
      // Re-orthogonalize an up that is not parallel to the iso direction.
      const V u = vnorm(V(up.x - dir.x * vdot(dir, up),
          up.y - dir.y * vdot(dir, up), up.z - dir.z * vdot(dir, up)));
      return {dir, u};
    }
  }
}

} // namespace ibrt::cameramath
