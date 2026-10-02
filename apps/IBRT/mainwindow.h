// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QMainWindow>
#include <QString>

class RenderWidget;
class QAction;
class RenderWorkerClient;
class QMenu;

class MainWindow : public QMainWindow
{
  Q_OBJECT
 public:
  explicit MainWindow(QWidget *parent = nullptr);

  // Queues a BRL-CAD database supplied by a desktop file association or
  // launcher. A supplied top object bypasses the interactive hierarchy picker.
  void setStartupGeometryDatabase(
      const QString &path, const QString &topObject = QString());

 private:
  // The main viewport and interaction surface for scene rendering.
  RenderWidget *renderWidget_ = nullptr;
  // Optional out-of-process renderer used to keep heavy rendering work off the UI thread.
  RenderWorkerClient *renderWorkerClient_ = nullptr;
  bool workerEverConnected_ = false;
  bool workerReconnectPending_ = false;
  QAction *selectBrlcadObjectAction_ = nullptr;
  QAction *orbitModeAction_ = nullptr;
  QAction *flyModeAction_ = nullptr;
  QString startupDatabasePath_;
  QString startupTopObject_;
  // Menu helpers keep startup/demo logic out of the constructor.
  void setupMenus();
  void updateBrlcadMenuState();
  void chooseAndLoadBrlcadObject(const QString &path);
  QString demoModelsDir() const;
  void populateDemoModelsMenu(QMenu *menu);
  QString defaultDemoPath() const;
  void loadStartupDemo();
  void openGeometryDatabase(
      const QString &path, const QString &topObject = QString());
};
