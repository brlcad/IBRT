// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

// BRL-CAD portability headers must precede C++/rkcommon headers.
extern "C" {
#include <brlcad/common.h>
#include <brlcad/raytrace.h>
}

#undef UNUSED

#include "hiddenlinerenderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace ibrt::render {
namespace {

struct Vec3d
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Vec3d operator+(const Vec3d &a, const Vec3d &b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3d operator-(const Vec3d &a, const Vec3d &b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3d operator*(const Vec3d &v, double scale)
{
  return {v.x * scale, v.y * scale, v.z * scale};
}

double dot(const Vec3d &a, const Vec3d &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3d cross(const Vec3d &a, const Vec3d &b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3d normalized(const Vec3d &v)
{
  const double length = std::sqrt(dot(v, v));
  if (length <= 1e-15)
    return {};
  return v * (1.0 / length);
}

Vec3d toVec(const HiddenLineVec3 &v)
{
  return {v.x, v.y, v.z};
}

struct RayResult
{
  HiddenLineSample *sample = nullptr;
};

int hiddenLineHit(application *ap, partition *partitions, seg *)
{
  auto *result = static_cast<RayResult *>(ap->a_uptr);
  if (!result || !result->sample || !partitions)
    return 0;

  partition *partition = partitions->pt_forw;
  if (partition == partitions)
    return 0;

  hit *hitPoint = partition->pt_inhit;
  soltab *solid = partition->pt_inseg->seg_stp;
  vect_t normal;
  RT_HIT_NORMAL(normal, hitPoint, solid, &ap->a_ray, partition->pt_inflip);

  HiddenLineSample &sample = *result->sample;
  sample.hit = true;
  sample.distance = static_cast<float>(hitPoint->hit_dist);
  sample.normalX = static_cast<float>(normal[0]);
  sample.normalY = static_cast<float>(normal[1]);
  sample.normalZ = static_cast<float>(normal[2]);
  if (partition->pt_regionp) {
    sample.regionId = partition->pt_regionp->reg_regionid;
    sample.regionKey = reinterpret_cast<std::uintptr_t>(partition->pt_regionp);
  }
  return 1;
}

int hiddenLineMiss(application *)
{
  return 0;
}

} // namespace

struct HiddenLineRenderer::Impl
{
  rt_i *rtip = nullptr;
  std::vector<resource> resources;

  ~Impl()
  {
    clear();
  }

  void clear()
  {
    if (!rtip)
      return;
    // rt_i_destroy cleans the per-thread allocations registered in
    // rti_resources, while ownership of the resource structs stays here.
    // Keep their backing storage alive until that cleanup is complete.
    rt_i_destroy(rtip);
    rtip = nullptr;
    resources.clear();
  }
};

HiddenLineRenderer::HiddenLineRenderer() : impl_(std::make_unique<Impl>()) {}

HiddenLineRenderer::~HiddenLineRenderer() = default;

bool HiddenLineRenderer::loadScene(const std::string &databasePath,
    const std::string &topObject,
    std::string &error)
{
  error.clear();
  impl_->clear();

  impl_->rtip = rt_dirbuild(databasePath.c_str(), nullptr, 0);
  if (!impl_->rtip) {
    error = "Hidden-line renderer could not open the BRL-CAD database.";
    return false;
  }

  std::vector<std::string> objects;
  if (topObject.empty()) {
    directory **topDirectories = nullptr;
    const std::size_t topCount =
        db_ls(impl_->rtip->rti_dbip, DB_LS_TOPS, nullptr, &topDirectories);
    objects.reserve(topCount);
    for (std::size_t i = 0; i < topCount; ++i) {
      if (topDirectories[i] && topDirectories[i]->d_namep
          && *topDirectories[i]->d_namep) {
        objects.emplace_back(topDirectories[i]->d_namep);
      }
    }
    if (topDirectories)
      bu_free(topDirectories, "hidden-line top-level object list");
  } else {
    objects.push_back(topObject);
  }

  if (objects.empty()) {
    error = "Hidden-line renderer found no top-level BRL-CAD objects.";
    impl_->clear();
    return false;
  }

  std::vector<const char *> objectNames;
  objectNames.reserve(objects.size());
  for (const std::string &object : objects)
    objectNames.push_back(object.c_str());

  const unsigned int hardwareThreads = std::thread::hardware_concurrency();
  const int threadCount = std::max(1, static_cast<int>(hardwareThreads));
  if (rt_gettrees(impl_->rtip,
          static_cast<int>(objectNames.size()),
          objectNames.data(),
          threadCount)
      < 0) {
    error = topObject.empty()
        ? "Hidden-line renderer could not load the top-level BRL-CAD objects."
        : "Hidden-line renderer could not load BRL-CAD object '" + topObject
            + "'.";
    impl_->clear();
    return false;
  }

  impl_->resources.resize(static_cast<std::size_t>(threadCount));
  for (int i = 0; i < threadCount; ++i)
    rt_init_resource(
        &impl_->resources[static_cast<std::size_t>(i)], i, impl_->rtip);
  rt_prep_parallel(impl_->rtip, threadCount);
  return true;
}

void HiddenLineRenderer::clearScene()
{
  impl_->clear();
}

bool HiddenLineRenderer::hasScene() const
{
  return impl_->rtip != nullptr;
}

bool HiddenLineRenderer::renderMask(const HiddenLineCamera &camera,
    int width,
    int height,
    std::vector<std::uint8_t> &mask,
    HiddenLineSettings &settings,
    std::string &error) const
{
  error.clear();
  mask.clear();
  if (!impl_->rtip) {
    error =
        "Hidden-line rendering is available for loaded BRL-CAD scenes only.";
    return false;
  }
  if (width <= 0 || height <= 0) {
    error = "Hidden-line render dimensions are invalid.";
    return false;
  }

  const Vec3d eye = toVec(camera.eye);
  const Vec3d center = toVec(camera.center);
  const Vec3d forward = normalized(center - eye);
  Vec3d right = normalized(cross(forward, toVec(camera.up)));
  if (dot(right, right) <= 1e-15)
    right = {1.0, 0.0, 0.0};
  const Vec3d screenUp = normalized(cross(right, forward));
  const double focusDistance =
      std::max(std::sqrt(dot(center - eye, center - eye)), 1e-9);
  const double halfFov =
      std::clamp<double>(camera.verticalFovDegrees, 0.01, 179.0)
      * 3.14159265358979323846 / 360.0;
  const double halfHeight = focusDistance * std::tan(halfFov);
  const double halfWidth =
      halfHeight * std::max<double>(camera.aspectRatio, 1e-9);

  // rtedge's default is (cell_width * atan(87deg)) + 2 model units.  Its
  // ARCTAN_87 constant is 19.08; use the same formula at the camera pivot.
  settings.maxDistance =
      static_cast<float>((2.0 * halfWidth / width) * 19.08 + 2.0);

  const std::size_t sampleCount = std::size_t(width) * std::size_t(height);
  std::vector<HiddenLineSample> samples(sampleCount);
  const int workerCount = std::max(1,
      std::min({height,
          static_cast<int>(impl_->resources.size()),
          static_cast<int>(std::thread::hardware_concurrency() == 0
                  ? 1
                  : std::thread::hardware_concurrency())}));
  std::atomic<int> nextRow{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(workerCount));

  for (int worker = 0; worker < workerCount; ++worker) {
    workers.emplace_back([&, worker]() {
      resource *threadResource =
          &impl_->resources[static_cast<std::size_t>(worker)];
      for (;;) {
        const int y = nextRow.fetch_add(1, std::memory_order_relaxed);
        if (y >= height)
          break;
        for (int x = 0; x < width; ++x) {
          const double xNdc = 2.0 * (double(x) + 0.5) / double(width) - 1.0;
          const double yNdc = 2.0 * (double(y) + 0.5) / double(height) - 1.0;
          const Vec3d offset =
              right * (xNdc * halfWidth) + screenUp * (yNdc * halfHeight);
          const Vec3d origin = camera.orthographic ? eye + offset : eye;
          const Vec3d direction = camera.orthographic
              ? forward
              : normalized(forward + offset * (1.0 / focusDistance));

          HiddenLineSample &sample =
              samples[std::size_t(y) * std::size_t(width) + std::size_t(x)];
          RayResult result{&sample};
          application ap;
          RT_APPLICATION_INIT(&ap);
          ap.a_rt_i = impl_->rtip;
          ap.a_onehit = 1;
          ap.a_resource = threadResource;
          ap.a_hit = hiddenLineHit;
          ap.a_miss = hiddenLineMiss;
          ap.a_logoverlap = rt_silent_logoverlap;
          ap.a_uptr = &result;
          VSET(ap.a_ray.r_pt, origin.x, origin.y, origin.z);
          VSET(ap.a_ray.r_dir, direction.x, direction.y, direction.z);
          ap.a_ray.r_min = 0.0;
          ap.a_ray.r_max = std::numeric_limits<double>::max();
          rt_shootray(&ap);
        }
      }
    });
  }
  for (std::thread &worker : workers)
    worker.join();

  mask = detectHiddenLineEdges(samples.data(), width, height, settings);
  return mask.size() == sampleCount;
}

} // namespace ibrt::render
