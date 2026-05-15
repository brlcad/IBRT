// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <ospray/ospray.h>

#include "mainwindow.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <fcntl.h>
#include <io.h>
#endif

#ifdef _WIN32
// A few BRL-CAD diagnostics are known-noisy during startup; route stderr through
// a filter so expected warnings do not swamp actionable errors.
static bool shouldSuppressStderrLine(const std::string &line)
{
  return line.find(
             "getCurveEstimateOfV(): estimate of 'v' given a trim curve and 'u' did not converge")
      != std::string::npos;
}

// Redirects stderr through a background pipe reader so selected lines can be filtered.
static void installStderrFilter()
{
  HANDLE originalStderr = GetStdHandle(STD_ERROR_HANDLE);
  if (!originalStderr || originalStderr == INVALID_HANDLE_VALUE)
    return;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    return;

  const int stderrFd = _fileno(stderr);
  if (stderrFd < 0) {
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return;
  }

  const int pipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(writePipe), _O_TEXT);
  if (pipeFd < 0) {
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return;
  }

  if (_dup2(pipeFd, stderrFd) != 0) {
    _close(pipeFd);
    CloseHandle(readPipe);
    return;
  }

  setvbuf(stderr, nullptr, _IONBF, 0);
  SetStdHandle(STD_ERROR_HANDLE, writePipe);
  _close(pipeFd);

  std::thread([readPipe, originalStderr]() {
    char buffer[1024];
    std::string pending;

    auto flushLine = [&](const std::string &line) {
      if (shouldSuppressStderrLine(line))
        return;

      DWORD written = 0;
      WriteFile(
          originalStderr, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    };

    for (;;) {
      DWORD bytesRead = 0;
      if (!ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr)
          || bytesRead == 0) {
        break;
      }

      pending.append(buffer, buffer + bytesRead);

      size_t newline = 0;
      while ((newline = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newline + 1);
        pending.erase(0, newline + 1);
        flushLine(line);
      }
    }

    if (!pending.empty())
      flushLine(pending);

    CloseHandle(readPipe);
  }).detach();
}

static LONG WINAPI crashDumpExceptionFilter(EXCEPTION_POINTERS *exceptionInfo)
{
  // Write crash dumps next to the executable so field failures can be inspected
  // without requiring a debugger to be attached.
  char exePath[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exePath, MAX_PATH);

  char dirPath[MAX_PATH] = {};
  std::strncpy(dirPath, exePath, MAX_PATH - 1);
  for (int i = int(std::strlen(dirPath)) - 1; i >= 0; --i) {
    if (dirPath[i] == '\\' || dirPath[i] == '/') {
      dirPath[i] = '\0';
      break;
    }
  }

  SYSTEMTIME st{};
  GetLocalTime(&st);

  char dumpPath[MAX_PATH] = {};
  std::snprintf(dumpPath,
      MAX_PATH,
      "%s\\IBRT_crash_%04u%02u%02u_%02u%02u%02u.dmp",
      dirPath,
      st.wYear,
      st.wMonth,
      st.wDay,
      st.wHour,
      st.wMinute,
      st.wSecond);

  HANDLE hFile = CreateFileA(
      dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ExceptionPointers = exceptionInfo;
    dumpInfo.ClientPointers = FALSE;

    MiniDumpWriteDump(GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithThreadInfo
            | MiniDumpWithIndirectlyReferencedMemory),
        &dumpInfo,
        nullptr,
        nullptr);
    CloseHandle(hFile);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

// Installs the crash-dump writer as the process-wide unhandled exception filter.
static void installCrashDumpHandler()
{
  SetUnhandledExceptionFilter(crashDumpExceptionFilter);
}
#endif

// Requests the BRL-CAD module during OSPRay startup so child worker processes
// inherit the same module set and BRL-CAD geometry is registered before use.
static void ensureOsprayLoadModule(const char *moduleName)
{
  const QByteArray module(moduleName);
  QByteArray modules = qgetenv("OSPRAY_LOAD_MODULES");
  const QList<QByteArray> existing = modules.split(',');
  for (const QByteArray &entry : existing) {
    if (entry.trimmed() == module)
      return;
  }

  if (!modules.isEmpty() && !modules.endsWith(','))
    modules.append(',');
  modules.append(module);
  qputenv("OSPRAY_LOAD_MODULES", modules);
}

// Forwards OSPRay errors to stderr using the application's logging format.
static void osprayErrorCallback(void *, OSPError error, const char *message)
{
  fprintf(stderr,
      "OSPRAY ERROR %d: %s\n",  
      (int)error,
      message ? message : "(null)");
  fflush(stderr);
}

// Ignores OSPRay status chatter to keep startup output quiet.
static void osprayStatusCallback(void *, const char *message)
{
  (void)message;
}

// Shows a blocking startup error dialog on Windows or prints to stderr elsewhere.
static void showFatalStartupError(const char *message)
{
#ifdef _WIN32
  MessageBoxA(nullptr, message, "IBRT Startup Error", MB_ICONERROR | MB_OK);
#else
  fprintf(stderr, "%s\n", message ? message : "Unknown startup error.");
#endif
}

// Initializes OSPRay, starts Qt, and runs the main viewer window.
int main(int argc, char *argv[])
{
#ifdef _WIN32
  // Install platform-specific diagnostics before OSPRay or Qt start doing work.
  installStderrFilter();
  installCrashDumpHandler();
#endif

  ensureOsprayLoadModule("brl_cad");

  // NOTE; have to manually initialize OpenGL on Mac or it'll default
  // to 2.1 and reduced features.  Qt supports metal, so we should get
  // that working instead, but will require coordination with imgui.
#ifdef __APPLE__
  QSurfaceFormat glFormat;
  glFormat.setRenderableType(QSurfaceFormat::OpenGL);
  glFormat.setVersion(4, 1);
  glFormat.setProfile(QSurfaceFormat::CoreProfile);
  glFormat.setDepthBufferSize(24);
  glFormat.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(glFormat);
#endif

  int ac = argc;
  const char **av = const_cast<const char **>(argv);

  const OSPError err = ospInit(&ac, av);
  if (err != OSP_NO_ERROR) {
    fprintf(stderr, "IBRT: OSPRay initialization failed.\n");
    showFatalStartupError("IBRT: OSPRay initialization failed.");
    return 1;
  }

  OSPDevice device = ospNewDevice("cpu");
  if (!device) {
    fprintf(stderr, "IBRT: Failed to create OSPRay CPU device.\n");
    showFatalStartupError("IBRT: Failed to create OSPRay CPU device.");
    return 1;
  }

  ospSetCurrentDevice(device);
  ospDeviceSetErrorCallback(device, osprayErrorCallback, nullptr);
  ospDeviceSetStatusCallback(device, osprayStatusCallback, nullptr);
  ospCommit((OSPObject)device);

  // The viewer and worker both rely on the CPU module being explicitly loaded.
  if (ospLoadModule("cpu") != OSP_NO_ERROR) {
    fprintf(stderr, "IBRT: Failed to load OSPRay CPU module.\n");
    showFatalStartupError("IBRT: Failed to load OSPRay CPU module.");
    return 1;
  }

  int rc = 0;
  {
    // Qt owns the app lifetime from here; MainWindow wires the viewer widget to
    // the worker process and initial demo loading.
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    rc = a.exec();
  }

  ospShutdown();
  return rc;
}
