// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <ibrt/cellplotplugininterface.h>

#include "cellplotpluginloader.h"

// Keeps plugin discovery, scene caching, cancellation, and background work out
// of RenderWidget. The widget only submits immutable view snapshots and paints
// the latest completed overlay.
class CellPlotOverlayController final : public QObject
{
  Q_OBJECT

 public:
  explicit CellPlotOverlayController(QObject *parent = nullptr);
  ~CellPlotOverlayController() override;

  bool isAvailable() const;
  bool isEnabled() const;
  void setEnabled(bool enabled);
  QString pluginName() const;
  QString status() const;
  QImage overlay() const;
  std::uint64_t raysTraced() const;
  std::uint64_t cellsHit() const;

  void requestEvaluation(const QString &databasePath,
      const QString &topObject,
      const ibrt::cellplot::Request &request);
  void clearScene();

 signals:
  void overlayChanged();
  void statusChanged();

 private:
  void discoverPlugin();
  void startLatestEvaluation();
  void finishEvaluation(
      std::uint64_t generation, ibrt::cellplot::Result result);
  void setStatus(const QString &status);

  CellPlotPluginLoader pluginLoader_;
  ibrt::cellplot::PluginInterface *plugin_ = nullptr;
  QTimer debounceTimer_;
  std::thread evaluationThread_;
  std::atomic_bool cancelRequested_{false};
  bool enabled_ = false;
  bool evaluationRunning_ = false;
  std::uint64_t requestedGeneration_ = 0;
  QString pendingDatabasePath_;
  QString pendingTopObject_;
  ibrt::cellplot::Request pendingRequest_;
  QString status_;
  QImage overlay_;
  std::uint64_t raysTraced_ = 0;
  std::uint64_t cellsHit_ = 0;
};
