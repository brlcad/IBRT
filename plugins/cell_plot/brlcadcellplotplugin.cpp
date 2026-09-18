// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "brlcadcellplotplugin.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <limits>

using ibrt::cellplot::Ray;
using ibrt::cellplot::Result;
using ibrt::cellplot::Rgba;

BrlcadCellPlotPlugin::~BrlcadCellPlotPlugin()
{
  clearScene();
}

QString BrlcadCellPlotPlugin::name() const
{
  return QStringLiteral("BRL-CAD cell plot");
}

void BrlcadCellPlotPlugin::clearScene()
{
  if (rtip_ && resourceInitialized_)
    rt_clean_resource_complete(rtip_, &resource_);
  resourceInitialized_ = false;

  if (rtip_)
    rt_free_rti(rtip_);
  rtip_ = nullptr;

  loadedDatabasePath_.clear();
  loadedTopObject_.clear();
  regionColors_.clear();
  sceneInfo_ = {};
}

int BrlcadCellPlotPlugin::collectRegionColor(region *reg, void *context)
{
  if (!reg || !context)
    return 0;

  auto &colors =
      *static_cast<std::unordered_map<std::int64_t, Rgba> *>(context);
  const std::int64_t regionId = static_cast<std::int64_t>(reg->reg_regionid);
  colors.emplace(regionId, ibrt::cellplot::colorForRegionId(regionId));
  return 0;
}

bool BrlcadCellPlotPlugin::loadScene(
    const QString &databasePath, const QString &topObject, QString &error)
{
  error.clear();
  const QString normalizedPath = databasePath.trimmed();
  const QString normalizedObject = topObject.trimmed().isEmpty()
      ? QStringLiteral("all")
      : topObject.trimmed();

  if (rtip_ && normalizedPath == loadedDatabasePath_
      && normalizedObject == loadedTopObject_) {
    return true;
  }

  clearScene();
  const QByteArray pathBytes = normalizedPath.toLocal8Bit();
  rtip_ = rt_dirbuild(pathBytes.constData(), nullptr, 0);
  if (!rtip_) {
    error = QStringLiteral("Unable to open BRL-CAD database: %1")
                .arg(normalizedPath);
    return false;
  }

  const QByteArray objectBytes = normalizedObject.toLocal8Bit();
  const char *objectName = objectBytes.constData();
  if (rt_gettrees(rtip_, 1, &objectName, 1) < 0) {
    error = QStringLiteral("Unable to load BRL-CAD object '%1' from %2")
                .arg(normalizedObject, normalizedPath);
    clearScene();
    return false;
  }

  rt_init_resource(&resource_, 0, rtip_);
  resourceInitialized_ = true;
  rt_prep_parallel(rtip_, 1);

  sceneInfo_.boundsMin = {
      rtip_->mdl_min[0], rtip_->mdl_min[1], rtip_->mdl_min[2]};
  sceneInfo_.boundsMax = {
      rtip_->mdl_max[0], rtip_->mdl_max[1], rtip_->mdl_max[2]};
  sceneInfo_.valid = true;

  regionColors_.clear();
  rt_iterate_regions(
      rtip_, &BrlcadCellPlotPlugin::collectRegionColor, &regionColors_);
  loadedDatabasePath_ = normalizedPath;
  loadedTopObject_ = normalizedObject;
  return true;
}

ibrt::cellplot::SceneInfo BrlcadCellPlotPlugin::sceneInfo() const
{
  return sceneInfo_;
}

int BrlcadCellPlotPlugin::firstHit(
    application *ap, partition *partitions, seg * /*segments*/)
{
  if (!ap || !partitions || !ap->a_uptr)
    return 0;

  auto &result = *static_cast<ProbeResult *>(ap->a_uptr);
  for (partition *part = partitions->pt_forw; part != partitions;
      part = part->pt_forw) {
    if (!part->pt_regionp)
      continue;
    result.regionId = static_cast<std::int64_t>(part->pt_regionp->reg_regionid);
    result.hit = true;
    break;
  }
  return result.hit ? 1 : 0;
}

int BrlcadCellPlotPlugin::miss(application * /*ap*/)
{
  return 0;
}

BrlcadCellPlotPlugin::ProbeResult BrlcadCellPlotPlugin::probe(const Ray &ray)
{
  ProbeResult result;
  if (!rtip_ || !resourceInitialized_)
    return result;

  application ap;
  RT_APPLICATION_INIT(&ap);
  ap.a_rt_i = rtip_;
  ap.a_onehit = 1;
  ap.a_resource = &resource_;
  ap.a_hit = &BrlcadCellPlotPlugin::firstHit;
  ap.a_miss = &BrlcadCellPlotPlugin::miss;
  ap.a_logoverlap = rt_silent_logoverlap;
  ap.a_uptr = &result;

  VSET(ap.a_ray.r_pt, ray.origin.x, ray.origin.y, ray.origin.z);
  VSET(ap.a_ray.r_dir, ray.direction.x, ray.direction.y, ray.direction.z);
  ap.a_ray.r_min = 0.0;
  ap.a_ray.r_max = std::numeric_limits<double>::infinity();
  rt_shootray(&ap);
  return result;
}

Result BrlcadCellPlotPlugin::evaluate(const ibrt::cellplot::Request &request,
    const std::atomic_bool *cancelRequested)
{
  Result result;
  if (!rtip_) {
    result.error =
        QStringLiteral("No BRL-CAD scene is loaded in the cell-plot plugin.");
    return result;
  }

  const int columns = std::clamp(request.columns, 1, 1024);
  const int rows = std::clamp(request.rows, 1, 1024);
  result.overlay = QImage(columns, rows, QImage::Format_RGBA8888);
  result.overlay.fill(Qt::transparent);

  for (int row = 0; row < rows; ++row) {
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
      result.cancelled = true;
      result.overlay = QImage();
      return result;
    }

    for (int column = 0; column < columns; ++column) {
      const Ray ray = ibrt::cellplot::rayForCell(
          request.camera, column, row, columns, rows);
      const ProbeResult probeResult = probe(ray);
      ++result.raysTraced;
      if (!probeResult.hit)
        continue;

      ++result.cellsHit;
      const auto found = regionColors_.find(probeResult.regionId);
      const Rgba color = found == regionColors_.end()
          ? ibrt::cellplot::colorForRegionId(probeResult.regionId)
          : found->second;
      result.overlay.setPixelColor(
          column, row, QColor(color.r, color.g, color.b, color.a));
    }
  }

  return result;
}
