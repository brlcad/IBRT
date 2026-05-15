// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include "mainwindow.h"
#include "renderwidget.h"
#include "renderworkerclient.h"

#include <algorithm>
#include <functional>
#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <QLabel>

namespace {
QString defaultBrlcadObject(const QStringList &objects, const QString &currentObject)
{
  if (!currentObject.trimmed().isEmpty() && objects.contains(currentObject))
    return currentObject;
  return objects.isEmpty() ? QString() : objects.front();
}

QString firstBrlcadNodeName(const std::vector<OsprayBackend::BrlcadNode> &roots)
{
  return roots.empty() ? QString() : QString::fromStdString(roots.front().name);
}

QString hierarchyNodeLabel(const OsprayBackend::BrlcadNode &node)
{
  QString label = QString::fromStdString(node.name);
  if (node.isRegion)
    label += QStringLiteral(" [region]");
  else if (node.isCombination)
    label += QStringLiteral(" [comb]");
  else if (node.isPrimitive)
    label += QStringLiteral(" [solid]");
  return label;
}

// Recursively populates a Qt tree item hierarchy from BRL-CAD node data.
void populateHierarchyItem(QTreeWidgetItem *parent,
    const OsprayBackend::BrlcadNode &node,
    const QString &currentObject,
    QTreeWidgetItem *&selectedItem)
{
  auto *item = new QTreeWidgetItem(QStringList{hierarchyNodeLabel(node)});
  item->setData(0, Qt::UserRole, QString::fromStdString(node.name));
  if (parent)
    parent->addChild(item);

  if (QString::fromStdString(node.name) == currentObject)
    selectedItem = item;

  for (const auto &child : node.children)
    populateHierarchyItem(item, child, currentObject, selectedItem);
}

// Presents a filterable hierarchy dialog and returns the selected BRL-CAD object.
QString chooseBrlcadObjectHierarchy(QWidget *parent,
    const std::vector<OsprayBackend::BrlcadNode> &roots,
    const QString &currentObject)
{
  // The chooser lists real top-level BRL-CAD objects exactly as reported by the
  // database and applies only lightweight type labels.
  if (roots.empty())
    return QString();

  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("Load BRL-CAD Object"));
  dialog.resize(520, 560);

  auto *layout = new QVBoxLayout(&dialog);
  auto *filterEdit = new QLineEdit(&dialog);
  filterEdit->setPlaceholderText(QStringLiteral("Filter objects..."));
  layout->addWidget(filterEdit);

  auto *tree = new QTreeWidget(&dialog);
  tree->setColumnCount(1);
  tree->setHeaderHidden(true);
  tree->setUniformRowHeights(true);
  tree->header()->setStretchLastSection(true);
  layout->addWidget(tree);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto *okButton = buttons->button(QDialogButtonBox::Ok);
  okButton->setEnabled(false);
  layout->addWidget(buttons);

  QTreeWidgetItem *selectedItem = nullptr;
  for (const auto &root : roots) {
    auto *rootItem = new QTreeWidgetItem(QStringList{hierarchyNodeLabel(root)});
    rootItem->setData(0, Qt::UserRole, QString::fromStdString(root.name));
    tree->addTopLevelItem(rootItem);
    if (QString::fromStdString(root.name) == currentObject)
      selectedItem = rootItem;
    for (const auto &child : root.children)
      populateHierarchyItem(rootItem, child, currentObject, selectedItem);
  }

  if (!selectedItem && tree->topLevelItemCount() > 0)
    selectedItem = tree->topLevelItem(0);

  if (selectedItem) {
    tree->setCurrentItem(selectedItem);
    selectedItem->setSelected(true);
  }

  auto updateSelectionState = [tree, okButton]() {
    const auto *item = tree->currentItem();
    okButton->setEnabled(item && !item->data(0, Qt::UserRole).toString().isEmpty());
  };

  auto applyFilter = [tree](const QString &text) {
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) -> bool {
      bool childVisible = false;
      for (int i = 0; i < item->childCount(); ++i)
        childVisible = visit(item->child(i)) || childVisible;

      const QString display = item->text(0);
      const QString objectName = item->data(0, Qt::UserRole).toString();
      const bool selfMatch = needle.isEmpty()
          || display.contains(needle, Qt::CaseInsensitive)
          || objectName.contains(needle, Qt::CaseInsensitive);
      const bool visible = selfMatch || childVisible;
      item->setHidden(!visible);
      if (!needle.isEmpty() && childVisible)
        item->setExpanded(true);
      return visible;
    };

    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      visit(tree->topLevelItem(i));
  };

  QObject::connect(filterEdit, &QLineEdit::textChanged, &dialog, applyFilter);
  QObject::connect(tree,
      &QTreeWidget::currentItemChanged,
      &dialog,
      [updateSelectionState](QTreeWidgetItem *, QTreeWidgetItem *) { updateSelectionState(); });
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
      [&dialog](QTreeWidgetItem *item, int) {
        if (!item->data(0, Qt::UserRole).toString().isEmpty())
          dialog.accept();
      });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  updateSelectionState();

  if (dialog.exec() != QDialog::Accepted || !tree->currentItem())
    return QString();

  return tree->currentItem()->data(0, Qt::UserRole).toString();
}
} // namespace

// Builds the main window, render widget, and worker wiring.
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  // The window coordinates the viewport and the optional worker process, but
  // all rendering state lives in RenderWidget / OsprayBackend.
  renderWidget_ = new RenderWidget(this);
  renderWorkerClient_ = new RenderWorkerClient(this);
  renderWidget_->setRenderWorkerClient(renderWorkerClient_);
  setCentralWidget(renderWidget_);
  resize(1200, 800);
  setWindowTitle("Interactive BRL-CAD Raytracer");

  connect(renderWidget_,
      &RenderWidget::sceneLoadFinished,
      this,
      [this](bool success, const QString &errorMessage) {
        if (!success) {
          QMessageBox::warning(this,
              "Load Failed",
              errorMessage.isEmpty() ? "Scene load failed." : errorMessage);
        }
        updateBrlcadMenuState();
      });

  connect(renderWorkerClient_,
      &RenderWorkerClient::workerConnectionChanged,
      this,
      [this](bool connected) {
        if (connected) {
          statusBar()->showMessage(workerReconnectPending_
                  ? QStringLiteral("Render worker reconnected.")
                  : QStringLiteral("Render worker connected."),
              2000);
          workerEverConnected_ = true;
          workerReconnectPending_ = false;
          renderWidget_->replayWorkerState();
          return;
        }

        if (!RenderWorkerClient::isSupported())
          return;

        if (workerEverConnected_)
          workerReconnectPending_ = true;
        statusBar()->showMessage(QStringLiteral("Render worker disconnected. Restarting..."), 1500);
        // If the worker dies, try to restore the previous viewport state after
        // reconnecting so the user does not have to manually recover.
        QTimer::singleShot(500, this, [this]() {
          if (renderWorkerClient_->isConnected())
            return;
          if (renderWorkerClient_->restart()) {
            renderWidget_->replayWorkerState();
          } else {
            statusBar()->showMessage(
                QStringLiteral("Render worker restart failed: %1")
                    .arg(renderWorkerClient_->lastError()));
          }
        });
      });

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!RenderWorkerClient::isSupported()) {
    statusBar()->showMessage(QStringLiteral(
        "Render worker unavailable on this platform. Using local rendering."));
  } else if (!renderWorkerClient_->start(workerPath)) {
    statusBar()->showMessage(
        QStringLiteral("Render worker unavailable: %1").arg(renderWorkerClient_->lastError()));
  } else {
    workerEverConnected_ = true;
    statusBar()->showMessage(QStringLiteral("Render worker connected."), 2000);
  }

  setupMenus();
  QTimer::singleShot(0, this, [this]() { loadStartupDemo(); });
}

// Creates the top-level menus and connects them to viewport actions.
void MainWindow::setupMenus()
{
  // Menu actions are thin UI wrappers that delegate actual scene/camera work to
  // RenderWidget.
  QMenu *fileMenu = menuBar()->addMenu("&File");

  QAction *openAction = new QAction("&Open Model...", this);
  fileMenu->addAction(openAction);

  QAction *resetViewAction = new QAction("&Reset View", this);
  fileMenu->addAction(resetViewAction);

  fileMenu->addSeparator();

  QAction *exitAction = new QAction("E&xit", this);
  fileMenu->addAction(exitAction);

  QMenu *viewMenu = menuBar()->addMenu("&View");
  QMenu *brlcadMenu = menuBar()->addMenu("&Select Model");
  QMenu *demoMenu = fileMenu->addMenu("&Demo Models");

  orbitModeAction_ = new QAction("Orbit Mode", this);
  orbitModeAction_->setCheckable(true);
  orbitModeAction_->setChecked(true);

  flyModeAction_ = new QAction("Fly Mode", this);
  flyModeAction_->setCheckable(true);

  QActionGroup *inputModeGroup = new QActionGroup(this);
  inputModeGroup->setExclusive(true);
  inputModeGroup->addAction(orbitModeAction_);
  inputModeGroup->addAction(flyModeAction_);

  QActionGroup *upAxisGroup = new QActionGroup(this);
  upAxisGroup->setExclusive(true);

  QAction *yUpAction = new QAction("Y-Up", this);
  yUpAction->setCheckable(true);
  upAxisGroup->addAction(yUpAction);

  QAction *zUpAction = new QAction("Z-Up", this);
  zUpAction->setCheckable(true);
  zUpAction->setChecked(true);
  upAxisGroup->addAction(zUpAction);

  viewMenu->addAction(orbitModeAction_);
  viewMenu->addAction(flyModeAction_);
  viewMenu->addSeparator();
  viewMenu->addAction(yUpAction);
  viewMenu->addAction(zUpAction);

  selectBrlcadObjectAction_ = new QAction("Object Hierarchy", this);
  brlcadMenu->addAction(selectBrlcadObjectAction_);
  updateBrlcadMenuState();
  populateDemoModelsMenu(demoMenu);

  connect(openAction, &QAction::triggered, this, [this]() {
    QString path = QFileDialog::getOpenFileName(this,
        "Open Model",
        QString(),
        "Model Files (*.obj *.g *.stl *.ply);;OBJ Files (*.obj);;BRL-CAD Files (*.g)");

    if (path.isEmpty())
      return;

    QFileInfo info(path);
    const QString ext = info.suffix().toLower();

    if (ext == "obj") {
      if (!renderWidget_->loadModel(path)) {
        const QString detail = renderWidget_->lastError();
        QMessageBox::warning(this,
            "Load Failed",
            detail.isEmpty() ? "Could not load OBJ file." : detail);
      }
      return;
    }

    if (ext == "g") {
      chooseAndLoadBrlcadObject(path);
      return;
    }

    QMessageBox::information(this,
        "Not Yet Implemented",
        "That file type is not implemented yet. Start with OBJ for testing.");
  });

  connect(resetViewAction, &QAction::triggered, this, [this]() {
    renderWidget_->rebuildSceneAndResetView();
  });

  connect(exitAction, &QAction::triggered, this, [this]() { close(); });

  connect(orbitModeAction_,
      &QAction::triggered,
      this,
      [this]() {
        renderWidget_->setInputMode(RenderWidget::InputMode::Orbit);
      });

  connect(flyModeAction_,
      &QAction::triggered,
      this,
      [this]() {
        renderWidget_->setInputMode(RenderWidget::InputMode::Fly);
      });

  connect(renderWidget_,
      &RenderWidget::inputModeChanged,
      this,
      [this](RenderWidget::InputMode mode) {
        if (!orbitModeAction_ || !flyModeAction_)
          return;
        orbitModeAction_->setChecked(mode == RenderWidget::InputMode::Orbit);
        flyModeAction_->setChecked(mode == RenderWidget::InputMode::Fly);
      });

  connect(yUpAction, &QAction::triggered, this, [this, yUpAction, zUpAction]() {
    yUpAction->setChecked(true);
    zUpAction->setChecked(false);
    renderWidget_->setUpAxis(RenderWidget::UpAxis::Y);
  });

  connect(zUpAction, &QAction::triggered, this, [this, yUpAction, zUpAction]() {
    zUpAction->setChecked(true);
    yUpAction->setChecked(false);
    renderWidget_->setUpAxis(RenderWidget::UpAxis::Z);
  });

  connect(selectBrlcadObjectAction_, &QAction::triggered, this, [this]() {
    if (!renderWidget_->hasBrlcadScene())
      return;
    chooseAndLoadBrlcadObject(renderWidget_->currentBrlcadPath());
  });
}

// Populates the demo-model submenu with any deployed sample databases.
void MainWindow::populateDemoModelsMenu(QMenu *menu)
{
  if (!menu)
    return;
  const QString dbDir = demoModelsDir();

  struct DemoModel
  {
    const char *fileName;
    const char *label;
  };

  const DemoModel models[] = {
      {"moss.g", "Moss"},
      {"havoc.g", "Havoc"},
  };

  bool addedAny = false;
  for (const DemoModel &model : models) {
    // Only advertise demo assets that are actually deployed with the build.
    const QString path = QDir(dbDir).filePath(QString::fromLatin1(model.fileName));
    if (!QFileInfo::exists(path))
      continue;

    QAction *action = new QAction(QString::fromLatin1(model.label), this);
    connect(action, &QAction::triggered, this, [this, path]() {
      chooseAndLoadBrlcadObject(path);
    });
    menu->addAction(action);
    addedAny = true;
  }

  if (!addedAny) {
    QAction *missingAction = new QAction("Demo Models Unavailable", this);
    missingAction->setEnabled(false);
    menu->addAction(missingAction);
  }
}

// Returns the preferred startup demo database path if one is available.
QString MainWindow::defaultDemoPath() const
{
  const QString dbDir = demoModelsDir();
  const QString mossPath = QDir(dbDir).filePath(QStringLiteral("moss.g"));
  if (QFileInfo::exists(mossPath))
    return mossPath;
  return QString();
}

// Locates the directory that contains shipped or installed BRL-CAD demo databases.
QString MainWindow::demoModelsDir() const
{
  const QString localDir =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
  if (QFileInfo::exists(QDir(localDir).filePath(QStringLiteral("moss.g"))))
    return localDir;

#ifdef BRLCAD_INSTALL_PREFIX
  return QDir(QStringLiteral(BRLCAD_INSTALL_PREFIX)).filePath(QStringLiteral("share/db"));
#else
  return QString();
#endif
}

// Loads a default demo scene after startup when no other scene is active.
void MainWindow::loadStartupDemo()
{
  // Start with a known-good BRL-CAD database when one is available so the app
  // opens with visible content instead of an empty viewport.
  if (!renderWidget_ || renderWidget_->hasBrlcadScene()
      || !renderWidget_->currentBrlcadPath().isEmpty())
    return;

  const QString path = defaultDemoPath();
  if (path.isEmpty())
    return;

  const QStringList objects = renderWidget_->listBrlcadObjects(path);
  const auto roots = renderWidget_->brlcadHierarchy(path);
  QString obj = defaultBrlcadObject(objects, firstBrlcadNodeName(roots));
  if (QFileInfo(path).fileName().compare(QStringLiteral("moss.g"), Qt::CaseInsensitive) == 0
      && objects.contains(QStringLiteral("all.g"))) {
    obj = QStringLiteral("all.g");
  }

  if (obj.isEmpty()) {
    statusBar()->showMessage(QStringLiteral("Startup demo load failed: no BRL-CAD objects found."),
        5000);
    return;
  }

  if (!renderWidget_->loadBrlcadModel(path, obj)) {
    statusBar()->showMessage(QStringLiteral("Startup demo load failed: %1")
                                 .arg(renderWidget_->lastError()),
        5000);
  }
}

// Enables or disables BRL-CAD-specific menu actions based on the current scene type.
void MainWindow::updateBrlcadMenuState()
{
  if (selectBrlcadObjectAction_)
    selectBrlcadObjectAction_->setEnabled(renderWidget_->hasBrlcadScene());
}

// Prompts for a BRL-CAD object and asks the render widget to load it.
void MainWindow::chooseAndLoadBrlcadObject(
    const QString &path)
{
  if (path.isEmpty())
    return;

  const auto roots = renderWidget_->brlcadHierarchy(path);
  if (roots.empty()) {
    QMessageBox::warning(this,
        "No Objects Found",
        "No selectable BRL-CAD objects were found in this .g file.");
    return;
  }

  QString obj = chooseBrlcadObjectHierarchy(this, roots, renderWidget_->currentBrlcadObject());
  if (obj.isEmpty())
    return;

  if (!renderWidget_->loadBrlcadModel(path, obj)) {
    const QString detail = renderWidget_->lastError();
    QMessageBox::warning(this, "Load Failed",
        detail.isEmpty()
            ? "Could not load BRL-CAD .g file.\n"
              "Check that the object name exists in the database."
            : detail);
    return;
  }

  statusBar()->showMessage(QStringLiteral("Loaded BRL-CAD object: %1").arg(obj), 5000);
}
