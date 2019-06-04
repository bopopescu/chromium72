// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/crash/content/app/crashpad.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/global_descriptors.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "components/crash/content/app/crash_reporter_client.h"
#include "content/public/common/content_descriptors.h"
#include "sandbox/linux/services/syscall_wrappers.h"
#include "third_party/crashpad/crashpad/client/annotation.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"
#include "third_party/crashpad/crashpad/snapshot/sanitized/sanitization_information.h"
#include "third_party/crashpad/crashpad/util/linux/exception_handler_client.h"
#include "third_party/crashpad/crashpad/util/linux/exception_information.h"
#include "third_party/crashpad/crashpad/util/misc/from_pointer_cast.h"
#include "third_party/crashpad/crashpad/util/posix/signals.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#include "base/android/java_exception_reporter.h"
#endif  // OS_ANDROID

namespace crashpad {
namespace {

bool SetSanitizationInfo(SanitizationInformation* info) {
  const char* const* whitelist = nullptr;
  void* target_module = nullptr;
  bool sanitize_stacks = false;
  crash_reporter::GetCrashReporterClient()->GetSanitizationInformation(
      &whitelist, &target_module, &sanitize_stacks);
  info->annotations_whitelist_address = FromPointerCast<VMAddress>(whitelist);
  info->target_module_address = FromPointerCast<VMAddress>(target_module);
  info->sanitize_stacks = sanitize_stacks;
  return whitelist != nullptr || target_module != nullptr || sanitize_stacks;
}

// A signal handler for non-browser processes in the sandbox.
// Sends a message to a crashpad::CrashHandlerHost to handle the crash.
class SandboxedHandler {
 public:
  static SandboxedHandler* Get() {
    static SandboxedHandler* instance = new SandboxedHandler();
    return instance;
  }

  bool Initialize() {
    SetSanitizationInfo(&sanitization_);
    server_fd_ = base::GlobalDescriptors::GetInstance()->Get(
        service_manager::kCrashDumpSignal);

    return Signals::InstallCrashHandlers(HandleCrash, 0, nullptr);
  }

 private:
  SandboxedHandler() = default;
  ~SandboxedHandler() = delete;

  int ConnectToHandler(int signo, base::ScopedFD* connection) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
      return errno;
    }
    base::ScopedFD local_connection(fds[0]);
    base::ScopedFD handlers_socket(fds[1]);

    iovec iov;
    iov.iov_base = &signo;
    iov.iov_len = sizeof(signo);

    msghdr msg;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int*>(CMSG_DATA(cmsg)) = handlers_socket.get();

    if (HANDLE_EINTR(sendmsg(server_fd_, &msg, MSG_NOSIGNAL)) < 0) {
      return errno;
    }

    *connection = std::move(local_connection);
    return 0;
  }

  static void HandleCrash(int signo, siginfo_t* siginfo, void* context) {
    SandboxedHandler* state = Get();

    base::ScopedFD connection;
    if (state->ConnectToHandler(signo, &connection) == 0) {
      ExceptionInformation exception_information;
      exception_information.siginfo_address =
          FromPointerCast<decltype(exception_information.siginfo_address)>(
              siginfo);
      exception_information.context_address =
          FromPointerCast<decltype(exception_information.context_address)>(
              context);
      exception_information.thread_id = sandbox::sys_gettid();

      ClientInformation info = {};
      info.exception_information_address =
          FromPointerCast<decltype(info.exception_information_address)>(
              &exception_information);

      info.sanitization_information_address =
          FromPointerCast<decltype(info.sanitization_information_address)>(
              &state->sanitization_);

      ExceptionHandlerClient handler_client(connection.get());
      handler_client.SetCanSetPtracer(false);
      handler_client.RequestCrashDump(info);
    }

    Signals::RestoreHandlerAndReraiseSignalOnReturn(siginfo, nullptr);
  }

  SanitizationInformation sanitization_;
  int server_fd_;

  DISALLOW_COPY_AND_ASSIGN(SandboxedHandler);
};

}  // namespace
}  // namespace crashpad

namespace crash_reporter {
namespace {

#if defined(OS_ANDROID)

void SetJavaExceptionInfo(const char* info_string) {
  static crashpad::StringAnnotation<5 * 4096> exception_info("exception_info");
  if (info_string) {
    exception_info.Set(info_string);
  } else {
    exception_info.Clear();
  }
}

void SetBuildInfoAnnotations(std::map<std::string, std::string>* annotations) {
  base::android::BuildInfo* info = base::android::BuildInfo::GetInstance();

  (*annotations)["android_build_id"] = info->android_build_id();
  (*annotations)["android_build_fp"] = info->android_build_fp();
  (*annotations)["device"] = info->device();
  (*annotations)["model"] = info->model();
  (*annotations)["brand"] = info->brand();
  (*annotations)["board"] = info->board();
  (*annotations)["installer_package_name"] = info->installer_package_name();
  (*annotations)["abi_name"] = info->abi_name();
  (*annotations)["custom_themes"] = info->custom_themes();
  (*annotations)["resources_verison"] = info->resources_version();
  (*annotations)["gms_core_version"] = info->gms_version_code();

  if (info->firebase_app_id()[0] != '\0') {
    (*annotations)["package"] = std::string(info->firebase_app_id()) + " v" +
                                info->package_version_code() + " (" +
                                info->package_version_name() + ")";
  }
}

#endif  // OS_ANDROID

bool BuildHandlerArgs(base::FilePath* database_path,
                      base::FilePath* metrics_path,
                      std::string* url,
                      std::map<std::string, std::string>* process_annotations,
                      std::vector<std::string>* arguments) {
  CrashReporterClient* crash_reporter_client = GetCrashReporterClient();
  crash_reporter_client->GetCrashDumpLocation(database_path);
  crash_reporter_client->GetCrashMetricsLocation(metrics_path);

// TODO(jperaza): Set URL for Android when Crashpad takes over report upload.
#if defined(GOOGLE_CHROME_BUILD) && defined(OFFICIAL_BUILD) && \
    !defined(OS_ANDROID)
  *url = "https://clients2.google.com/cr/report";
#else
  *url = std::string();
#endif

  std::string product_name;
  std::string product_version;
  std::string channel;
  crash_reporter_client->GetProductNameAndVersion(&product_name,
                                                  &product_version, &channel);
  (*process_annotations)["prod"] = product_name;
  (*process_annotations)["ver"] = product_version;

#if defined(OS_ANDROID)
  SetBuildInfoAnnotations(process_annotations);
#endif  // OS_ANDROID

#if defined(GOOGLE_CHROME_BUILD)
  // Empty means stable.
  const bool allow_empty_channel = true;
#else
  const bool allow_empty_channel = false;
#endif
  if (allow_empty_channel || !channel.empty()) {
    (*process_annotations)["channel"] = channel;
  }

#if defined(OS_ANDROID)
  (*process_annotations)["plat"] = std::string("Android");
#else
  (*process_annotations)["plat"] = std::string("Linux");
#endif

  if (crash_reporter_client->ShouldMonitorCrashHandlerExpensively()) {
    arguments->push_back("--monitor-self");
  }

  // Set up --monitor-self-annotation even in the absence of --monitor-self
  // so that minidumps produced by Crashpad's generate_dump tool will
  // contain these annotations.
  arguments->push_back("--monitor-self-annotation=ptype=crashpad-handler");
  return true;
}

bool GetHandlerPath(base::FilePath* exe_dir, base::FilePath* handler_path) {
#if defined(OS_ANDROID)
  // There is not any normal way to package native executables in an Android
  // APK. The Crashpad handler is packaged like a loadable module, which
  // Android's APK installer expects to be named like a shared library, but it
  // is in fact a standalone executable.
  if (!base::PathService::Get(base::DIR_MODULE, exe_dir)) {
    return false;
  }
  *handler_path = exe_dir->Append("libcrashpad_handler.so");
#else
  if (!base::PathService::Get(base::DIR_EXE, exe_dir)) {
    return false;
  }
  *handler_path = exe_dir->Append("crashpad_handler");
#endif
  return true;
}

bool SetLdLibraryPath(const base::FilePath& lib_path) {
#if defined(OS_ANDROID) && defined(COMPONENT_BUILD)
  std::string library_path(lib_path.value());

  static constexpr char kLibraryPathVar[] = "LD_LIBRARY_PATH";
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  std::string old_path;
  if (env->GetVar(kLibraryPathVar, &old_path)) {
    library_path.push_back(':');
    library_path.append(old_path);
  }

  if (!env->SetVar(kLibraryPathVar, library_path)) {
    return false;
  }
#endif

  return true;
}

class HandlerStarter {
 public:
  static HandlerStarter* Get() {
    static HandlerStarter* instance = new HandlerStarter();
    return instance;
  }

  base::FilePath Initialize() {
    base::FilePath exe_dir;
    base::FilePath handler_path;
    if (!GetHandlerPath(&exe_dir, &handler_path)) {
      return base::FilePath();
    }

    if (!SetLdLibraryPath(exe_dir)) {
      return base::FilePath();
    }

    base::FilePath database_path;
    base::FilePath metrics_path;
    std::string url;
    std::map<std::string, std::string> process_annotations;
    std::vector<std::string> arguments;
    if (!BuildHandlerArgs(&database_path, &metrics_path, &url,
                          &process_annotations, &arguments)) {
      return base::FilePath();
    }

    if (crashpad::SetSanitizationInfo(&browser_sanitization_info_)) {
      arguments.push_back(base::StringPrintf("--sanitization-information=%p",
                                             &browser_sanitization_info_));
    }

    bool result = GetCrashpadClient().StartHandlerAtCrash(
        handler_path, database_path, metrics_path, url, process_annotations,
        arguments);
    DCHECK(result);
    return database_path;
  }

  bool StartHandlerForClient(int fd) {
    base::FilePath exe_dir;
    base::FilePath handler_path;
    if (!GetHandlerPath(&exe_dir, &handler_path)) {
      return false;
    }

    if (!SetLdLibraryPath(exe_dir)) {
      return false;
    }

    base::FilePath database_path;
    base::FilePath metrics_path;
    std::string url;
    std::map<std::string, std::string> process_annotations;
    std::vector<std::string> arguments;
    if (!BuildHandlerArgs(&database_path, &metrics_path, &url,
                          &process_annotations, &arguments)) {
      return false;
    }

    return GetCrashpadClient().StartHandlerForClient(
        handler_path, database_path, metrics_path, url, process_annotations,
        arguments, fd);
  }

 private:
  HandlerStarter() = default;
  ~HandlerStarter() = delete;

  crashpad::SanitizationInformation browser_sanitization_info_;

  DISALLOW_COPY_AND_ASSIGN(HandlerStarter);
};

}  // namespace

namespace internal {

bool StartHandlerForClient(int fd) {
  return HandlerStarter::Get()->StartHandlerForClient(fd);
}

base::FilePath PlatformCrashpadInitialization(
    bool initial_client,
    bool browser_process,
    bool embedded_handler,
    const std::string& user_data_dir,
    const base::FilePath& exe_path,
    const std::vector<std::string>& initial_arguments) {
  DCHECK_EQ(initial_client, browser_process);
  DCHECK(initial_arguments.empty());

  // Not used on Linux/Android.
  DCHECK(!embedded_handler);
  DCHECK(exe_path.empty());

#if defined(OS_ANDROID)
  base::android::SetJavaExceptionCallback(SetJavaExceptionInfo);
#endif  // OS_ANDROID

  if (browser_process) {
    HandlerStarter* starter = HandlerStarter::Get();
    return starter->Initialize();
  }

  crashpad::SandboxedHandler* handler = crashpad::SandboxedHandler::Get();
  bool result = handler->Initialize();
  DCHECK(result);

  return base::FilePath();
}

}  // namespace internal

}  // namespace crash_reporter
