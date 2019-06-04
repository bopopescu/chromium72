// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TASK_MANAGER_PROVIDERS_CHILD_PROCESS_TASK_PROVIDER_H_
#define CHROME_BROWSER_TASK_MANAGER_PROVIDERS_CHILD_PROCESS_TASK_PROVIDER_H_

#include <map>
#include <memory>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/task_manager/providers/task_provider.h"
#include "content/public/browser/browser_child_process_observer.h"

namespace content {
struct ChildProcessData;
}

namespace task_manager {

class ChildProcessTask;

// Defines a provider to provide the tasks that represent various types of child
// processes such as the GPU process or a plugin process ... etc.
class ChildProcessTaskProvider
    : public TaskProvider,
      public content::BrowserChildProcessObserver {
 public:
  ChildProcessTaskProvider();
  ~ChildProcessTaskProvider() override;

  // task_manager::TaskProvider:
  Task* GetTaskOfUrlRequest(int child_id, int route_id) override;

  // content::BrowserChildProcessObserver:
  void BrowserChildProcessLaunchedAndConnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessHostDisconnected(
      const content::ChildProcessData& data) override;

 private:
  friend class ChildProcessTaskTest;

  // task_manager::TaskProvider:
  void StartUpdating() override;
  void StopUpdating() override;

  // The pre-existing child processes data will be collected on the IO thread.
  // When that is done, we will be notified on the UI thread by receiving a call
  // to this method.
  void ChildProcessDataCollected(
      std::unique_ptr<const std::vector<content::ChildProcessData>>
          child_processes);

  // Creates a ChildProcessTask from the given |data| and notifies the observer
  // of its addition.
  void CreateTask(const content::ChildProcessData& data);

  // Deletes a ChildProcessTask whose |handle| is provided after notifying the
  // observer of its deletion.
  void DeleteTask(base::ProcessHandle handle);

  // A map to track ChildProcessTasks by their handles.
  //
  // This uses pids instead of handles because on windows (where pids and
  // handles differ), there may be multiple different handles to the same
  // process.
  std::map<base::ProcessId, std::unique_ptr<ChildProcessTask>>
      tasks_by_processid_;

  // A map to track ChildProcessTask's by their child process unique ids.
  base::flat_map<int, ChildProcessTask*> tasks_by_child_id_;

  // Always keep this the last member of this class to make sure it's the
  // first thing to be destructed.
  base::WeakPtrFactory<ChildProcessTaskProvider> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ChildProcessTaskProvider);
};

}  // namespace task_manager

#endif  // CHROME_BROWSER_TASK_MANAGER_PROVIDERS_CHILD_PROCESS_TASK_PROVIDER_H_
