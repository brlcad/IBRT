// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "cellplotoverlaycontroller.h"

#include <QMetaObject>

#include <utility>

CellPlotOverlayController::CellPlotOverlayController(QObject *parent)
    : QObject(parent)
{
  debounceTimer_.setSingleShot(true);
  debounceTimer_.setInterval(75);
  connect(&debounceTimer_,
      &QTimer::timeout,
      this,
      &CellPlotOverlayController::startLatestEvaluation);
  discoverPlugin();
}

CellPlotOverlayController::~CellPlotOverlayController()
{
  debounceTimer_.stop();
  cancelRequested_.store(true, std::memory_order_relaxed);
  if (evaluationThread_.joinable())
    evaluationThread_.join();
  plugin_ = nullptr;
}

void CellPlotOverlayController::discoverPlugin()
{
  pluginLoader_.discover();
  plugin_ = pluginLoader_.plugin();
  setStatus(pluginLoader_.status());
}

bool CellPlotOverlayController::isAvailable() const
{
  return plugin_ != nullptr;
}

bool CellPlotOverlayController::isEnabled() const
{
  return enabled_;
}

void CellPlotOverlayController::setEnabled(bool enabled)
{
  const bool requestedState = enabled && isAvailable();
  if (enabled_ == requestedState)
    return;

  enabled_ = requestedState;
  ++requestedGeneration_;
  if (!enabled_) {
    debounceTimer_.stop();
    cancelRequested_.store(true, std::memory_order_relaxed);
    overlay_ = QImage();
    raysTraced_ = 0;
    cellsHit_ = 0;
    if (isAvailable())
      setStatus(QStringLiteral("Ready"));
    emit overlayChanged();
    return;
  }

  if (pendingDatabasePath_.isEmpty()) {
    setStatus(QStringLiteral("No BRL-CAD scene"));
  } else {
    setStatus(QStringLiteral("Queued"));
    debounceTimer_.start();
  }
}

QString CellPlotOverlayController::pluginName() const
{
  return plugin_ ? plugin_->name() : QString();
}

QString CellPlotOverlayController::status() const
{
  return status_;
}

QImage CellPlotOverlayController::overlay() const
{
  return overlay_;
}

std::uint64_t CellPlotOverlayController::raysTraced() const
{
  return raysTraced_;
}

std::uint64_t CellPlotOverlayController::cellsHit() const
{
  return cellsHit_;
}

void CellPlotOverlayController::requestEvaluation(const QString &databasePath,
    const QString &topObject,
    const ibrt::cellplot::Request &request)
{
  pendingDatabasePath_ = databasePath;
  pendingTopObject_ = topObject;
  pendingRequest_ = request;
  ++requestedGeneration_;

  if (!enabled_ || !plugin_)
    return;

  cancelRequested_.store(true, std::memory_order_relaxed);
  setStatus(QStringLiteral("Queued"));
  debounceTimer_.start();
}

void CellPlotOverlayController::clearScene()
{
  pendingDatabasePath_.clear();
  pendingTopObject_.clear();
  ++requestedGeneration_;
  debounceTimer_.stop();
  cancelRequested_.store(true, std::memory_order_relaxed);
  overlay_ = QImage();
  raysTraced_ = 0;
  cellsHit_ = 0;
  if (isAvailable())
    setStatus(enabled_ ? QStringLiteral("No BRL-CAD scene")
                       : QStringLiteral("Ready"));
  emit overlayChanged();
}

void CellPlotOverlayController::startLatestEvaluation()
{
  if (!enabled_ || !plugin_ || pendingDatabasePath_.isEmpty())
    return;

  if (evaluationRunning_) {
    cancelRequested_.store(true, std::memory_order_relaxed);
    return;
  }

  if (evaluationThread_.joinable())
    evaluationThread_.join();

  const std::uint64_t generation = requestedGeneration_;
  const QString databasePath = pendingDatabasePath_;
  const QString topObject = pendingTopObject_;
  const ibrt::cellplot::Request request = pendingRequest_;
  cancelRequested_.store(false, std::memory_order_relaxed);
  evaluationRunning_ = true;
  setStatus(QStringLiteral("Evaluating %1 x %2 grid...")
          .arg(request.columns)
          .arg(request.rows));

  evaluationThread_ =
      std::thread([this, generation, databasePath, topObject, request]() {
        ibrt::cellplot::Result result;
        QString loadError;
        if (!plugin_->loadScene(databasePath, topObject, loadError)) {
          result.error = loadError;
        } else if (cancelRequested_.load(std::memory_order_relaxed)) {
          result.cancelled = true;
        } else {
          result = plugin_->evaluate(request, &cancelRequested_);
        }

        QMetaObject::invokeMethod(
            this,
            [this, generation, result = std::move(result)]() mutable {
              finishEvaluation(generation, std::move(result));
            },
            Qt::QueuedConnection);
      });
}

void CellPlotOverlayController::finishEvaluation(
    std::uint64_t generation, ibrt::cellplot::Result result)
{
  if (evaluationThread_.joinable())
    evaluationThread_.join();
  evaluationRunning_ = false;

  const bool isLatest = generation == requestedGeneration_;
  if (isLatest && enabled_ && !result.cancelled) {
    if (!result.error.isEmpty()) {
      overlay_ = QImage();
      raysTraced_ = 0;
      cellsHit_ = 0;
      setStatus(result.error);
    } else {
      overlay_ = std::move(result.overlay);
      raysTraced_ = result.raysTraced;
      cellsHit_ = result.cellsHit;
      setStatus(QStringLiteral("Ready: %1/%2 cells hit")
              .arg(cellsHit_)
              .arg(raysTraced_));
    }
    emit overlayChanged();
  }

  if (enabled_ && generation != requestedGeneration_)
    debounceTimer_.start(0);
}

void CellPlotOverlayController::setStatus(const QString &status)
{
  if (status_ == status)
    return;
  status_ = status;
  emit statusChanged();
}
