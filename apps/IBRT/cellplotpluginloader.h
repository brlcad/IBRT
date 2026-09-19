// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QPluginLoader>
#include <QString>

#include <memory>

#include <ibrt/cellplotplugininterface.h>

// Shared runtime discovery/ownership for GUI and headless cell-plot hosts.
// Keeping QPluginLoader alive keeps the interface instance alive.
class CellPlotPluginLoader final
{
 public:
  CellPlotPluginLoader() = default;
  ~CellPlotPluginLoader();

  CellPlotPluginLoader(const CellPlotPluginLoader &) = delete;
  CellPlotPluginLoader &operator=(const CellPlotPluginLoader &) = delete;

  bool discover(const QString &explicitPluginPath = QString());
  ibrt::cellplot::PluginInterface *plugin() const;
  QString status() const;
  QString libraryPath() const;

 private:
  std::unique_ptr<QPluginLoader> loader_;
  ibrt::cellplot::PluginInterface *plugin_ = nullptr;
  QString status_;
  QString libraryPath_;
};
