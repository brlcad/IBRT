// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QString>
#include <QtPlugin>

#include <atomic>
#include <cstdint>

#include <ibrt/cellplotmath.h>

namespace ibrt::cellplot {

inline constexpr int kDefaultGridColumns = 100;
inline constexpr int kDefaultGridRows = 100;

struct Request
{
  Camera camera;
  int columns = kDefaultGridColumns;
  int rows = kDefaultGridRows;
};

struct SceneInfo
{
  Vec3 boundsMin;
  Vec3 boundsMax;
  bool valid = false;
};

struct Result
{
  QImage overlay;
  std::uint64_t raysTraced = 0;
  std::uint64_t cellsHit = 0;
  bool cancelled = false;
  QString error;
};

// Stable host/plugin boundary for secondary cell-plot implementations. Calls
// are serialized by the host and run outside the UI thread.
class PluginInterface
{
 public:
  virtual ~PluginInterface() = default;

  virtual QString name() const = 0;
  virtual bool loadScene(const QString &databasePath,
      const QString &topObject,
      QString &error) = 0;
  virtual SceneInfo sceneInfo() const = 0;
  virtual Result evaluate(
      const Request &request, const std::atomic_bool *cancelRequested) = 0;
};

} // namespace ibrt::cellplot

#define IBRT_CELL_PLOT_PLUGIN_IID "org.brlcad.ibrt.CellPlotPluginInterface/1.0"
Q_DECLARE_INTERFACE(ibrt::cellplot::PluginInterface, IBRT_CELL_PLOT_PLUGIN_IID)
