// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// Named constants that were previously scattered as magic literals across the
// IPC layer, the render backend, and the camera code (see review-plan R-12,
// SEC-01). Keeping them in one dependency-free header lets every translation
// unit share a single source of truth and makes the intent of each value
// self-documenting. This header pulls in nothing but <cstdint>/<cstddef> so it
// is safe to include from the worker, the client, the plugin, and tests alike.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ibrt {

// ---------------------------------------------------------------------------
// IPC wire protocol
// ---------------------------------------------------------------------------

// "IRBT" in little-endian ASCII (0x54='T', 0x42='B', 0x52='R', 0x49='I'); used
// as the leading bytes of every MessageHeader so a desynchronized or foreign
// stream is rejected instead of being memcpy'd into a struct. Previously this
// literal was repeated in worker_ipc.h and hand-checked in three places.
inline constexpr std::uint32_t kIpcMagic = 0x54425249u;

// Bump whenever a payload struct layout changes so mismatched builds fail fast
// rather than silently corrupting memory.
inline constexpr std::uint32_t kIpcVersion = 1u;

// Hard upper bound on an accepted IPC payload. readMessage() reads a 32-bit
// payloadSize from an untrusted local peer and allocates that many bytes; an
// unbounded value lets a corrupt/hostile peer request ~4 GiB and take the
// process down with an uncaught bad_alloc (SEC-01). This bound is generously
// sized for the largest legitimate frame: a 8192x8192 RGBA image plus a small
// header margin. Anything larger is treated as a framing error.
inline constexpr std::size_t kMaxIpcPayloadSize =
    std::size_t(8192) * std::size_t(8192) * 4u + 1024u;

// ---------------------------------------------------------------------------
// Rendering / watchdog
// ---------------------------------------------------------------------------

// Default per-frame watchdog budget. Was duplicated as a bare 1500 in both
// renderworkerclient.h and ospraybackend.h; derive both from here.
inline constexpr int kDefaultWatchdogMs = 1500;

// ---------------------------------------------------------------------------
// Math / camera
// ---------------------------------------------------------------------------

// A single high-precision pi. The codebase previously used the low-precision
// literal 3.14159265f in many spots (enough to introduce visible drift over
// repeated orbit accumulation).
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kHalfPi = 0.5f * kPi;

// Initial orbit angles used by Reset View. orbitTheta_ is the azimuth around
// the up axis; orbitPhi_ is the polar angle from the up axis. The historical
// seed 1.77079633 == pi/2 + 0.2 rad, i.e. very slightly below the horizon so
// the model is viewed from a natural, mildly-elevated three-quarter angle
// rather than dead-level. Named here so the "mystery" value is documented.
inline constexpr float kInitialOrbitTheta = 0.3f;
inline constexpr float kInitialOrbitPhi = kHalfPi + 0.2f; // ~= 1.77079633

// Polar-angle guard so the orbit camera never looks exactly along the up axis
// (which would make the right/up basis degenerate).
inline constexpr float kOrbitPhiEpsilon = 0.001f;

} // namespace ibrt
