// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Authoritative definitions of every IPC payload that travels over the render
// worker connection (review-plan R-03, B-25). Today these PODs are re-declared
// as function-local structs in worker_main.cpp and renderworkerclient.cpp (~13
// copies) and memcpy'd positionally, so any field that drifts on one side is
// silent memory corruption rather than a compile error. Consolidating them here
// with a static_assert on every sizeof means a layout change breaks the build
// instead of the wire.
//
// This header is intentionally standalone: it depends only on <cstdint> and the
// dependency-free ibrt_constants.h, so it can be included by the client, the
// worker, and tests without dragging in <windows.h> (which worker_ipc.h does on
// Win32). MessageHeader itself stays in worker_ipc.h; this header covers the
// variable payloads that follow it.
//
// Migration note: the existing call sites can adopt these types incrementally.
// Because the layouts match the current locals byte-for-byte (locked by the
// asserts below), swapping a local struct for the shared one is a no-op on the
// wire.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "ibrt_constants.h"

namespace ibrt::ipc::wire {

// Result of a LoadObj / LoadBrlcad / Resize / SetRenderSettings request. When
// errorSize > 0 the header is immediately followed by that many bytes of UTF-8
// error text.
struct LoadResultPayload
{
  std::uint32_t success;
  float boundsMin[3];
  float boundsMax[3];
  std::uint32_t errorSize;
};

// Resize request (client -> worker).
struct ResizePayload
{
  std::int32_t width;
  std::int32_t height;
};

// Camera update (client -> worker).
struct CameraPayload
{
  float eye[3];
  float center[3];
  float up[3];
  float fovyDeg;
};

// Header of a FrameData reply (worker -> client). Followed by rendererNameSize
// bytes of renderer name and then width*height*4 bytes of RGBA pixels.
struct FrameHeader
{
  std::uint32_t width;
  std::uint32_t height;
  float frameTimeMs;
  float renderFPS;
  std::uint32_t updated;
  std::uint32_t currentScale;
  std::uint64_t accumulatedFrames;
  std::uint64_t watchdogCancels;
  std::uint64_t aoAutoReductions;
  std::uint32_t rendererNameSize;
};

// Full render-settings snapshot (client -> worker). Field order and types must
// stay in lockstep with RenderWorkerClient::RenderSettingsState serialization.
struct SettingsPayload
{
  std::int32_t settingsMode;
  std::int32_t automaticPreset;
  float automaticTargetFrameTimeMs;
  std::uint32_t automaticAccumulationEnabled;
  std::int32_t customStartScale;
  float customTargetFrameTimeMs;
  std::int32_t customAoSamples;
  float customAoDistance;
  std::int32_t customPixelSamples;
  std::int32_t customMaxPathLength;
  std::int32_t customRoulettePathLength;
  std::uint32_t customAccumulationEnabled;
  std::int32_t customMaxAccumulationFrames;
  std::uint32_t customLowQualityWhileInteracting;
  std::uint32_t customFullResAccumulationOnly;
  std::int32_t customWatchdogTimeoutMs;
  float worldUp[3];
  std::uint32_t denoiseEnabled;
  std::uint32_t projectionMode; // 0 = perspective, 1 = orthographic
  std::uint32_t edgeRenderMode; // 0 = disabled, 1 = overlay, 2 = flat fill
  float edgeColor[3];
  float flatFillColor[3];
};

// ---------------------------------------------------------------------------
// Layout locks. These catch accidental field drift/reordering/padding changes
// at compile time. The numbers assume the LP32/LP64-compatible layout both
// binaries already rely on (matching toolchain + arch, per the architecture
// doc's "C++/ISPC shared structs" and B-25).
// ---------------------------------------------------------------------------

static_assert(std::is_standard_layout_v<LoadResultPayload>);
static_assert(std::is_standard_layout_v<ResizePayload>);
static_assert(std::is_standard_layout_v<CameraPayload>);
static_assert(std::is_standard_layout_v<FrameHeader>);
static_assert(std::is_standard_layout_v<SettingsPayload>);

static_assert(sizeof(LoadResultPayload) == 32, "LoadResultPayload layout drift");
static_assert(sizeof(ResizePayload) == 8, "ResizePayload layout drift");
static_assert(sizeof(CameraPayload) == 40, "CameraPayload layout drift");
static_assert(sizeof(FrameHeader) == 56, "FrameHeader layout drift");
static_assert(sizeof(SettingsPayload) == 112, "SettingsPayload layout drift");

// A couple of explicit offset checks on the struct whose 8-byte members force
// alignment padding, so a reordering that keeps the size constant is still
// caught.
static_assert(offsetof(FrameHeader, accumulatedFrames) == 24, "FrameHeader alignment drift");
static_assert(offsetof(FrameHeader, rendererNameSize) == 48, "FrameHeader tail drift");
static_assert(offsetof(LoadResultPayload, errorSize) == 28, "LoadResultPayload tail drift");

// Every payload must fit within the accepted IPC size bound (SEC-01); the bulk
// FrameData reply is header + name + pixels and is bounded separately by the
// max image dimensions folded into kMaxIpcPayloadSize.
static_assert(sizeof(SettingsPayload) <= kMaxIpcPayloadSize);
static_assert(sizeof(FrameHeader) <= kMaxIpcPayloadSize);

} // namespace ibrt::ipc::wire
