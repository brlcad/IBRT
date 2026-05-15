// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QImage>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <mutex>

#include <ospray/ospray_cpp/ext/rkcommon.h>

#ifdef _WIN32
#include <windows.h>
#endif

class RenderWorkerClient : public QObject
{
  Q_OBJECT

 public:
  struct FrameResult
  {
    QImage image;
    float frameTimeMs = 0.0f;
    bool updated = false;
    float renderFPS = 0.0f;
    int currentScale = 1;
    uint64_t accumulatedFrames = 0;
    uint64_t watchdogCancels = 0;
    uint64_t aoAutoReductions = 0;
    QString renderer;
  };

  struct SceneLoadResult
  {
    bool success = false;
    QString errorMessage;
    rkcommon::math::vec3f boundsMin{0.f, 0.f, 0.f};
    rkcommon::math::vec3f boundsMax{0.f, 0.f, 0.f};
  };

  struct RenderSettingsState
  {
    int settingsMode = 0;
    int automaticPreset = 1;
    float automaticTargetFrameTimeMs = 16.0f;
    bool automaticAccumulationEnabled = true;
    int customStartScale = 8;
    float customTargetFrameTimeMs = 16.0f;
    int customAoSamples = 1;
    float customAoDistance = 1e20f;
    int customPixelSamples = 1;
    int customMaxPathLength = 20;
    int customRoulettePathLength = 5;
    bool customAccumulationEnabled = true;
    int customMaxAccumulationFrames = 0;
    bool customLowQualityWhileInteracting = false;
    bool customFullResAccumulationOnly = true;
    int customWatchdogTimeoutMs = 1500;
  };

  explicit RenderWorkerClient(QObject *parent = nullptr);
  ~RenderWorkerClient() override;

  static bool isSupported();
  static QString executableFileName();
  static QString defaultWorkerPath(const QString &applicationDirPath);

  bool start(const QString &workerPath);
  void stop();
  bool isConnected() const;
  QString lastError() const;
  QStringList listBrlcadObjects(const QString &path);
  SceneLoadResult loadObj(const QString &path);
  SceneLoadResult loadBrlcad(const QString &path, const QString &objectName);
  bool resize(int width, int height);
  bool setCamera(const rkcommon::math::vec3f &eye,
      const rkcommon::math::vec3f &center,
      const rkcommon::math::vec3f &up,
      float fovyDeg);
  bool resetAccumulation();
  bool setRenderer(const QString &rendererType);
  bool setRenderSettings(const RenderSettingsState &settings);
  bool setInteracting(bool interacting);
  FrameResult requestFrame();
  bool restart();

 signals:
  void workerConnectionChanged(bool connected);

 private:
#ifdef _WIN32
  bool connectPipe();
  bool sendPing();
  bool sendRequest(uint32_t type, const QString &payload, QString *responsePayload);
  bool sendRequestBytes(uint32_t type,
      const std::string &payload,
      std::string *responsePayload);
  void closePipe();
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
#elif defined(__linux__) || defined(__APPLE__)
  bool connectSocket();
  bool sendPing();
  bool sendRequest(uint32_t type, const QString &payload, QString *responsePayload);
  bool sendRequestBytes(uint32_t type,
      const std::string &payload,
      std::string *responsePayload);
  void closeSocket();
  int socket_ = -1;
#endif
  QProcess *process_ = nullptr;
  QString workerPath_;
  QString pipeName_;
  QString lastError_;
  uint64_t nextRequestId_ = 2;
  bool connected_ = false;
  bool stopInProgress_ = false;
  mutable std::mutex requestMutex_;

  void setConnected(bool connected);
};
