// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#ifdef __cplusplus
#include "common/Data.h"
#endif

#include "geometry/GeometryShared.h"

#ifdef __cplusplus
namespace ispc {
#endif // __cplusplus

struct BRLCAD_sh
{
  Geometry super; // MUST be first (mirrors ispc::Geometry layout)
  void *brlcadSelf; // pointer back to the C++ BRLCAD object
  uint32 colorEnabled;

#ifdef __cplusplus
  BRLCAD_sh()
      : brlcadSelf(nullptr), colorEnabled(1)
  {}
#endif
};

#ifdef __cplusplus
} // namespace ispc
#endif // __cplusplus
