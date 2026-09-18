// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <QCoreApplication>
#include <QPluginLoader>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include <ibrt/cellplotplugininterface.h>

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <plugin> <database.g> <object>\n", argv[0]);
    return 2;
  }

  QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
  QObject *instance = loader.instance();
  if (!instance) {
    std::fprintf(stderr,
        "plugin load failed: %s\n",
        loader.errorString().toUtf8().constData());
    return 3;
  }

  auto *plugin = qobject_cast<ibrt::cellplot::PluginInterface *>(instance);
  if (!plugin) {
    std::fprintf(
        stderr, "loaded library does not implement the cell-plot interface\n");
    return 4;
  }

  QString error;
  if (!plugin->loadScene(QString::fromLocal8Bit(argv[2]),
          QString::fromLocal8Bit(argv[3]),
          error)) {
    std::fprintf(stderr, "scene load failed: %s\n", error.toUtf8().constData());
    return 5;
  }

  const ibrt::cellplot::SceneInfo scene = plugin->sceneInfo();
  if (!scene.valid) {
    std::fprintf(stderr, "plugin did not report prepared scene bounds\n");
    return 6;
  }

  const ibrt::cellplot::Vec3 center{
      0.5 * (scene.boundsMin.x + scene.boundsMax.x),
      0.5 * (scene.boundsMin.y + scene.boundsMax.y),
      0.5 * (scene.boundsMin.z + scene.boundsMax.z)};
  const double extent = std::max({scene.boundsMax.x - scene.boundsMin.x,
      scene.boundsMax.y - scene.boundsMin.y,
      scene.boundsMax.z - scene.boundsMin.z,
      1.0});

  ibrt::cellplot::Request request;
  request.columns = 24;
  request.rows = 20;
  request.camera.position = {center.x, center.y, center.z + 2.0 * extent};
  request.camera.forward = center - request.camera.position;
  request.camera.up = {0.0, 1.0, 0.0};
  request.camera.verticalFovDegrees = 60.0;
  request.camera.aspectRatio = double(request.columns) / double(request.rows);
  request.camera.focusDistance = 2.0 * extent;

  std::atomic_bool cancel{false};
  const auto first = plugin->evaluate(request, &cancel);
  if (!first.error.isEmpty() || first.cancelled
      || first.raysTraced != std::uint64_t(request.columns * request.rows)
      || first.overlay.width() != request.columns
      || first.overlay.height() != request.rows || first.cellsHit == 0) {
    std::fprintf(stderr,
        "first evaluation failed: error='%s', rays=%llu, hits=%llu, image=%dx%d\n",
        first.error.toUtf8().constData(),
        static_cast<unsigned long long>(first.raysTraced),
        static_cast<unsigned long long>(first.cellsHit),
        first.overlay.width(),
        first.overlay.height());
    return 7;
  }

  const auto repeated = plugin->evaluate(request, &cancel);
  if (repeated.overlay != first.overlay
      || repeated.cellsHit != first.cellsHit) {
    std::fprintf(stderr, "repeated view was not deterministic\n");
    return 8;
  }

  request.camera.position.x += 0.35 * extent;
  request.camera.forward = center - request.camera.position;
  const auto moved = plugin->evaluate(request, &cancel);
  if (!moved.error.isEmpty() || moved.cancelled
      || moved.overlay == first.overlay) {
    std::fprintf(stderr, "changed view did not produce a new cell plot\n");
    return 9;
  }

  std::printf(
      "CellPlotPluginSmoke: %s, %llu/%llu cells hit; view update verified\n",
      plugin->name().toUtf8().constData(),
      static_cast<unsigned long long>(first.cellsHit),
      static_cast<unsigned long long>(first.raysTraced));
  return 0;
}
