// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "common/DifferentialGeometry.ih"
#include "common/Ray.ih"
// c++ shared
#include "BRLCADShared.h"

OSPRAY_BEGIN_ISPC_NAMESPACE

static inline varying vec4f unpackRegionColor(varying uint32 packed)
{
  const uniform float scale = 1.0f / 255.0f;
  return make_vec4f((float)((packed >> 0) & 0xffu) * scale,
      (float)((packed >> 8) & 0xffu) * scale,
      (float)((packed >> 16) & 0xffu) * scale,
      (float)((packed >> 24) & 0xffu) * scale);
}

// postIntersect: called by Geometry_dispatch_postIntersect for GEOMETRY_TYPE_UNKNOWN.
// Fill dg.Ng and dg.Ns from the intersection normal stored in ray.Ng.
// Normalization and face-forwarding are handled by OSPRay's World::postIntersect.
SYCL_EXTERNAL void BRLCAD_postIntersect(const Geometry *uniform _self,
    varying DifferentialGeometry &dg,
    const varying Ray &ray,
    uniform int64 flags)
{
  const BRLCAD_sh *uniform self = (const BRLCAD_sh *uniform)_self;
  dg.Ng = dg.Ns = ray.Ng;
  if ((flags & DG_COLOR) && self->colorEnabled != 0) {
    dg.color = unpackRegionColor(ray.primID);
  }
}

export void *uniform BRLCAD_postIntersect_addr()
{
  return (void *uniform)BRLCAD_postIntersect;
}

// C bridge: implemented in brlcad.cpp with extern "C" linkage.
// The ISPC intersect function calls into C++ BRL-CAD ray tracing via this bridge.
extern "C" void brlcadIntersectN_C(void *uniform self,
    const struct RTCIntersectFunctionNArguments *uniform args,
    uniform bool isOcclusionTest);

// intersect: called by Geometry_dispatch_intersect/occluded for GEOMETRY_TYPE_UNKNOWN.
// geometryUserPtr is the BRLCAD_sh* (set by createEmbreeUserGeometry).
void unmasked BRLCAD_intersect(
    const struct RTCIntersectFunctionNArguments *uniform args,
    uniform bool isOcclusionTest)
{
  BRLCAD_sh *uniform sh = (BRLCAD_sh *uniform) args->geometryUserPtr;
  brlcadIntersectN_C(sh->brlcadSelf, args, isOcclusionTest);
}

export void *uniform BRLCAD_intersect_addr()
{
  return (void *uniform)BRLCAD_intersect;
}

OSPRAY_END_ISPC_NAMESPACE
