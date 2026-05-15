// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ibrt::ipc {

enum class MessageType : uint32_t
{
  Ping = 1,
  Pong = 2,
  Shutdown = 3,
  ListBrlcadObjects = 4,
  BrlcadObjectList = 5,
  LoadObj = 6,
  LoadBrlcad = 7,
  LoadResult = 8,
  Error = 9,
  Resize = 10,
  SetCamera = 11,
  ResetAccumulation = 12,
  RequestFrame = 13,
  FrameData = 14,
  SetRenderer = 15,
  SetRenderSettings = 16,
  SetInteracting = 17
};

struct MessageHeader
{
  uint32_t magic = 0x54425249;
  uint32_t version = 1;
  uint32_t type = 0;
  uint64_t requestId = 0;
  uint32_t payloadSize = 0;
};

struct Message
{
  MessageType type = MessageType::Error;
  uint64_t requestId = 0;
  std::string payload;
};

std::string makePipeName(uint32_t processId);

#ifdef _WIN32
bool writeMessage(HANDLE pipe, const Message &message);
bool readMessage(HANDLE pipe, Message &message);
#elif defined(__linux__) || defined(__APPLE__)
bool writeMessage(int fd, const Message &message);
bool readMessage(int fd, Message &message);
#endif

} // namespace ibrt::ipc
