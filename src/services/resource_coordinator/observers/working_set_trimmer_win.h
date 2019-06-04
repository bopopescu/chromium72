// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_OBSERVERS_WORKING_SET_TRIMMER_WIN_H_
#define SERVICES_RESOURCE_COORDINATOR_OBSERVERS_WORKING_SET_TRIMMER_WIN_H_

#include "base/macros.h"
#include "services/resource_coordinator/observers/coordination_unit_graph_observer.h"

namespace resource_coordinator {

// Empties the working set of processes in which all frames are frozen.
//
// Objective #1: Track working set growth rate.
//   Swap trashing occurs when a lot of pages are accessed in a short period of
//   time. Swap trashing can be reduced by reducing the number of pages accessed
//   by processes in which all frames are frozen. To track efforts towards this
//   goal, we empty the working set of processes when all their frames become
//   frozen and record the size of their working set after x minutes.
//   TODO(fdoray): Record the working set size x minutes after emptying it.
//   https://crbug.com/885293
//
// Objective #2: Improve performance.
//   We hypothesize that emptying the working set of a process causes its pages
//   to be compressed and/or written to disk preemptively, which makes more
//   memory available quickly for foreground processes and improves global
//   browser performance.
class WorkingSetTrimmer : public CoordinationUnitGraphObserver {
 public:
  WorkingSetTrimmer();
  ~WorkingSetTrimmer() override;

  // CoordinationUnitGraphObserver:
  bool ShouldObserve(const CoordinationUnitBase* coordination_unit) override;
  void OnAllFramesInProcessFrozen(
      const ProcessCoordinationUnitImpl* process_cu) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(WorkingSetTrimmer);
};

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_OBSERVERS_WORKING_SET_TRIMMER_WIN_H_
