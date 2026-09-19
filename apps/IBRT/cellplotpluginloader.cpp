// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "cellplotpluginloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QStringList>

CellPlotPluginLoader::~CellPlotPluginLoader()
{
  plugin_ = nullptr;
  if (loader_)
    loader_->unload();
}

bool CellPlotPluginLoader::discover(const QString &explicitPluginPath)
{
  plugin_ = nullptr;
  libraryPath_.clear();
  status_.clear();
  if (loader_) {
    loader_->unload();
    loader_.reset();
  }

  QString requestedPlugin = explicitPluginPath.trimmed();
  if (requestedPlugin.isEmpty())
    requestedPlugin =
        QString::fromLocal8Bit(qgetenv("IBRT_CELL_PLOT_PLUGIN")).trimmed();

  QStringList candidates;
  if (!requestedPlugin.isEmpty()) {
    candidates << QFileInfo(requestedPlugin).absoluteFilePath();
  } else {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirectories = {
        QDir(appDir).filePath(QStringLiteral("plugins/visualizations")),
        appDir};
    for (const QString &directory : searchDirectories) {
      QDir dir(directory);
      const QFileInfoList files = dir.entryInfoList(
          {QStringLiteral("*ibrt_cell_plot*")}, QDir::Files | QDir::Readable);
      for (const QFileInfo &file : files) {
        if (QLibrary::isLibrary(file.absoluteFilePath()))
          candidates << file.absoluteFilePath();
      }
    }
  }

  candidates.removeDuplicates();
  QString lastError;
  for (const QString &candidate : candidates) {
    auto candidateLoader = std::make_unique<QPluginLoader>(candidate);
    QObject *instance = candidateLoader->instance();
    if (!instance) {
      lastError = candidateLoader->errorString();
      continue;
    }

    auto *candidatePlugin =
        qobject_cast<ibrt::cellplot::PluginInterface *>(instance);
    if (!candidatePlugin) {
      lastError = QStringLiteral(
          "Library does not implement the IBRT cell-plot interface: %1")
                      .arg(candidate);
      candidateLoader->unload();
      continue;
    }

    plugin_ = candidatePlugin;
    libraryPath_ = candidate;
    loader_ = std::move(candidateLoader);
    status_ = QStringLiteral("Ready");
    return true;
  }

  if (lastError.isEmpty()) {
    status_ = QStringLiteral(
        "Cell-plot plugin not found (set IBRT_CELL_PLOT_PLUGIN or pass an explicit path).");
  } else {
    status_ =
        QStringLiteral("Cell-plot plugin failed to load: %1").arg(lastError);
  }
  return false;
}

ibrt::cellplot::PluginInterface *CellPlotPluginLoader::plugin() const
{
  return plugin_;
}

QString CellPlotPluginLoader::status() const
{
  return status_;
}

QString CellPlotPluginLoader::libraryPath() const
{
  return libraryPath_;
}
