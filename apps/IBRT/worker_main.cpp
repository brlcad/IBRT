// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <ospray/ospray.h>

#include "ospraybackend.h"
#include "worker_ipc.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

static void osprayErrorCallback(void *, OSPError error, const char *message)
{
  fprintf(stderr,
      "IBRT Worker OSPRAY ERROR %d: %s\n",
      static_cast<int>(error),
      message ? message : "(null)");
  fflush(stderr);
}

static void osprayStatusCallback(void *, const char *message)
{
  (void)message;
}

std::string joinLines(const std::vector<std::string> &values)
{
  std::string out;
  for (size_t i = 0; i < values.size(); ++i) {
    out += values[i];
    if (i + 1 < values.size())
      out.push_back('\n');
  }
  return out;
}

std::string makeLoadResultPayload(
    bool success, const OsprayBackend &backend, const std::string &errorMessage)
{
  struct LoadResultPayload
  {
    uint32_t success;
    float boundsMin[3];
    float boundsMax[3];
    uint32_t errorSize;
  } header{success ? 1u : 0u,
      {backend.getBoundsMin().x, backend.getBoundsMin().y, backend.getBoundsMin().z},
      {backend.getBoundsMax().x, backend.getBoundsMax().y, backend.getBoundsMax().z},
      static_cast<uint32_t>(errorMessage.size())};

  std::string payload(sizeof(header), '\0');
  std::memcpy(payload.data(), &header, sizeof(header));
  if (!errorMessage.empty())
    payload += errorMessage;
  return payload;
}

template <typename T>
bool readPodPayload(const std::string &payload, T &out)
{
  if (payload.size() != sizeof(T))
    return false;
  std::memcpy(&out, payload.data(), sizeof(T));
  return true;
}

} // namespace

int main(int argc, char *argv[])
{
#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)
  fprintf(stderr, "IBRT render worker currently supports Windows only.\n");
  return 1;
#elif defined(__linux__) || defined(__APPLE__)
  std::string pipeName;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--pipe") {
      pipeName = argv[i + 1];
      break;
    }
  }

  if (pipeName.empty()) {
    fprintf(stderr, "IBRT render worker missing --pipe argument.\n");
    return 1;
  }

  ::unlink(pipeName.c_str());

  int serverFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (serverFd == -1) {
    fprintf(stderr, "IBRT render worker failed to create socket.\n");
    return 1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, pipeName.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(serverFd);
    fprintf(stderr, "IBRT render worker failed to bind socket.\n");
    return 1;
  }

  if (::listen(serverFd, 1) != 0) {
    ::close(serverFd);
    ::unlink(pipeName.c_str());
    fprintf(stderr, "IBRT render worker failed to listen on socket.\n");
    return 1;
  }

  int socketDescriptor = ::accept(serverFd, nullptr, nullptr);
  ::close(serverFd);
  if (socketDescriptor == -1) {
    ::unlink(pipeName.c_str());
    fprintf(stderr, "IBRT render worker failed to accept socket connection.\n");
    return 1;
  }

  int ac = argc;
  const char **av = const_cast<const char **>(argv);
  if (ospInit(&ac, av) != OSP_NO_ERROR) {
    ibrt::ipc::writeMessage(socketDescriptor,
        {ibrt::ipc::MessageType::Error, 0, "OSPRay initialization failed."});
    ::close(socketDescriptor);
    ::unlink(pipeName.c_str());
    return 1;
  }

  OSPDevice device = ospNewDevice("cpu");
  if (!device) {
    ibrt::ipc::writeMessage(socketDescriptor,
        {ibrt::ipc::MessageType::Error, 0, "Failed to create OSPRay CPU device."});
    ospShutdown();
    ::close(socketDescriptor);
    ::unlink(pipeName.c_str());
    return 1;
  }

  ospSetCurrentDevice(device);
  ospDeviceSetErrorCallback(device, osprayErrorCallback, nullptr);
  ospDeviceSetStatusCallback(device, osprayStatusCallback, nullptr);
  ospCommit(reinterpret_cast<OSPObject>(device));
  ospLoadModule("cpu");

  OsprayBackend backend;
  backend.init();
  backend.resize(1, 1);

  bool running = true;
  while (running) {
    ibrt::ipc::Message message;
    if (!ibrt::ipc::readMessage(socketDescriptor, message))
      break;

    switch (message.type) {
    case ibrt::ipc::MessageType::Ping:
      ibrt::ipc::writeMessage(
          socketDescriptor, {ibrt::ipc::MessageType::Pong, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::Shutdown:
      running = false;
      break;

    case ibrt::ipc::MessageType::ListBrlcadObjects: {
      const auto names = backend.listBrlcadObjects(message.payload);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::BrlcadObjectList,
              message.requestId,
              joinLines(names)});
      break;
    }

    case ibrt::ipc::MessageType::LoadObj: {
      const bool ok = backend.loadObj(message.payload);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult,
              message.requestId,
              makeLoadResultPayload(ok, backend, ok ? std::string() : backend.lastError())});
      break;
    }

    case ibrt::ipc::MessageType::LoadBrlcad: {
      const size_t sep = message.payload.find('\n');
      const std::string path = sep == std::string::npos
          ? message.payload
          : message.payload.substr(0, sep);
      const std::string object =
          sep == std::string::npos ? std::string() : message.payload.substr(sep + 1);
      const bool ok = backend.loadBrlcad(path, object);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult,
              message.requestId,
              makeLoadResultPayload(ok, backend, ok ? std::string() : backend.lastError())});
      break;
    }

    case ibrt::ipc::MessageType::Resize: {
      struct ResizePayload
      {
        int32_t width;
        int32_t height;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(socketDescriptor,
            {ibrt::ipc::MessageType::Error, message.requestId, "Invalid resize payload."});
        break;
      }
      backend.resize(payload.width, payload.height);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::SetCamera: {
      struct CameraPayload
      {
        float eye[3];
        float center[3];
        float up[3];
        float fovyDeg;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(socketDescriptor,
            {ibrt::ipc::MessageType::Error, message.requestId, "Invalid camera payload."});
        break;
      }

      backend.setCamera(rkcommon::math::vec3f(payload.eye[0], payload.eye[1], payload.eye[2]),
          rkcommon::math::vec3f(
              payload.center[0], payload.center[1], payload.center[2]),
          rkcommon::math::vec3f(payload.up[0], payload.up[1], payload.up[2]),
          payload.fovyDeg);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::ResetAccumulation:
      backend.resetAccumulation();
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::RequestFrame: {
      const bool updated = backend.advanceRender();
      struct FrameHeader
      {
        uint32_t width;
        uint32_t height;
        float frameTimeMs;
        float renderFPS;
        uint32_t updated;
        uint32_t currentScale;
        uint64_t accumulatedFrames;
        uint64_t watchdogCancels;
        uint64_t aoAutoReductions;
        uint32_t rendererNameSize;
      } header{static_cast<uint32_t>(backend.width()),
          static_cast<uint32_t>(backend.height()),
          backend.lastFrameTimeMs(),
          backend.renderFPS(),
          updated ? 1u : 0u,
          static_cast<uint32_t>(backend.currentScale()),
          backend.accumulatedFrames(),
          backend.watchdogCancelCount(),
          backend.aoAutoReductionCount(),
          static_cast<uint32_t>(backend.currentRenderer().size())};

      std::string payload(sizeof(FrameHeader), '\0');
      std::memcpy(payload.data(), &header, sizeof(FrameHeader));

      const uint32_t *pixels = backend.pixels();
      const size_t pixelBytes = size_t(backend.width()) * size_t(backend.height()) * 4;
      if (pixels && pixelBytes > 0) {
        const size_t base = payload.size();
        payload.resize(base + pixelBytes);
        std::memcpy(payload.data() + base, pixels, pixelBytes);
      }
      payload += backend.currentRenderer();

      ibrt::ipc::writeMessage(
          socketDescriptor, {ibrt::ipc::MessageType::FrameData, message.requestId, payload});
      break;
    }

    case ibrt::ipc::MessageType::SetRenderer:
      backend.setRenderer(message.payload);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::SetRenderSettings: {
      struct SettingsPayload
      {
        int32_t settingsMode;
        int32_t automaticPreset;
        float automaticTargetFrameTimeMs;
        uint32_t automaticAccumulationEnabled;
        int32_t customStartScale;
        float customTargetFrameTimeMs;
        int32_t customAoSamples;
        float customAoDistance;
        int32_t customPixelSamples;
        int32_t customMaxPathLength;
        int32_t customRoulettePathLength;
        uint32_t customAccumulationEnabled;
        int32_t customMaxAccumulationFrames;
        uint32_t customLowQualityWhileInteracting;
        uint32_t customFullResAccumulationOnly;
        int32_t customWatchdogTimeoutMs;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(socketDescriptor,
            {ibrt::ipc::MessageType::Error,
                message.requestId,
                "Invalid render settings payload."});
        break;
      }

      backend.setSettingsMode(payload.settingsMode == 0
              ? OsprayBackend::SettingsMode::Automatic
              : OsprayBackend::SettingsMode::Custom);
      backend.setAutomaticPreset(payload.automaticPreset == 0
              ? OsprayBackend::AutomaticPreset::Fast
              : (payload.automaticPreset == 1 ? OsprayBackend::AutomaticPreset::Balanced
                                              : OsprayBackend::AutomaticPreset::Quality));
      backend.setAutomaticTargetFrameTimeMs(payload.automaticTargetFrameTimeMs);
      backend.setAutomaticAccumulationEnabled(payload.automaticAccumulationEnabled != 0);
      backend.setCustomStartScale(payload.customStartScale);
      backend.setCustomTargetFrameTimeMs(payload.customTargetFrameTimeMs);
      backend.setAoSamples(payload.customAoSamples);
      backend.setAoDistance(payload.customAoDistance);
      backend.setPixelSamples(payload.customPixelSamples);
      backend.setMaxPathLength(payload.customMaxPathLength);
      backend.setRoulettePathLength(payload.customRoulettePathLength);
      backend.setCustomAccumulationEnabled(payload.customAccumulationEnabled != 0);
      backend.setCustomMaxAccumulationFrames(payload.customMaxAccumulationFrames);
      backend.setCustomLowQualityWhileInteracting(
          payload.customLowQualityWhileInteracting != 0);
      backend.setCustomFullResAccumulationOnly(
          payload.customFullResAccumulationOnly != 0);
      backend.setCustomWatchdogTimeoutMs(payload.customWatchdogTimeoutMs);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::SetInteracting:
      if (message.payload.size() != 1) {
        ibrt::ipc::writeMessage(socketDescriptor,
            {ibrt::ipc::MessageType::Error,
                message.requestId,
                "Invalid interacting payload."});
        break;
      }
      backend.setInteracting(message.payload[0] != 0);
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    default:
      ibrt::ipc::writeMessage(socketDescriptor,
          {ibrt::ipc::MessageType::Error,
              message.requestId,
              "Unsupported message type."});
      break;
    }
  }

  ospShutdown();
  ::close(socketDescriptor);
  ::unlink(pipeName.c_str());
  return 0;
#else
  std::string pipeName;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--pipe") {
      pipeName = argv[i + 1];
      break;
    }
  }

  if (pipeName.empty()) {
    fprintf(stderr, "IBRT render worker missing --pipe argument.\n");
    return 1;
  }

  HANDLE pipe = CreateNamedPipeA(pipeName.c_str(),
      PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      64 * 1024,
      64 * 1024,
      0,
      nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "IBRT render worker failed to create named pipe.\n");
    return 1;
  }

  if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    fprintf(stderr, "IBRT render worker failed to accept pipe connection.\n");
    return 1;
  }

  int ac = argc;
  const char **av = const_cast<const char **>(argv);
  if (ospInit(&ac, av) != OSP_NO_ERROR) {
    ibrt::ipc::writeMessage(
        pipe, {ibrt::ipc::MessageType::Error, 0, "OSPRay initialization failed."});
    CloseHandle(pipe);
    return 1;
  }

  OSPDevice device = ospNewDevice("cpu");
  if (!device) {
    ibrt::ipc::writeMessage(
        pipe, {ibrt::ipc::MessageType::Error, 0, "Failed to create OSPRay CPU device."});
    ospShutdown();
    CloseHandle(pipe);
    return 1;
  }

  ospSetCurrentDevice(device);
  ospDeviceSetErrorCallback(device, osprayErrorCallback, nullptr);
  ospDeviceSetStatusCallback(device, osprayStatusCallback, nullptr);
  ospCommit(reinterpret_cast<OSPObject>(device));
  ospLoadModule("cpu");

  OsprayBackend backend;
  backend.init();
  backend.resize(1, 1);

  bool running = true;
  while (running) {
    ibrt::ipc::Message message;
    if (!ibrt::ipc::readMessage(pipe, message))
      break;

    switch (message.type) {
    case ibrt::ipc::MessageType::Ping:
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::Pong, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::Shutdown:
      running = false;
      break;

    case ibrt::ipc::MessageType::ListBrlcadObjects: {
      const auto names = backend.listBrlcadObjects(message.payload);
      ibrt::ipc::writeMessage(pipe,
          {ibrt::ipc::MessageType::BrlcadObjectList,
              message.requestId,
              joinLines(names)});
      break;
    }

    case ibrt::ipc::MessageType::LoadObj: {
      const bool ok = backend.loadObj(message.payload);
      ibrt::ipc::writeMessage(pipe,
          {ibrt::ipc::MessageType::LoadResult,
              message.requestId,
              makeLoadResultPayload(ok, backend, ok ? std::string() : backend.lastError())});
      break;
    }

    case ibrt::ipc::MessageType::LoadBrlcad: {
      const size_t sep = message.payload.find('\n');
      const std::string path = sep == std::string::npos
          ? message.payload
          : message.payload.substr(0, sep);
      const std::string object =
          sep == std::string::npos ? std::string() : message.payload.substr(sep + 1);
      const bool ok = backend.loadBrlcad(path, object);
      ibrt::ipc::writeMessage(pipe,
          {ibrt::ipc::MessageType::LoadResult,
              message.requestId,
              makeLoadResultPayload(ok, backend, ok ? std::string() : backend.lastError())});
      break;
    }

    case ibrt::ipc::MessageType::Resize: {
      struct ResizePayload
      {
        int32_t width;
        int32_t height;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(pipe,
            {ibrt::ipc::MessageType::Error, message.requestId, "Invalid resize payload."});
        break;
      }
      backend.resize(payload.width, payload.height);
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::SetCamera: {
      struct CameraPayload
      {
        float eye[3];
        float center[3];
        float up[3];
        float fovyDeg;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(pipe,
            {ibrt::ipc::MessageType::Error, message.requestId, "Invalid camera payload."});
        break;
      }

      backend.setCamera(rkcommon::math::vec3f(payload.eye[0], payload.eye[1], payload.eye[2]),
          rkcommon::math::vec3f(
              payload.center[0], payload.center[1], payload.center[2]),
          rkcommon::math::vec3f(payload.up[0], payload.up[1], payload.up[2]),
          payload.fovyDeg);
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::ResetAccumulation:
      backend.resetAccumulation();
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::RequestFrame: {
      const bool updated = backend.advanceRender();
      struct FrameHeader
      {
        uint32_t width;
        uint32_t height;
        float frameTimeMs;
        float renderFPS;
        uint32_t updated;
        uint32_t currentScale;
        uint64_t accumulatedFrames;
        uint64_t watchdogCancels;
        uint64_t aoAutoReductions;
        uint32_t rendererNameSize;
      } header{static_cast<uint32_t>(backend.width()),
          static_cast<uint32_t>(backend.height()),
          backend.lastFrameTimeMs(),
          backend.renderFPS(),
          updated ? 1u : 0u,
          static_cast<uint32_t>(backend.currentScale()),
          backend.accumulatedFrames(),
          backend.watchdogCancelCount(),
          backend.aoAutoReductionCount(),
          static_cast<uint32_t>(backend.currentRenderer().size())};

      std::string payload(sizeof(FrameHeader), '\0');
      std::memcpy(payload.data(), &header, sizeof(FrameHeader));

      const uint32_t *pixels = backend.pixels();
      const size_t pixelBytes = size_t(backend.width()) * size_t(backend.height()) * 4;
      if (pixels && pixelBytes > 0) {
        const size_t base = payload.size();
        payload.resize(base + pixelBytes);
        std::memcpy(payload.data() + base, pixels, pixelBytes);
      }
      payload += backend.currentRenderer();

      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::FrameData, message.requestId, payload});
      break;
    }

    case ibrt::ipc::MessageType::SetRenderer:
      backend.setRenderer(message.payload);
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    case ibrt::ipc::MessageType::SetRenderSettings: {
      struct SettingsPayload
      {
        int32_t settingsMode;
        int32_t automaticPreset;
        float automaticTargetFrameTimeMs;
        uint32_t automaticAccumulationEnabled;
        int32_t customStartScale;
        float customTargetFrameTimeMs;
        int32_t customAoSamples;
        float customAoDistance;
        int32_t customPixelSamples;
        int32_t customMaxPathLength;
        int32_t customRoulettePathLength;
        uint32_t customAccumulationEnabled;
        int32_t customMaxAccumulationFrames;
        uint32_t customLowQualityWhileInteracting;
        uint32_t customFullResAccumulationOnly;
        int32_t customWatchdogTimeoutMs;
      } payload{};
      if (!readPodPayload(message.payload, payload)) {
        ibrt::ipc::writeMessage(pipe,
            {ibrt::ipc::MessageType::Error,
                message.requestId,
                "Invalid render settings payload."});
        break;
      }

      backend.setSettingsMode(payload.settingsMode == 0
              ? OsprayBackend::SettingsMode::Automatic
              : OsprayBackend::SettingsMode::Custom);
      backend.setAutomaticPreset(payload.automaticPreset == 0
              ? OsprayBackend::AutomaticPreset::Fast
              : (payload.automaticPreset == 1 ? OsprayBackend::AutomaticPreset::Balanced
                                              : OsprayBackend::AutomaticPreset::Quality));
      backend.setAutomaticTargetFrameTimeMs(payload.automaticTargetFrameTimeMs);
      backend.setAutomaticAccumulationEnabled(payload.automaticAccumulationEnabled != 0);
      backend.setCustomStartScale(payload.customStartScale);
      backend.setCustomTargetFrameTimeMs(payload.customTargetFrameTimeMs);
      backend.setAoSamples(payload.customAoSamples);
      backend.setAoDistance(payload.customAoDistance);
      backend.setPixelSamples(payload.customPixelSamples);
      backend.setMaxPathLength(payload.customMaxPathLength);
      backend.setRoulettePathLength(payload.customRoulettePathLength);
      backend.setCustomAccumulationEnabled(payload.customAccumulationEnabled != 0);
      backend.setCustomMaxAccumulationFrames(payload.customMaxAccumulationFrames);
      backend.setCustomLowQualityWhileInteracting(
          payload.customLowQualityWhileInteracting != 0);
      backend.setCustomFullResAccumulationOnly(
          payload.customFullResAccumulationOnly != 0);
      backend.setCustomWatchdogTimeoutMs(payload.customWatchdogTimeoutMs);
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;
    }

    case ibrt::ipc::MessageType::SetInteracting:
      if (message.payload.size() != 1) {
        ibrt::ipc::writeMessage(pipe,
            {ibrt::ipc::MessageType::Error,
                message.requestId,
                "Invalid interacting payload."});
        break;
      }
      backend.setInteracting(message.payload[0] != 0);
      ibrt::ipc::writeMessage(
          pipe, {ibrt::ipc::MessageType::LoadResult, message.requestId, std::string()});
      break;

    default:
      ibrt::ipc::writeMessage(pipe,
          {ibrt::ipc::MessageType::Error,
              message.requestId,
              "Unsupported message type."});
      break;
    }
  }

  ospShutdown();
  FlushFileBuffers(pipe);
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);
  return 0;
#endif
}
