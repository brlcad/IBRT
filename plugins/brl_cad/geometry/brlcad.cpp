// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "brlcad.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "brlcad/rt/mater.h"

// Declare ISPC-exported function addresses (generated from brlcad.ispc).
// ISPC exports use C linkage; extern "C" inside namespace ispc maps
// the C symbol to the ispc:: namespace for C++ call sites.
namespace ispc {
extern "C" void *BRLCAD_postIntersect_addr();
extern "C" void *BRLCAD_intersect_addr();
} // namespace ispc

namespace ospray {
namespace brlcad {

// ---------------------------------------------------------------------------
// Thread-local CPU index for per-thread BRL-CAD resources
// ---------------------------------------------------------------------------

static inline int getCpuId()
{
  static std::atomic<int> nextCpuId{0};
  thread_local int cpuId = nextCpuId.fetch_add(1);
  return cpuId;
}

static std::atomic<uint64_t> g_nextBrlcadInstanceId{1};

// ---------------------------------------------------------------------------
// CSV helper
// ---------------------------------------------------------------------------

static inline std::vector<std::string> splitCSV(const std::string &input)
{
  std::vector<std::string> out;
  std::stringstream ss(input);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty())
      out.push_back(item);
  }
  return out;
}

static inline int getNumThreads()
{
  const unsigned int hc = std::thread::hardware_concurrency();
  return hc > 0 ? static_cast<int>(hc) : 1;
}

static void cleanupTrackedResources(BRLCAD &geom)
{
  if (!geom.rtip)
    return;

  for (auto &res : geom.resources)
    rt_clean_resource_complete(geom.rtip, &res);

  for (const auto &block : geom.overflowResourceBlocks) {
    if (!block || !block->data)
      continue;
    for (size_t i = 0; i < block->count; ++i)
      rt_clean_resource_complete(geom.rtip, &block->data[i]);
  }
}

static resource *ensureOverflowResource(const BRLCAD &geom, size_t cpuId)
{
  const size_t primaryCount = geom.resources.size();
  const size_t overflowIndex = cpuId - primaryCount;

  for (;;) {
    BRLCAD::OverflowResourceBlock *overflow =
        geom.activeOverflowResources.load(std::memory_order_acquire);
    if (overflow && overflowIndex < overflow->count)
      return &overflow->data[overflowIndex];

    bool expected = false;
    if (!geom.overflowExpansionInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
      std::this_thread::yield();
      continue;
    }

    struct ExpansionGuard
    {
      std::atomic<bool> &flag;
      ~ExpansionGuard()
      {
        flag.store(false, std::memory_order_release);
      }
    } guard{geom.overflowExpansionInProgress};

    overflow = geom.activeOverflowResources.load(std::memory_order_acquire);
    if (overflow && overflowIndex < overflow->count)
      return &overflow->data[overflowIndex];

    const size_t currentCount = overflow ? overflow->count : 0;
    const size_t growth = std::max(primaryCount, size_t(8));
    auto block = std::make_unique<BRLCAD::OverflowResourceBlock>();
    block->count = std::max(overflowIndex + 1, currentCount + growth);
    block->data = std::make_unique<resource[]>(block->count);
    for (size_t i = 0; i < block->count; ++i)
      rt_init_resource(&block->data[i], int(primaryCount + i), geom.rtip);

    BRLCAD::OverflowResourceBlock *blockPtr = block.get();
    geom.overflowResourceBlocks.push_back(std::move(block));
    geom.activeOverflowResources.store(blockPtr, std::memory_order_release);
    return &blockPtr->data[overflowIndex];
  }
}

static resource *selectResourceForThread(const BRLCAD &geom, uint64_t currentInstanceId)
{
  thread_local resource *tl_res = nullptr;
  thread_local uint64_t tl_instanceId = 0;

  if (tl_instanceId == currentInstanceId && tl_res)
    return tl_res;

  const size_t cpuId = size_t(getCpuId());
  if (cpuId < geom.resources.size()) {
    tl_res = &geom.resources[cpuId];
  } else {
    tl_res = ensureOverflowResource(geom, cpuId);
  }

  tl_instanceId = currentInstanceId;
  return tl_res;
}

static constexpr bool kVerboseBRLCADLogging = false;

static unsigned int currentContextInstID(const RTCIntersectFunctionNArguments *args)
{
  if (!args || !args->context)
    return RTC_INVALID_GEOMETRY_ID;
#if RTC_MAX_INSTANCE_LEVEL_COUNT > 1
  if (args->context->instStackSize > 0)
    return args->context->instID[args->context->instStackSize - 1];
#endif
  return args->context->instID[0];
}

static inline uint32_t packRegionColor(float r, float g, float b, float a = 1.0f)
{
  const auto toByte = [](float v) -> uint32_t {
    const float clamped = std::max(0.0f, std::min(1.0f, v));
    return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
  };
  return toByte(r) | (toByte(g) << 8) | (toByte(b) << 16) | (toByte(a) << 24);
}

static inline uint32_t fallbackRegionColor()
{
  return packRegionColor(0.8f, 0.8f, 0.8f, 1.0f);
}

// ---------------------------------------------------------------------------
// Embree ray-packet helpers
// ---------------------------------------------------------------------------

inline static void getRay(
    void *rayhitN, unsigned int N, unsigned int i, RTCRayHit &rayhit)
{
  auto &ray = rayhit.ray;
  auto &hit = rayhit.hit;

  RTCRayHitN *rays = (RTCRayHitN *)rayhitN;
  RTCRayN *rayN = RTCRayHitN_RayN(rays, N);
  RTCHitN *hitN = RTCRayHitN_HitN(rays, N);

  ray.org_x = RTCRayN_org_x(rayN, N, i);
  ray.org_y = RTCRayN_org_y(rayN, N, i);
  ray.org_z = RTCRayN_org_z(rayN, N, i);

  ray.dir_x = RTCRayN_dir_x(rayN, N, i);
  ray.dir_y = RTCRayN_dir_y(rayN, N, i);
  ray.dir_z = RTCRayN_dir_z(rayN, N, i);

  ray.tnear = RTCRayN_tnear(rayN, N, i);
  ray.tfar = RTCRayN_tfar(rayN, N, i);

  ray.time = RTCRayN_time(rayN, N, i);
  ray.mask = RTCRayN_mask(rayN, N, i);
  ray.flags = 0;

  hit.primID = RTCHitN_primID(hitN, N, i);
  hit.geomID = RTCHitN_geomID(hitN, N, i);
  hit.instID[0] = RTCHitN_instID(hitN, N, i, 0);
}

inline static void setRay(
    const RTCRayHit &rayhit, void *rayhitN, unsigned int N, unsigned int i)
{
  const auto &ray = rayhit.ray;
  const auto &hit = rayhit.hit;

  RTCRayHitN *rays = (RTCRayHitN *)rayhitN;
  RTCRayN *rayN = RTCRayHitN_RayN(rays, N);
  RTCHitN *hitN = RTCRayHitN_HitN(rays, N);

  RTCRayN_tfar(rayN, N, i) = ray.tfar;

  if (hit.geomID != RTC_INVALID_GEOMETRY_ID) {
    RTCHitN_Ng_x(hitN, N, i) = hit.Ng_x;
    RTCHitN_Ng_y(hitN, N, i) = hit.Ng_y;
    RTCHitN_Ng_z(hitN, N, i) = hit.Ng_z;

    RTCHitN_primID(hitN, N, i) = hit.primID;
    RTCHitN_geomID(hitN, N, i) = hit.geomID;
    RTCHitN_instID(hitN, N, i, 0) = hit.instID[0];
    RTCHitN_u(hitN, N, i) = hit.u;
    RTCHitN_v(hitN, N, i) = hit.v;
  } else {
    RTCHitN_geomID(hitN, N, i) = RTC_INVALID_GEOMETRY_ID;
    RTCHitN_primID(hitN, N, i) = RTC_INVALID_GEOMETRY_ID;
    RTCHitN_instID(hitN, N, i, 0) = hit.instID[0];
  }
}

// ---------------------------------------------------------------------------
// BRL-CAD hit / miss callbacks
// ---------------------------------------------------------------------------

static int hitCallback(application *ap, partition *PartHeadp, seg * /*segs*/)
{
  auto &rayhit = *static_cast<RTCRayHit *>(ap->a_uptr);
  auto &ray = rayhit.ray;
  auto &hit = rayhit.hit;

  for (auto *pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {
    auto *hitp = pp->pt_inhit;
    auto *stp = pp->pt_inseg->seg_stp;

    vect_t inormal;
    RT_HIT_NORMAL(inormal, hitp, stp, &(ap->a_ray), pp->pt_inflip);

    ray.tfar = hitp->hit_dist;
    hit.Ng_x = inormal[0];
    hit.Ng_y = inormal[1];
    hit.Ng_z = inormal[2];
    hit.geomID = static_cast<unsigned int>(ap->a_user);
    const region *region = pp->pt_regionp;
    hit.primID = fallbackRegionColor();
    if (region && region->reg_mater.ma_color_valid) {
      hit.primID = packRegionColor(region->reg_mater.ma_color[0],
          region->reg_mater.ma_color[1],
          region->reg_mater.ma_color[2],
          1.0f);
    }
    return 1;
  }

  return 1;
}

static int missCallback(application * /*ap*/)
{
  return 0;
}

// ---------------------------------------------------------------------------
// Single-ray BRL-CAD trace (called per lane from brlcadIntersectN_C)
// ---------------------------------------------------------------------------

static void traceRay(const BRLCAD &geom, RTCRayHit &rayhit, unsigned int geomID)
{
  auto &ray = rayhit.ray;
  auto &hit = rayhit.hit;
  application ap;

  RT_APPLICATION_INIT(&ap);

  ap.a_rt_i = geom.rtip;
  ap.a_onehit = 1;
  ap.a_resource = selectResourceForThread(
      geom, geom.instanceId.load(std::memory_order_acquire));
  ap.a_user = static_cast<int>(geomID);

  VSET(ap.a_ray.r_pt, ray.org_x, ray.org_y, ray.org_z);
  VSET(ap.a_ray.r_dir, ray.dir_x, ray.dir_y, ray.dir_z);
  ap.a_ray.r_min = ray.tnear;
  ap.a_ray.r_max = ray.tfar;

  ap.a_hit = hitCallback;
  ap.a_miss = missCallback;
  ap.a_logoverlap = rt_silent_logoverlap;
  ap.a_uptr = &rayhit;

  // Reset hit info so misses cannot leak stale data.
  hit.geomID = RTC_INVALID_GEOMETRY_ID;
  hit.primID = RTC_INVALID_GEOMETRY_ID;

  auto didHit = rt_shootray(&ap);
  if (didHit <= 0) {
    hit.geomID = RTC_INVALID_GEOMETRY_ID;
    hit.primID = RTC_INVALID_GEOMETRY_ID;
    return;
  }
}

// ---------------------------------------------------------------------------
// Embree bounds callback
// geometryUserPtr = getSh() = ispc::BRLCAD_sh*  (set by
// createEmbreeUserGeometry)
// ---------------------------------------------------------------------------

static void brlcadBounds(const struct RTCBoundsFunctionArguments *args)
{
  const ispc::BRLCAD_sh *sh =
      static_cast<const ispc::BRLCAD_sh *>(args->geometryUserPtr);
  const BRLCAD *geom = static_cast<const BRLCAD *>(sh->brlcadSelf);

  RTCBounds *bounds_o = args->bounds_o;
  bounds_o->lower_x = geom->bounds.lower.x;
  bounds_o->lower_y = geom->bounds.lower.y;
  bounds_o->lower_z = geom->bounds.lower.z;
  bounds_o->upper_x = geom->bounds.upper.x;
  bounds_o->upper_y = geom->bounds.upper.y;
  bounds_o->upper_z = geom->bounds.upper.z;
}

// ---------------------------------------------------------------------------
// C bridge called from ISPC BRLCAD_intersect.
// args->geometryUserPtr = getSh() = BRLCAD_sh* (not used here; geom via self)
// ---------------------------------------------------------------------------

extern "C" void brlcadIntersectN_C(void *self,
    const RTCIntersectFunctionNArguments *args,
    bool isOcclusionTest)
{
  const BRLCAD *geom = static_cast<const BRLCAD *>(self);

  const unsigned int N = args->N;
  const unsigned int geomID = args->geomID;

  if (isOcclusionTest) {
    // args was actually RTCOccludedFunctionNArguments* cast to intersect args.
    // The 'rayhit' field maps to 'ray' in the occluded struct.
    RTCRayN *rayN = (RTCRayN *)args->rayhit;
    for (unsigned int i = 0; i < N; ++i) {
      if (args->valid && !args->valid[i])
        continue;

      RTCRayHit rayhit{};
      rayhit.ray.org_x = RTCRayN_org_x(rayN, N, i);
      rayhit.ray.org_y = RTCRayN_org_y(rayN, N, i);
      rayhit.ray.org_z = RTCRayN_org_z(rayN, N, i);
      rayhit.ray.dir_x = RTCRayN_dir_x(rayN, N, i);
      rayhit.ray.dir_y = RTCRayN_dir_y(rayN, N, i);
      rayhit.ray.dir_z = RTCRayN_dir_z(rayN, N, i);
      rayhit.ray.tnear = RTCRayN_tnear(rayN, N, i);
      rayhit.ray.tfar = RTCRayN_tfar(rayN, N, i);
      rayhit.ray.time = RTCRayN_time(rayN, N, i);
      rayhit.ray.mask = RTCRayN_mask(rayN, N, i);
      rayhit.ray.id = 0;
      rayhit.ray.flags = 0;
      rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
      rayhit.hit.primID = RTC_INVALID_GEOMETRY_ID;
      rayhit.hit.instID[0] = currentContextInstID(args);

      traceRay(*geom, rayhit, geomID);

      // Embree occlusion convention: tfar = -inf means occluded
      if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
        RTCRayN_tfar(rayN, N, i) = -std::numeric_limits<float>::infinity();
    }
  } else {
    for (unsigned int i = 0; i < N; ++i) {
      if (!args->valid || args->valid[i]) {
        RTCRayHit rayhit;
        getRay(args->rayhit, N, i, rayhit);
        rayhit.hit.instID[0] = currentContextInstID(args);
        traceRay(*geom, rayhit, geomID);
        setRay(rayhit, args->rayhit, N, i);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// BRLCAD class implementation
// ---------------------------------------------------------------------------

BRLCAD::BRLCAD(api::ISPCDevice &device)
    : AddStructShared(device.getIspcrtContext(), device, FFG_BOX)
{
  instanceId.store(g_nextBrlcadInstanceId.fetch_add(1), std::memory_order_relaxed);
#ifndef OSPRAY_TARGET_SYCL
  getSh()->super.type = ispc::GEOMETRY_TYPE_UNKNOWN;
  getSh()->super.postIntersect =
      reinterpret_cast<ispc::Geometry_postIntersectFct>(
          ispc::BRLCAD_postIntersect_addr());
  getSh()->super.intersect = reinterpret_cast<ispc::Geometry_IntersectFct>(
      ispc::BRLCAD_intersect_addr());
#endif
}

BRLCAD::~BRLCAD()
{
  if (rtip) {
    cleanupTrackedResources(*this);
    rt_free_rti(rtip);
    rtip = nullptr;
  }
}

size_t BRLCAD::numPrimitives() const
{
  return rtip ? 1 : 0;
}

void BRLCAD::commit()
{
  instanceId.store(
      g_nextBrlcadInstanceId.fetch_add(1), std::memory_order_release);
  getSh()->super.type = ispc::GEOMETRY_TYPE_UNKNOWN;
#ifndef OSPRAY_TARGET_SYCL
  getSh()->super.postIntersect =
      reinterpret_cast<ispc::Geometry_postIntersectFct>(
          ispc::BRLCAD_postIntersect_addr());
  getSh()->super.intersect = reinterpret_cast<ispc::Geometry_IntersectFct>(
      ispc::BRLCAD_intersect_addr());
#endif
  if (rtip) {
    cleanupTrackedResources(*this);
    rt_free_rti(rtip);
    rtip = nullptr;
  }
  resources.clear();
  overflowResourceBlocks.clear();
  activeOverflowResources.store(nullptr, std::memory_order_release);
  overflowExpansionInProgress.store(false, std::memory_order_release);
  const std::string filename = getParam<std::string>("filename", "");
  if (filename.empty())
    throw std::runtime_error("BRL-CAD geometry requires 'filename' parameter");

  std::string objectList = getParam<std::string>("objects", "all");
  const bool colorEnabled = getParam<bool>("colorEnabled", true);
  const int nThreads = getNumThreads();

  rtip = rt_dirbuild(filename.c_str(), nullptr, 0);
  if (rtip == nullptr)
    throw std::runtime_error("Failed to open BRL-CAD database: " + filename);

  objects.clear();
  auto objNames = splitCSV(objectList);
  for (const auto &obj : objNames) {
    if (obj.empty())
      continue;
    objects.push_back(obj);
    const char *object = obj.c_str();
    if (rt_gettrees(rtip, 1, &object, nThreads) < 0)
      throw std::runtime_error("Failed to load BRL-CAD object: " + obj);
  }

  if (objects.empty()) {
    static const char *allObj = "all";
    if (rt_gettrees(rtip, 1, &allObj, nThreads) < 0)
      throw std::runtime_error("Failed to load default BRL-CAD object: all");
    objects.emplace_back(allObj);
  }

  resources.reserve(size_t(std::max(nThreads, 1)));
  for (int cpu = 0; cpu < std::max(nThreads, 1); ++cpu) {
    resources.emplace_back();
    rt_init_resource(&resources.back(), cpu, rtip);
  }
  rt_prep_parallel(rtip, nThreads);

  rt_iterate_regions(rtip,
      [](region *reg, void *) -> int {
        if (reg)
          rt_region_color_map(reg);
        return 0;
      },
      nullptr);

  bounds.lower.x = rtip->mdl_min[0];
  bounds.lower.y = rtip->mdl_min[1];
  bounds.lower.z = rtip->mdl_min[2];
  bounds.upper.x = rtip->mdl_max[0];
  bounds.upper.y = rtip->mdl_max[1];
  bounds.upper.z = rtip->mdl_max[2];

  if (kVerboseBRLCADLogging) {
    fprintf(stderr,
        "BRLCAD bounds min=(%f,%f,%f) max=(%f,%f,%f)\n",
        bounds.lower.x,
        bounds.lower.y,
        bounds.lower.z,
        bounds.upper.x,
        bounds.upper.y,
        bounds.upper.z);
  }

  getSh()->brlcadSelf = this;
  getSh()->colorEnabled = colorEnabled ? 1u : 0u;
  createEmbreeUserGeometry((RTCBoundsFunction)brlcadBounds);

  getSh()->super.numPrimitives = static_cast<int>(numPrimitives());
}
} // namespace brlcad
} // namespace ospray
