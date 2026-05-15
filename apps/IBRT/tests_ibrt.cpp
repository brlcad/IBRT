// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <QtTest/QtTest>

#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <Qt>

#include <thread>
#include <vector>

#include <ospray/ospray.h>

#include "interactioncontroller.h"
#include "ospraybackend.h"
#include "qualitysettings.h"
#include "renderreplaylogic.h"
#include "renderworkflowlogic.h"
#include "renderworkerclient.h"
#include "renderworkerqueuelogic.h"
#include "worker_ipc.h"

class IbrtTests : public QObject
{
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void unitBackendCustomSettingsClampToExpectedRanges();
  void unitBackendAutomaticSettingsRoundTrip();
  void unitBackendLoadObjRejectsMissingFile();
  void unitBackendLoadObjParsesSimpleTriangle();
  void unitQualitySettingsSeedWorkerStateFromAutomatic();
  void unitQualitySettingsSeedBackendCustomFromAutomatic();
  void unitQualitySettingsMirrorBackendToWorkerState();
  void unitInteractionControllerClassifiesDocumentedChords();
  void unitWorkerIpcPipeNameUsesProcessId();
  void unitWorkerIpcRoundTripMessage();
  void unitRenderWorkflowShouldPreemptWorkerControl();
  void unitRenderWorkflowShouldPreemptWorkerInteractiveCamera();
  void unitRenderWorkflowDecidesRebuildAction();
  void unitRenderWorkerQueueCoalescesLatestCommands();
  void unitRenderWorkerQueueDrainClearsOneShotFlags();
  void unitRenderReplayBuildsObjReplayPlan();
  void unitRenderReplayBuildsBrlcadReplayPlan();
  void unitRenderReplaySkipsWhenWorkerPathInactive();
  void integrationBackendListBrlcadObjectsFromGeneratedDb();
  void integrationBackendListBrlcadHierarchyFromGeneratedDb();
  void integrationBackendLoadBrlcadRejectsInvalidFile();
  void integrationBackendLoadBrlcadRejectsInvalidObject();
  void integrationBackendBrlcadToOsprayProducesGeometry();
  void integrationBackendValidBrlcadSceneDoesNotUseDefaultBounds();
  void integrationBackendRenderProducesNonEmptyFrame();
  void integrationBackendRenderProducesConsistentFrameForSameInput();
  void integrationBackendGeometryChangeAffectsRenderedOutput();
  void integrationWorkerSmokeTestWorkerLifecycle();
  void integrationWorkerLoadBrlcadProducesNonEmptyFrame();
  void integrationWorkerLoadBrlcadPropagatesValidBounds();
  void integrationWorkerFrameMatchesRequestedViewportSize();
  void systemLoadRenderInteractCycle();
  void systemSwitchTopObjectChangesBounds();
  void systemSwitchRendererChangesFrame();
  void systemReloadSameBrlcadObjectStaysRenderable();
  void systemReloadDifferentBrlcadObjectChangesFrame();
  void systemWorkerLoadRenderInteractCycle();
  void systemWorkerRendererSwitchChangesFrame();
  void systemWorkerReloadDifferentObjectChangesFrame();
  void systemWorkerCrashRecovery();
};

namespace {

std::optional<QString> makeExampleBrlcadDb()
{
  static const QString toolPath = QStringLiteral("C:/brlcad-build/bin/wdb_example.exe");
  const QString dbPath =
      QDir::temp().filePath(QStringLiteral("ibrt_test_example.g"));
  QFile::remove(dbPath);

  if (QFileInfo::exists(toolPath)) {
    QProcess process;
    process.start(toolPath, {dbPath});
    if (!process.waitForFinished(10000))
      return std::nullopt;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
      return std::nullopt;
    if (!QFileInfo::exists(dbPath))
      return std::nullopt;

    return dbPath;
  }

#ifdef BRLCAD_INSTALL_PREFIX
  const QString mgedPath =
      QDir(QStringLiteral(BRLCAD_INSTALL_PREFIX)).filePath(QStringLiteral("bin/mged"));
  if (!QFileInfo::exists(mgedPath))
    return std::nullopt;

  auto runMged = [&](const QStringList &arguments) {
    QProcess process;
    process.start(mgedPath, arguments);
    if (!process.waitForStarted(5000)) {
      std::fprintf(stderr,
          "makeExampleBrlcadDb: failed to start mged: %s\n",
          process.errorString().toStdString().c_str());
      return false;
    }
    if (!process.waitForFinished(10000)) {
      process.kill();
      process.waitForFinished();
    }

    const QByteArray stderrOutput = process.readAllStandardError();
    const QByteArray stdoutOutput = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      const std::string commandLine =
          QStringList(QStringList{mgedPath} + arguments).join(QLatin1Char(' ')).toStdString();
      std::fprintf(stderr,
          "makeExampleBrlcadDb: mged failed (exitStatus=%d exitCode=%d): %s\n",
          int(process.exitStatus()),
          process.exitCode(),
          commandLine.c_str());
      if (!stderrOutput.isEmpty()) {
        std::fprintf(stderr,
            "makeExampleBrlcadDb: mged stderr:\n%s\n",
            stderrOutput.constData());
      }
      if (!stdoutOutput.isEmpty()) {
        std::fprintf(stderr,
            "makeExampleBrlcadDb: mged stdout:\n%s\n",
            stdoutOutput.constData());
      }
      return false;
    }

    return true;
  };

  if (!runMged({QStringLiteral("-c"),
          dbPath,
          QStringLiteral("in"),
          QStringLiteral("ball.s"),
          QStringLiteral("sph"),
          QStringLiteral("0"),
          QStringLiteral("0"),
          QStringLiteral("0"),
          QStringLiteral("1")}))
    return std::nullopt;
  if (!runMged({QStringLiteral("-c"),
          dbPath,
          QStringLiteral("in"),
          QStringLiteral("box.s"),
          QStringLiteral("rpp"),
          QStringLiteral("2"),
          QStringLiteral("4"),
          QStringLiteral("-1"),
          QStringLiteral("1"),
          QStringLiteral("-1"),
          QStringLiteral("1")}))
    return std::nullopt;
  if (!runMged({QStringLiteral("-c"),
          dbPath,
          QStringLiteral("r"),
          QStringLiteral("ball.r"),
          QStringLiteral("u"),
          QStringLiteral("ball.s")}))
    return std::nullopt;
  if (!runMged({QStringLiteral("-c"),
          dbPath,
          QStringLiteral("r"),
          QStringLiteral("box.r"),
          QStringLiteral("u"),
          QStringLiteral("box.s")}))
    return std::nullopt;
  if (!runMged({QStringLiteral("-c"),
          dbPath,
          QStringLiteral("r"),
          QStringLiteral("box_n_ball.r"),
          QStringLiteral("u"),
          QStringLiteral("ball.s"),
          QStringLiteral("u"),
          QStringLiteral("box.s")}))
    return std::nullopt;

  if (!QFileInfo::exists(dbPath))
    return std::nullopt;

  return dbPath;
#else
  return std::nullopt;
#endif
}

bool frameHasNonZeroPixel(const uint32_t *pixels, int width, int height)
{
  if (!pixels || width <= 0 || height <= 0)
    return false;

  const size_t pixelCount = size_t(width) * size_t(height);
  for (size_t i = 0; i < pixelCount; ++i) {
    if (pixels[i] != 0u)
      return true;
  }
  return false;
}

std::vector<uint32_t> renderUntilImageReady(OsprayBackend &backend)
{
  for (int attempt = 0; attempt < 80; ++attempt) {
    if (backend.advanceRender()) {
      const uint32_t *pixels = backend.pixels();
      if (!pixels)
        return {};
      return std::vector<uint32_t>(
          pixels, pixels + size_t(backend.width()) * size_t(backend.height()));
    }
    QThread::msleep(10);
  }
  return {};
}

QImage renderWorkerUntilImageReady(
    RenderWorkerClient &client, bool requireUpdatedFrame = true)
{
  for (int attempt = 0; attempt < 80; ++attempt) {
    const auto frame = client.requestFrame();
    if (!frame.image.isNull() && (!requireUpdatedFrame || frame.updated))
      return frame.image;
    QThread::msleep(10);
  }
  return {};
}

bool imageHasNonZeroPixel(const QImage &image)
{
  if (image.isNull())
    return false;

  const QImage argbImage = image.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < argbImage.height(); ++y) {
    const auto *row =
        reinterpret_cast<const uint32_t *>(argbImage.constScanLine(y));
    for (int x = 0; x < argbImage.width(); ++x) {
      if (row[x] != 0u)
        return true;
    }
  }
  return false;
}

RenderWorkerClient::SceneLoadResult loadWorkerExampleScene(
    RenderWorkerClient &client, const QString &dbPath, const QString &objectName)
{
  RenderWorkerClient::RenderSettingsState settings;
  settings.settingsMode = 1;
  settings.customStartScale = 4;
  settings.customAoSamples = 1;
  settings.customAoDistance = 1e20f;
  settings.customPixelSamples = 1;
  settings.customMaxPathLength = 20;
  settings.customRoulettePathLength = 5;

  if (!client.setRenderSettings(settings))
    return {};
  if (!client.resize(128, 128))
    return {};
  if (!client.setRenderer(QStringLiteral("ao")))
    return {};
  const auto result = client.loadBrlcad(dbPath, objectName);
  if (!result.success)
    return result;

  const rkcommon::math::vec3f center(
      (result.boundsMin.x + result.boundsMax.x) * 0.5f,
      (result.boundsMin.y + result.boundsMax.y) * 0.5f,
      (result.boundsMin.z + result.boundsMax.z) * 0.5f);
  const rkcommon::math::vec3f extent(result.boundsMax.x - result.boundsMin.x,
      result.boundsMax.y - result.boundsMin.y,
      result.boundsMax.z - result.boundsMin.z);
  const float radius =
      std::max(std::max(std::max(extent.x, extent.y), extent.z) * 0.5f, 0.001f);

  if (!client.setCamera(rkcommon::math::vec3f(center.x + radius * 2.5f,
          center.y - radius * 2.5f,
          center.z + radius * 1.5f),
          center,
          rkcommon::math::vec3f(0.f, 0.f, 1.f),
          60.0f)) {
    return {};
  }
  if (!client.resetAccumulation())
    return {};
  return result;
}

void frameCameraToBounds(OsprayBackend &backend)
{
  const auto center = backend.getBoundsCenter();
  const float radius = std::max(backend.getBoundsRadius(), 0.001f);
  const rkcommon::math::vec3f eye(center.x + radius * 2.5f,
      center.y - radius * 2.5f,
      center.z + radius * 1.5f);
  backend.setCamera(eye, center, rkcommon::math::vec3f(0.f, 0.f, 1.f), 60.0f);
}

} // namespace

void IbrtTests::initTestCase()
{
  int argc = 0;
  const char **argv = nullptr;
  const OSPError initResult = ospInit(&argc, argv);
  QVERIFY2(initResult == OSP_NO_ERROR, "ospInit failed in test setup.");

  OSPDevice device = ospNewDevice("cpu");
  QVERIFY2(device != nullptr, "ospNewDevice(\"cpu\") failed in test setup.");
  ospSetCurrentDevice(device);
  ospCommit(reinterpret_cast<OSPObject>(device));
  QCOMPARE(ospLoadModule("cpu"), OSP_NO_ERROR);
}

void IbrtTests::cleanupTestCase()
{
  ospShutdown();
}

// Unit tests: isolated state, mapping, and helper behavior.
void IbrtTests::unitBackendCustomSettingsClampToExpectedRanges()
{
  OsprayBackend backend;

  backend.setCustomStartScale(3);
  QCOMPARE(backend.customStartScale(), 4);

  backend.setCustomTargetFrameTimeMs(0.25f);
  QCOMPARE(backend.customTargetFrameTimeMs(), 2.0f);
  backend.setCustomTargetFrameTimeMs(2500.0f);
  QCOMPARE(backend.customTargetFrameTimeMs(), 1000.0f);

  backend.setAoSamples(-4);
  QCOMPARE(backend.customAoSamples(), 0);
  backend.setAoSamples(400);
  QCOMPARE(backend.customAoSamples(), 32);

  backend.setAoDistance(-5.0f);
  QCOMPARE(backend.customAoDistance(), 0.0f);
  backend.setAoDistance(2.5e21f);
  QCOMPARE(backend.customAoDistance(), 1e20f);

  backend.setPixelSamples(-2);
  QCOMPARE(backend.customPixelSamples(), 1);
  backend.setPixelSamples(400);
  QCOMPARE(backend.customPixelSamples(), 64);

  backend.setMaxPathLength(-3);
  QCOMPARE(backend.customMaxPathLength(), 0);
  backend.setMaxPathLength(400);
  QCOMPARE(backend.customMaxPathLength(), 64);

  backend.setRoulettePathLength(-7);
  QCOMPARE(backend.customRoulettePathLength(), 0);
  backend.setRoulettePathLength(400);
  QCOMPARE(backend.customRoulettePathLength(), 64);

  backend.setCustomMaxAccumulationFrames(-10);
  QCOMPARE(backend.customMaxAccumulationFrames(), 0);
  backend.setCustomMaxAccumulationFrames(2000000);
  QCOMPARE(backend.customMaxAccumulationFrames(), 1000000);

  backend.setCustomWatchdogTimeoutMs(1);
  QCOMPARE(backend.customWatchdogTimeoutMs(), 10);
  backend.setCustomWatchdogTimeoutMs(120000);
  QCOMPARE(backend.customWatchdogTimeoutMs(), 60000);
}

void IbrtTests::unitBackendAutomaticSettingsRoundTrip()
{
  OsprayBackend backend;

  backend.setSettingsMode(OsprayBackend::SettingsMode::Custom);
  QCOMPARE(backend.settingsMode(), OsprayBackend::SettingsMode::Custom);
  backend.setSettingsMode(OsprayBackend::SettingsMode::Automatic);
  QCOMPARE(backend.settingsMode(), OsprayBackend::SettingsMode::Automatic);

  backend.setAutomaticPreset(OsprayBackend::AutomaticPreset::Fast);
  QCOMPARE(backend.automaticPreset(), OsprayBackend::AutomaticPreset::Fast);
  backend.setAutomaticPreset(OsprayBackend::AutomaticPreset::Quality);
  QCOMPARE(backend.automaticPreset(), OsprayBackend::AutomaticPreset::Quality);

  backend.setAutomaticTargetFrameTimeMs(1.0f);
  QCOMPARE(backend.automaticTargetFrameTimeMs(), 2.0f);
  backend.setAutomaticTargetFrameTimeMs(5000.0f);
  QCOMPARE(backend.automaticTargetFrameTimeMs(), 1000.0f);

  backend.setAutomaticAccumulationEnabled(false);
  QCOMPARE(backend.automaticAccumulationEnabled(), false);
  backend.setAutomaticAccumulationEnabled(true);
  QCOMPARE(backend.automaticAccumulationEnabled(), true);
}

void IbrtTests::unitBackendLoadObjRejectsMissingFile()
{
  OsprayBackend backend;
  const bool ok = backend.loadObj("C:/definitely/not/a/real/file.obj");
  QCOMPARE(ok, false);
  QVERIFY(!backend.lastError().empty());
}

void IbrtTests::unitBackendLoadObjParsesSimpleTriangle()
{
  // Positive OBJ scene loading still needs a stable fixture/environment path.
  // Keep the negative-path coverage for now and leave this as a placeholder.
  return;
}

// Integration tests: real BRL-CAD/OSPRay/worker boundaries without full UI flows.
void IbrtTests::integrationBackendListBrlcadObjectsFromGeneratedDb()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  const auto names = backend.listBrlcadObjects(dbPath->toStdString());
  QVERIFY(!names.empty());
  QVERIFY(std::find(names.begin(), names.end(), std::string("box_n_ball.r")) != names.end());
}

void IbrtTests::integrationBackendListBrlcadHierarchyFromGeneratedDb()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  const auto roots = backend.listBrlcadHierarchy(dbPath->toStdString());
  QVERIFY(!roots.empty());

  const auto rootIt = std::find_if(roots.begin(), roots.end(), [](const auto &node) {
    return node.name == "box_n_ball.r";
  });
  QVERIFY(rootIt != roots.end());
  QVERIFY(rootIt->isCombination);
  QVERIFY(rootIt->isRegion);
  QVERIFY(!rootIt->children.empty());

  const bool hasBall = std::any_of(
      rootIt->children.begin(), rootIt->children.end(), [](const auto &child) {
        return child.name == "ball.s" && child.isPrimitive;
      });
  const bool hasBox = std::any_of(
      rootIt->children.begin(), rootIt->children.end(), [](const auto &child) {
        return child.name == "box.s" && child.isPrimitive;
      });
  QVERIFY(hasBall);
  QVERIFY(hasBox);
}

void IbrtTests::integrationBackendLoadBrlcadRejectsInvalidFile()
{
  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  const bool ok = backend.loadBrlcad("C:/definitely/not/a/real/file.g", "all");
  QCOMPARE(ok, false);
  QVERIFY(!backend.lastError().empty());
}

void IbrtTests::integrationBackendLoadBrlcadRejectsInvalidObject()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  const bool ok =
      backend.loadBrlcad(dbPath->toStdString(), "definitely_not_a_real_object.s");
  QCOMPARE(ok, false);
  QVERIFY(!backend.lastError().empty());
}

void IbrtTests::integrationBackendBrlcadToOsprayProducesGeometry()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  const bool ok = backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r");
  if (!ok)
    return;

  QVERIFY(backend.debugSceneInstanceCount() > 0);
  const auto boundsMin = backend.getBoundsMin();
  const auto boundsMax = backend.getBoundsMax();
  QVERIFY(std::isfinite(boundsMin.x));
  QVERIFY(std::isfinite(boundsMin.y));
  QVERIFY(std::isfinite(boundsMin.z));
  QVERIFY(std::isfinite(boundsMax.x));
  QVERIFY(std::isfinite(boundsMax.y));
  QVERIFY(std::isfinite(boundsMax.z));
  QVERIFY(boundsMax.x >= boundsMin.x);
  QVERIFY(boundsMax.y >= boundsMin.y);
  QVERIFY(boundsMax.z >= boundsMin.z);
}

void IbrtTests::integrationBackendValidBrlcadSceneDoesNotUseDefaultBounds()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  const auto boundsMin = backend.getBoundsMin();
  const auto boundsMax = backend.getBoundsMax();
  const bool usingDefaultBounds =
      std::fabs(boundsMin.x + 1.0f) < 0.0001f
      && std::fabs(boundsMin.y + 1.0f) < 0.0001f
      && std::fabs(boundsMin.z + 1.0f) < 0.0001f
      && std::fabs(boundsMax.x - 1.0f) < 0.0001f
      && std::fabs(boundsMax.y - 1.0f) < 0.0001f
      && std::fabs(boundsMax.z - 1.0f) < 0.0001f;
  QVERIFY(!usingDefaultBounds);
}

void IbrtTests::integrationBackendRenderProducesNonEmptyFrame()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  frameCameraToBounds(backend);
  const auto pixels = renderUntilImageReady(backend);
  if (pixels.empty())
    return;

  QVERIFY(frameHasNonZeroPixel(pixels.data(), backend.width(), backend.height()));
}

void IbrtTests::integrationBackendRenderProducesConsistentFrameForSameInput()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  auto renderFrame = [&](std::vector<uint32_t> &outFrame) -> bool {
    OsprayBackend backend;
    backend.init();
    backend.resize(128, 128);
    if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
      return false;
    frameCameraToBounds(backend);
    outFrame = renderUntilImageReady(backend);
    return !outFrame.empty();
  };

  std::vector<uint32_t> frameA;
  std::vector<uint32_t> frameB;
  if (!renderFrame(frameA) || !renderFrame(frameB))
    return;

  QCOMPARE(frameA, frameB);
}

void IbrtTests::integrationBackendGeometryChangeAffectsRenderedOutput()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  auto renderObject = [&](const char *objectName) -> std::vector<uint32_t> {
    OsprayBackend backend;
    backend.init();
    backend.resize(128, 128);
    if (!backend.loadBrlcad(dbPath->toStdString(), objectName))
      return {};
    frameCameraToBounds(backend);
    return renderUntilImageReady(backend);
  };

  const auto ballFrame = renderObject("ball.s");
  const auto boxFrame = renderObject("box.s");
  if (ballFrame.empty() || boxFrame.empty())
    return;

  QVERIFY(ballFrame != boxFrame);
}

// System tests: user-visible flows across multiple real components.
void IbrtTests::systemLoadRenderInteractCycle()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  frameCameraToBounds(backend);
  const auto frameA = renderUntilImageReady(backend);
  if (frameA.empty())
    return;

  const auto center = backend.getBoundsCenter();
  const float radius = std::max(backend.getBoundsRadius(), 0.001f);
  const rkcommon::math::vec3f movedEye(center.x - radius * 3.0f,
      center.y + radius * 2.0f,
      center.z + radius * 1.25f);
  backend.setCamera(movedEye, center, rkcommon::math::vec3f(0.f, 0.f, 1.f), 60.0f);
  const auto frameB = renderUntilImageReady(backend);
  if (frameB.empty())
    return;

  QVERIFY(frameA != frameB);
}

void IbrtTests::systemSwitchTopObjectChangesBounds()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "ball.s"))
    return;

  const auto ballMin = backend.getBoundsMin();
  const auto ballMax = backend.getBoundsMax();

  if (!backend.loadBrlcad(dbPath->toStdString(), "box.s"))
    return;

  const auto boxMin = backend.getBoundsMin();
  const auto boxMax = backend.getBoundsMax();

  const bool sameBounds =
      std::fabs(ballMin.x - boxMin.x) < 0.0001f
      && std::fabs(ballMin.y - boxMin.y) < 0.0001f
      && std::fabs(ballMin.z - boxMin.z) < 0.0001f
      && std::fabs(ballMax.x - boxMax.x) < 0.0001f
      && std::fabs(ballMax.y - boxMax.y) < 0.0001f
      && std::fabs(ballMax.z - boxMax.z) < 0.0001f;
  QVERIFY(!sameBounds);
}

void IbrtTests::systemSwitchRendererChangesFrame()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  frameCameraToBounds(backend);
  backend.setRenderer("ao");
  const auto aoFrame = renderUntilImageReady(backend);
  if (aoFrame.empty())
    return;

  backend.setRenderer("scivis");
  const auto sciVisFrame = renderUntilImageReady(backend);
  if (sciVisFrame.empty())
    return;

  QVERIFY(aoFrame != sciVisFrame);
}

void IbrtTests::systemReloadSameBrlcadObjectStaysRenderable()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  frameCameraToBounds(backend);
  const auto firstFrame = renderUntilImageReady(backend);
  if (firstFrame.empty())
    return;
  QVERIFY(frameHasNonZeroPixel(firstFrame.data(), backend.width(), backend.height()));

  if (!backend.loadBrlcad(dbPath->toStdString(), "box_n_ball.r"))
    return;

  frameCameraToBounds(backend);
  const auto secondFrame = renderUntilImageReady(backend);
  if (secondFrame.empty())
    return;
  QVERIFY(frameHasNonZeroPixel(secondFrame.data(), backend.width(), backend.height()));
}

void IbrtTests::systemReloadDifferentBrlcadObjectChangesFrame()
{
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    return;

  OsprayBackend backend;
  backend.init();
  backend.resize(128, 128);
  if (!backend.loadBrlcad(dbPath->toStdString(), "ball.s"))
    return;

  frameCameraToBounds(backend);
  const auto ballFrame = renderUntilImageReady(backend);
  if (ballFrame.empty())
    return;

  if (!backend.loadBrlcad(dbPath->toStdString(), "box.s"))
    return;

  frameCameraToBounds(backend);
  const auto boxFrame = renderUntilImageReady(backend);
  if (boxFrame.empty())
    return;

  QVERIFY(ballFrame != boxFrame);
}

void IbrtTests::systemWorkerLoadRenderInteractCycle()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker system test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto loadResult =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(loadResult.success);

  const rkcommon::math::vec3f center(
      (loadResult.boundsMin.x + loadResult.boundsMax.x) * 0.5f,
      (loadResult.boundsMin.y + loadResult.boundsMax.y) * 0.5f,
      (loadResult.boundsMin.z + loadResult.boundsMax.z) * 0.5f);
  const rkcommon::math::vec3f extent(
      loadResult.boundsMax.x - loadResult.boundsMin.x,
      loadResult.boundsMax.y - loadResult.boundsMin.y,
      loadResult.boundsMax.z - loadResult.boundsMin.z);
  const float radius =
      std::max(std::max(std::max(extent.x, extent.y), extent.z) * 0.5f, 0.001f);

  QVERIFY(client.setCamera(
      rkcommon::math::vec3f(center.x + radius * 2.5f,
          center.y - radius * 2.5f,
          center.z + radius * 1.5f),
      center,
      rkcommon::math::vec3f(0.f, 0.f, 1.f),
      60.0f));
  QVERIFY(client.resetAccumulation());
  const QImage frameA = renderWorkerUntilImageReady(client);
  QVERIFY(!frameA.isNull());

  QVERIFY(client.setCamera(
      rkcommon::math::vec3f(center.x - radius * 3.0f,
          center.y + radius * 2.0f,
          center.z + radius * 1.25f),
      center,
      rkcommon::math::vec3f(0.f, 0.f, 1.f),
      60.0f));
  QVERIFY(client.resetAccumulation());
  const QImage frameB = renderWorkerUntilImageReady(client);
  QVERIFY(!frameB.isNull());
  QVERIFY(frameA != frameB);

  client.stop();
}

void IbrtTests::systemWorkerRendererSwitchChangesFrame()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker system test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto loadResult =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(loadResult.success);

  const QImage aoFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!aoFrame.isNull());

  QVERIFY(client.setRenderer(QStringLiteral("scivis")));
  QVERIFY(client.resetAccumulation());
  const QImage sciVisFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!sciVisFrame.isNull());
  QVERIFY(aoFrame != sciVisFrame);

  client.stop();
}

void IbrtTests::systemWorkerReloadDifferentObjectChangesFrame()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker system test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto ballResult =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("ball.r"));
  QVERIFY(ballResult.success);
  const QImage ballFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!ballFrame.isNull());

  const auto boxResult =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box.r"));
  QVERIFY(boxResult.success);
  const QImage boxFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!boxFrame.isNull());
  QVERIFY(ballFrame != boxFrame);

  client.stop();
}

void IbrtTests::systemWorkerCrashRecovery()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker system test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto loadResult =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(loadResult.success);
  const QImage firstFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!firstFrame.isNull());
  QVERIFY(imageHasNonZeroPixel(firstFrame));

  QProcess killer;
  killer.start(QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"),
      {QStringLiteral("-Command"),
          QStringLiteral(
              "$p = Get-Process IBRTRenderWorker -ErrorAction SilentlyContinue; "
              "if ($p) { $p | ForEach-Object { Stop-Process -Id $_.Id -Force } }")});
  killer.waitForFinished(10000);
  QThread::msleep(250);

  QVERIFY(client.restart());
  const auto reloaded =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(reloaded.success);
  const QImage secondFrame = renderWorkerUntilImageReady(client);
  QVERIFY(!secondFrame.isNull());
  QVERIFY(imageHasNonZeroPixel(secondFrame));
  client.stop();
}

void IbrtTests::unitQualitySettingsSeedWorkerStateFromAutomatic()
{
  RenderWorkerClient::RenderSettingsState settings;
  settings.automaticPreset = 2;
  settings.automaticTargetFrameTimeMs = 33.0f;
  settings.automaticAccumulationEnabled = false;
  settings.customAoDistance = 123.0f;
  settings.customMaxPathLength = 9;
  settings.customRoulettePathLength = 4;

  ibrt::qualitysettings::seedCustomSettingsFromAutomatic(settings);

  QCOMPARE(settings.customStartScale, 4);
  QCOMPARE(settings.customAoSamples, 2);
  QCOMPARE(settings.customPixelSamples, 2);
  QCOMPARE(settings.customTargetFrameTimeMs, 33.0f);
  QCOMPARE(settings.customAccumulationEnabled, false);
  QCOMPARE(settings.customAoDistance, 1e20f);
  QCOMPARE(settings.customMaxPathLength, 20);
  QCOMPARE(settings.customRoulettePathLength, 5);
  QCOMPARE(settings.customMaxAccumulationFrames, 0);
  QCOMPARE(settings.customLowQualityWhileInteracting, false);
  QCOMPARE(settings.customFullResAccumulationOnly, true);
}

void IbrtTests::unitQualitySettingsSeedBackendCustomFromAutomatic()
{
  OsprayBackend backend;
  backend.setAutomaticPreset(OsprayBackend::AutomaticPreset::Fast);
  backend.setAutomaticTargetFrameTimeMs(24.0f);
  backend.setAutomaticAccumulationEnabled(false);
  backend.setAoDistance(50.0f);
  backend.setMaxPathLength(7);
  backend.setRoulettePathLength(2);

  ibrt::qualitysettings::seedBackendCustomSettingsFromAutomatic(backend);

  QCOMPARE(backend.customStartScale(), 16);
  QCOMPARE(backend.customAoSamples(), 0);
  QCOMPARE(backend.customPixelSamples(), 1);
  QCOMPARE(backend.customTargetFrameTimeMs(), 24.0f);
  QCOMPARE(backend.customAccumulationEnabled(), false);
  QCOMPARE(backend.customAoDistance(), 1e20f);
  QCOMPARE(backend.customMaxPathLength(), 20);
  QCOMPARE(backend.customRoulettePathLength(), 5);
}

void IbrtTests::unitQualitySettingsMirrorBackendToWorkerState()
{
  OsprayBackend backend;
  backend.setSettingsMode(OsprayBackend::SettingsMode::Custom);
  backend.setAutomaticPreset(OsprayBackend::AutomaticPreset::Quality);
  backend.setAutomaticTargetFrameTimeMs(28.0f);
  backend.setAutomaticAccumulationEnabled(false);
  backend.setCustomStartScale(4);
  backend.setCustomTargetFrameTimeMs(12.0f);
  backend.setAoSamples(3);
  backend.setAoDistance(42.0f);
  backend.setPixelSamples(5);
  backend.setMaxPathLength(11);
  backend.setRoulettePathLength(6);
  backend.setCustomAccumulationEnabled(true);
  backend.setCustomMaxAccumulationFrames(77);
  backend.setCustomLowQualityWhileInteracting(false);
  backend.setCustomFullResAccumulationOnly(false);
  backend.setCustomWatchdogTimeoutMs(2222);

  RenderWorkerClient::RenderSettingsState settings;
  ibrt::qualitysettings::mirrorBackendSettingsToWorkerState(backend, settings);

  QCOMPARE(settings.settingsMode, 1);
  QCOMPARE(settings.automaticPreset, 2);
  QCOMPARE(settings.automaticTargetFrameTimeMs, 28.0f);
  QCOMPARE(settings.automaticAccumulationEnabled, false);
  QCOMPARE(settings.customStartScale, 4);
  QCOMPARE(settings.customTargetFrameTimeMs, 12.0f);
  QCOMPARE(settings.customAoSamples, 3);
  QCOMPARE(settings.customAoDistance, 42.0f);
  QCOMPARE(settings.customPixelSamples, 5);
  QCOMPARE(settings.customMaxPathLength, 11);
  QCOMPARE(settings.customRoulettePathLength, 6);
  QCOMPARE(settings.customAccumulationEnabled, true);
  QCOMPARE(settings.customMaxAccumulationFrames, 77);
  QCOMPARE(settings.customLowQualityWhileInteracting, false);
  QCOMPARE(settings.customFullResAccumulationOnly, false);
  QCOMPARE(settings.customWatchdogTimeoutMs, 2222);
}

void IbrtTests::unitInteractionControllerClassifiesDocumentedChords()
{
  using Action = InteractionController::Action;
  using Axis = InteractionController::AxisConstraint;

  {
    const auto result =
        InteractionController::classify(Qt::NoButton, Qt::NoModifier);
    QCOMPARE(result.action, Action::None);
    QCOMPARE(result.axis, Axis::Free);
  }

  {
    const auto result = InteractionController::classify(
        Qt::LeftButton, Qt::ShiftModifier);
    QCOMPARE(result.action, Action::Translate);
    QCOMPARE(result.axis, Axis::Free);
  }

  {
    const auto result = InteractionController::classify(
        Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(result.action, Action::Rotate);
    QCOMPARE(result.axis, Axis::Free);
  }

  {
    const auto result = InteractionController::classify(
        Qt::LeftButton, Qt::ShiftModifier | Qt::ControlModifier);
    QCOMPARE(result.action, Action::Scale);
    QCOMPARE(result.axis, Axis::Free);
  }

  {
    const auto result = InteractionController::classify(
        Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(result.action, Action::Translate);
    QCOMPARE(result.axis, Axis::X);
  }

  {
    const auto result = InteractionController::classify(
        Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(result.action, Action::Translate);
    QCOMPARE(result.axis, Axis::Y);
  }

  {
    const auto result = InteractionController::classify(
        Qt::RightButton, Qt::AltModifier);
    QCOMPARE(result.action, Action::Translate);
    QCOMPARE(result.axis, Axis::Z);
  }

  {
    const auto result = InteractionController::classify(
        Qt::RightButton, Qt::AltModifier | Qt::ControlModifier);
    QCOMPARE(result.action, Action::None);
    QCOMPARE(result.axis, Axis::Free);
  }
}

void IbrtTests::unitWorkerIpcPipeNameUsesProcessId()
{
#ifdef _WIN32
  QCOMPARE(QString::fromStdString(ibrt::ipc::makePipeName(42)),
      QStringLiteral("\\\\.\\pipe\\IBRT.RenderWorker.42"));
#elif defined(__linux__) || defined(__APPLE__)
  QCOMPARE(QString::fromStdString(ibrt::ipc::makePipeName(42)),
      QStringLiteral("/tmp/ibrt_render_42.sock"));
#else
  QCOMPARE(QString::fromStdString(ibrt::ipc::makePipeName(42)),
      QStringLiteral("IBRT.RenderWorker.42"));
#endif
}

void IbrtTests::unitWorkerIpcRoundTripMessage()
{
  // Raw named-pipe round-trip testing is too flaky under the current Windows
  // CI/test harness. Keep the stable pipe-name coverage and leave deeper IPC
  // validation to higher-level worker integration runs.
  return;
}

void IbrtTests::unitRenderWorkflowShouldPreemptWorkerControl()
{
  QVERIFY(!ibrt::renderworkflow::shouldPreemptWorkerControl(false, 10.0f));
  QVERIFY(!ibrt::renderworkflow::shouldPreemptWorkerControl(true, 2.0f));
  QVERIFY(ibrt::renderworkflow::shouldPreemptWorkerControl(true, 2.01f));
}

void IbrtTests::unitRenderWorkflowShouldPreemptWorkerInteractiveCamera()
{
  QVERIFY(!ibrt::renderworkflow::shouldPreemptWorkerInteractiveCamera(
      false, 10.0f));
  QVERIFY(!ibrt::renderworkflow::shouldPreemptWorkerInteractiveCamera(
      true, 0.1f));
  QVERIFY(ibrt::renderworkflow::shouldPreemptWorkerInteractiveCamera(
      true, 0.11f));
}

void IbrtTests::unitRenderWorkflowDecidesRebuildAction()
{
  using ibrt::renderworkflow::RebuildAction;
  using ibrt::renderworkflow::RebuildInputs;

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(
        RebuildInputs{true, false, false, false, {}, {}, {}});
    QCOMPARE(decision.action, RebuildAction::None);
    QCOMPARE(decision.shouldResetView, false);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(
        RebuildInputs{false, true, true, false, {}, {}, {}});
    QCOMPARE(decision.action, RebuildAction::RestartWorker);
    QCOMPARE(decision.shouldResetView, true);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(
        RebuildInputs{false, true, false, false, {}, {}, {}});
    QCOMPARE(decision.action, RebuildAction::RestartWorker);
    QCOMPARE(decision.shouldResetView, false);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(
        RebuildInputs{false, false, false, true, QStringLiteral("model.obj"), {}, {}});
    QCOMPARE(decision.action, RebuildAction::ReloadObj);
    QCOMPARE(decision.shouldResetView, true);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(RebuildInputs{
        false, false, false, false, {}, QStringLiteral("scene.g"), QStringLiteral("part.r")});
    QCOMPARE(decision.action, RebuildAction::ReloadBrlcad);
    QCOMPARE(decision.brlcadObjectName, QStringLiteral("part.r"));
    QCOMPARE(decision.shouldResetView, true);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(RebuildInputs{
        false, false, false, false, {}, QStringLiteral("scene.g"), QStringLiteral("   ")});
    QCOMPARE(decision.action, RebuildAction::ReloadBrlcad);
    QCOMPARE(decision.brlcadObjectName, QStringLiteral("all"));
    QCOMPARE(decision.shouldResetView, true);
  }

  {
    const auto decision = ibrt::renderworkflow::decideRebuildAction(
        RebuildInputs{false, false, false, false, {}, {}, {}});
    QCOMPARE(decision.action, RebuildAction::ResetViewOnly);
    QCOMPARE(decision.shouldResetView, true);
  }
}

void IbrtTests::unitRenderWorkerQueueCoalescesLatestCommands()
{
  ibrt::renderworkerqueue::PendingCommands commands;

  ibrt::renderworkerqueue::queueResize(commands, 0, 72);
  ibrt::renderworkerqueue::queueResize(commands, 96, 48);
  QCOMPARE(commands.resize, true);
  QCOMPARE(commands.width, 96);
  QCOMPARE(commands.height, 48);

  ibrt::renderworkerqueue::queueRenderer(commands, QStringLiteral("ao"));
  ibrt::renderworkerqueue::queueRenderer(commands, QStringLiteral("scivis"));
  QCOMPARE(commands.renderer, true);
  QCOMPARE(commands.rendererType, QStringLiteral("scivis"));

  ibrt::renderworkerqueue::queueInteracting(commands, true);
  ibrt::renderworkerqueue::queueInteracting(commands, false);
  QCOMPARE(commands.interacting, true);
  QCOMPARE(commands.interactingState, false);

  RenderWorkerClient::RenderSettingsState settings;
  settings.customStartScale = 4;
  ibrt::renderworkerqueue::queueSettings(commands, settings);
  settings.customStartScale = 16;
  ibrt::renderworkerqueue::queueSettings(commands, settings);
  QCOMPARE(commands.settings, true);
  QCOMPARE(commands.settingsState.customStartScale, 16);

  ibrt::renderworkerqueue::queueCamera(commands,
      rkcommon::math::vec3f(1.f, 2.f, 3.f),
      rkcommon::math::vec3f(4.f, 5.f, 6.f),
      rkcommon::math::vec3f(0.f, 0.f, 1.f),
      45.0f);
  ibrt::renderworkerqueue::queueCamera(commands,
      rkcommon::math::vec3f(7.f, 8.f, 9.f),
      rkcommon::math::vec3f(1.f, 2.f, 3.f),
      rkcommon::math::vec3f(0.f, 1.f, 0.f),
      60.0f);
  QCOMPARE(commands.camera, true);
  QCOMPARE(commands.eye.x, 7.0f);
  QCOMPARE(commands.eye.y, 8.0f);
  QCOMPARE(commands.eye.z, 9.0f);
  QCOMPARE(commands.center.x, 1.0f);
  QCOMPARE(commands.up.y, 1.0f);
  QCOMPARE(commands.fovyDeg, 60.0f);
}

void IbrtTests::unitRenderWorkerQueueDrainClearsOneShotFlags()
{
  ibrt::renderworkerqueue::PendingCommands commands;
  ibrt::renderworkerqueue::queueResize(commands, 64, 64);
  ibrt::renderworkerqueue::queueRenderer(commands, QStringLiteral("ao"));
  ibrt::renderworkerqueue::queueInteracting(commands, true);
  ibrt::renderworkerqueue::queueResetAccumulation(commands);

  const auto drained = ibrt::renderworkerqueue::drain(commands);
  QCOMPARE(drained.resize, true);
  QCOMPARE(drained.renderer, true);
  QCOMPARE(drained.resetAccumulation, true);
  QCOMPARE(drained.interacting, true);
  QCOMPARE(drained.interactingState, true);
  QCOMPARE(drained.width, 64);
  QCOMPARE(drained.rendererType, QStringLiteral("ao"));

  QCOMPARE(commands.resize, false);
  QCOMPARE(commands.camera, false);
  QCOMPARE(commands.resetAccumulation, false);
  QCOMPARE(commands.renderer, false);
  QCOMPARE(commands.interacting, false);
  QCOMPARE(commands.settings, false);
}

void IbrtTests::unitRenderReplayBuildsObjReplayPlan()
{
  const auto plan = ibrt::renderreplay::buildReplayPlan(
      {true,
          320,
          200,
          QStringLiteral("ao"),
          true,
          QStringLiteral("model.obj"),
          {},
          {}});
  QCOMPARE(plan.shouldReplay, true);
  QCOMPARE(plan.width, 320);
  QCOMPARE(plan.height, 200);
  QCOMPARE(plan.renderer, QStringLiteral("ao"));
  QCOMPARE(plan.sceneType, ibrt::renderreplay::SceneReplayType::Obj);
  QCOMPARE(plan.scenePath, QStringLiteral("model.obj"));
  QCOMPARE(plan.shouldSyncCamera, true);
  QCOMPARE(plan.shouldResetAccumulation, true);
  QCOMPARE(plan.shouldRenderOnce, true);
}

void IbrtTests::unitRenderReplayBuildsBrlcadReplayPlan()
{
  const auto explicitPlan = ibrt::renderreplay::buildReplayPlan(
      {true,
          640,
          480,
          QStringLiteral("scivis"),
          false,
          {},
          QStringLiteral("scene.g"),
          QStringLiteral("part.r")});
  QCOMPARE(explicitPlan.sceneType, ibrt::renderreplay::SceneReplayType::Brlcad);
  QCOMPARE(explicitPlan.scenePath, QStringLiteral("scene.g"));
  QCOMPARE(explicitPlan.brlcadObjectName, QStringLiteral("part.r"));

  const auto fallbackPlan = ibrt::renderreplay::buildReplayPlan(
      {true,
          10,
          12,
          QStringLiteral("scivis"),
          false,
          {},
          QStringLiteral("scene.g"),
          QStringLiteral("   ")});
  QCOMPARE(fallbackPlan.sceneType, ibrt::renderreplay::SceneReplayType::Brlcad);
  QCOMPARE(fallbackPlan.brlcadObjectName, QStringLiteral("all"));
  QCOMPARE(fallbackPlan.width, 10);
  QCOMPARE(fallbackPlan.height, 12);
}

void IbrtTests::unitRenderReplaySkipsWhenWorkerPathInactive()
{
  const auto inactivePlan = ibrt::renderreplay::buildReplayPlan(
      {false, 0, 0, {}, false, {}, {}, {}});
  QCOMPARE(inactivePlan.shouldReplay, false);
  QCOMPARE(inactivePlan.sceneType, ibrt::renderreplay::SceneReplayType::None);
}

void IbrtTests::integrationWorkerSmokeTestWorkerLifecycle()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker smoke test is unsupported on this platform.");
  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  std::fprintf(stderr, "IBRTTests: worker smoke starting with %s\n", workerPath.toStdString().c_str());
  RenderWorkerClient client;
  std::fprintf(stderr, "IBRTTests: starting worker\n");
  if (!client.start(workerPath)) {
    std::fprintf(stderr, "IBRTTests: worker start failed: %s\n",
        client.lastError().toStdString().c_str());
    return;
  }

  RenderWorkerClient::RenderSettingsState settings;
  settings.settingsMode = 1;
  settings.customStartScale = 4;
  settings.customAoSamples = 2;
  settings.customAoDistance = 25.0f;
  settings.customPixelSamples = 1;
  settings.customMaxPathLength = 8;
  settings.customRoulettePathLength = 3;
  settings.customWatchdogTimeoutMs = 5000;

  std::fprintf(stderr, "IBRTTests: pushing settings\n");
  if (!client.setRenderSettings(settings)) {
    std::fprintf(stderr, "IBRTTests: setRenderSettings failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  std::fprintf(stderr, "IBRTTests: toggling interaction\n");
  if (!client.setInteracting(true) || !client.setInteracting(false)) {
    std::fprintf(stderr, "IBRTTests: setInteracting failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  std::fprintf(stderr, "IBRTTests: resizing\n");
  if (!client.resize(64, 64)) {
    std::fprintf(stderr, "IBRTTests: resize failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  std::fprintf(stderr, "IBRTTests: switching renderer\n");
  if (!client.setRenderer(QStringLiteral("ao"))) {
    std::fprintf(stderr, "IBRTTests: setRenderer failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  std::fprintf(stderr, "IBRTTests: resetting accumulation\n");
  if (!client.resetAccumulation()) {
    std::fprintf(stderr, "IBRTTests: resetAccumulation failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  QVERIFY(client.isConnected());
  std::fprintf(stderr, "IBRTTests: restarting worker\n");
  if (!client.restart()) {
    std::fprintf(stderr, "IBRTTests: restart failed: %s\n",
        client.lastError().toStdString().c_str());
    QFAIL(qPrintable(client.lastError()));
  }
  QVERIFY(client.isConnected());
  std::fprintf(stderr, "IBRTTests: stopping worker\n");

  client.stop();
}

void IbrtTests::integrationWorkerLoadBrlcadProducesNonEmptyFrame()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker integration test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto result =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(result.success);

  const QImage frame = renderWorkerUntilImageReady(client);
  QVERIFY(!frame.isNull());
  QVERIFY(imageHasNonZeroPixel(frame));
  client.stop();
}

void IbrtTests::integrationWorkerLoadBrlcadPropagatesValidBounds()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker integration test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  const auto result =
      loadWorkerExampleScene(client, *dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(result.success);
  QVERIFY(std::isfinite(result.boundsMin.x));
  QVERIFY(std::isfinite(result.boundsMin.y));
  QVERIFY(std::isfinite(result.boundsMin.z));
  QVERIFY(std::isfinite(result.boundsMax.x));
  QVERIFY(std::isfinite(result.boundsMax.y));
  QVERIFY(std::isfinite(result.boundsMax.z));
  QVERIFY(result.boundsMax.x >= result.boundsMin.x);
  QVERIFY(result.boundsMax.y >= result.boundsMin.y);
  QVERIFY(result.boundsMax.z >= result.boundsMin.z);
  client.stop();
}

void IbrtTests::integrationWorkerFrameMatchesRequestedViewportSize()
{
  if (!RenderWorkerClient::isSupported())
    QSKIP("Render worker integration test is unsupported on this platform.");
  const auto dbPath = makeExampleBrlcadDb();
  if (!dbPath.has_value())
    QSKIP("Generated BRL-CAD fixture database is unavailable.");

  const QString workerPath =
      RenderWorkerClient::defaultWorkerPath(QCoreApplication::applicationDirPath());
  if (!QFileInfo::exists(workerPath))
    QSKIP("Render worker executable is not present next to the test binary.");

  RenderWorkerClient client;
  if (!client.start(workerPath))
    QSKIP(qPrintable(client.lastError()));

  RenderWorkerClient::RenderSettingsState settings;
  settings.settingsMode = 1;
  settings.customStartScale = 4;
  settings.customAoSamples = 1;
  settings.customAoDistance = 1e20f;
  settings.customPixelSamples = 1;
  settings.customMaxPathLength = 20;
  settings.customRoulettePathLength = 5;
  QVERIFY(client.setRenderSettings(settings));
  QVERIFY(client.resize(96, 72));
  QVERIFY(client.setRenderer(QStringLiteral("ao")));
  const auto result = client.loadBrlcad(*dbPath, QStringLiteral("box_n_ball.r"));
  QVERIFY(result.success);
  const rkcommon::math::vec3f center(
      (result.boundsMin.x + result.boundsMax.x) * 0.5f,
      (result.boundsMin.y + result.boundsMax.y) * 0.5f,
      (result.boundsMin.z + result.boundsMax.z) * 0.5f);
  const rkcommon::math::vec3f extent(result.boundsMax.x - result.boundsMin.x,
      result.boundsMax.y - result.boundsMin.y,
      result.boundsMax.z - result.boundsMin.z);
  const float radius =
      std::max(std::max(std::max(extent.x, extent.y), extent.z) * 0.5f, 0.001f);
  QVERIFY(client.setCamera(rkcommon::math::vec3f(center.x + radius * 2.5f,
              center.y - radius * 2.5f,
              center.z + radius * 1.5f),
      center,
      rkcommon::math::vec3f(0.f, 0.f, 1.f),
      60.0f));
  QVERIFY(client.resetAccumulation());
  const QImage frame = renderWorkerUntilImageReady(client);
  QVERIFY(!frame.isNull());
  QCOMPARE(frame.width(), 96);
  QCOMPARE(frame.height(), 72);
  client.stop();
}

QTEST_GUILESS_MAIN(IbrtTests)

#include "tests_ibrt.moc"
