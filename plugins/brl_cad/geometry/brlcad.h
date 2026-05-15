// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Geometry.h"
#include "rkcommon/math/box.h"
#include "rkcommon/math/vec.h"

// Shared C++/ISPC struct for BRLCAD geometry
#include "BRLCADShared.h"

#undef UNUSED
#undef _USE_MATH_DEFINES
#include "brlcad/common.h"
#include "brlcad/raytrace.h" /* librt interface definitions */
#include "brlcad/vmath.h" /* vector math macros */

#include <embree4/rtcore.h>
#include <embree4/rtcore_ray.h>

namespace ospray {
namespace brlcad {

// BRLCAD extends AddStructShared<Geometry, ispc::BRLCAD_sh> so that:
//  - getSh() returns ispc::BRLCAD_sh* (which starts with ispc::Geometry super)
//  - Geometry_dispatch_postIntersect / _intersect can use getSh()->super.*
//  - BRLCAD_intersect (ISPC) reads getSh()->brlcadSelf to reach this object
struct BRLCAD : public AddStructShared<Geometry, ispc::BRLCAD_sh>
{
  struct OverflowResourceBlock
  {
    size_t count = 0;
    std::unique_ptr<resource[]> data;
  };

  BRLCAD(api::ISPCDevice &device);
  ~BRLCAD() override;

  void commit() override;
  size_t numPrimitives() const override;

  // Scene bounds (populated in commit from BRL-CAD rtip)
  rkcommon::math::box3f bounds;

  // BRL-CAD ray-trace instance
  application ap;
  rt_i *rtip{nullptr};

  std::atomic<uint64_t> instanceId{0};
  mutable std::vector<resource> resources;
  mutable std::vector<std::unique_ptr<OverflowResourceBlock>> overflowResourceBlocks;
  mutable std::atomic<OverflowResourceBlock *> activeOverflowResources{nullptr};
  mutable std::atomic<bool> overflowExpansionInProgress{false};
  std::vector<std::string> objects;
};

} // namespace brlcad
} // namespace ospray
