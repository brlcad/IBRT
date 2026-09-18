// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>

#include <cstdint>
#include <unordered_map>

#include <ibrt/cellplotplugininterface.h>

#undef UNUSED
#undef _USE_MATH_DEFINES
#include "brlcad/common.h"
#include "brlcad/raytrace.h"
#include "brlcad/vmath.h"

class BrlcadCellPlotPlugin final : public QObject,
                                   public ibrt::cellplot::PluginInterface
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID IBRT_CELL_PLOT_PLUGIN_IID)
  Q_INTERFACES(ibrt::cellplot::PluginInterface)

 public:
  BrlcadCellPlotPlugin() = default;
  ~BrlcadCellPlotPlugin() override;

  QString name() const override;
  bool loadScene(const QString &databasePath,
      const QString &topObject,
      QString &error) override;
  ibrt::cellplot::SceneInfo sceneInfo() const override;
  ibrt::cellplot::Result evaluate(const ibrt::cellplot::Request &request,
      const std::atomic_bool *cancelRequested) override;

 private:
  struct ProbeResult
  {
    std::int64_t regionId = -1;
    bool hit = false;
  };

  static int firstHit(application *ap, partition *partitions, seg *segments);
  static int miss(application *ap);
  static int collectRegionColor(region *reg, void *context);

  ProbeResult probe(const ibrt::cellplot::Ray &ray);
  void clearScene();

  rt_i *rtip_ = nullptr;
  resource resource_{};
  bool resourceInitialized_ = false;
  QString loadedDatabasePath_;
  QString loadedTopObject_;
  std::unordered_map<std::int64_t, ibrt::cellplot::Rgba> regionColors_;
  ibrt::cellplot::SceneInfo sceneInfo_;
};
