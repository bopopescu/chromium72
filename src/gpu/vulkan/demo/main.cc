// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/message_loop/message_loop.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/trace_event/trace_event.h"
#include "components/tracing/common/trace_to_console.h"
#include "components/tracing/common/tracing_switches.h"
#include "gpu/vulkan/demo/vulkan_demo.h"

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::AtExitManager exit_manager;

  base::debug::EnableInProcessStackDumping();

  // Initialize logging so we can enable VLOG messages.
  logging::LoggingSettings settings;
  logging::InitLogging(settings);

  // Initialize tracing.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kTraceToConsole)) {
    base::trace_event::TraceConfig trace_config =
        tracing::GetConfigForTraceToConsole();
    base::trace_event::TraceLog::GetInstance()->SetEnabled(
        trace_config, base::trace_event::TraceLog::RECORDING_MODE);
  }

  // Build UI thread message loop. This is used by platform
  // implementations for event polling & running background tasks.
  base::MessageLoopForUI message_loop;
  base::TaskScheduler::CreateAndStartWithDefaultParams("VulkanDemo");

  gpu::VulkanDemo vulkan_demo;
  vulkan_demo.Initialize();
  vulkan_demo.Run();
  vulkan_demo.Destroy();

  return 0;
}
