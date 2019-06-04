// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_WAKE_LOCK_POWER_SAVE_BLOCKER_POWER_SAVE_BLOCKER_H_
#define SERVICES_DEVICE_WAKE_LOCK_POWER_SAVE_BLOCKER_POWER_SAVE_BLOCKER_H_

#include <memory>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "services/device/public/mojom/wake_lock.mojom.h"

#if defined(OS_ANDROID)
#include "ui/android/view_android.h"
#endif  // OS_ANDROID

namespace device {

// A RAII-style class to block the system from entering low-power (sleep) mode.
// This class is thread-safe; it may be constructed and deleted on any thread.
class PowerSaveBlocker {
 public:
  // Pass in the type of power save blocking desired. If multiple types of
  // blocking are desired, instantiate one PowerSaveBlocker for each type.
  // |reason| and |description| (a more-verbose, human-readable justification of
  // the blocking) may be provided to the underlying system APIs on some
  // platforms.
  PowerSaveBlocker(
      mojom::WakeLockType type,
      mojom::WakeLockReason reason,
      const std::string& description,
      scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> blocking_task_runner);
  virtual ~PowerSaveBlocker();

#if defined(OS_ANDROID)
  // On Android, the mojom::WakeLockType::kPreventDisplaySleep type of
  // PowerSaveBlocker should associated with a View, so the blocker can be
  // removed by the platform. Note that |view_android| is guaranteed to be
  // valid only for the lifetime of this call; hence it should not be cached
  // internally.
  void InitDisplaySleepBlocker(ui::ViewAndroid* view_android);
#endif

 private:
  class Delegate;

  // Implementations of this class may need a second object with different
  // lifetime than the RAII container, or additional storage. This member is
  // here for that purpose. If not used, just define the class as an empty
  // RefCounted (or RefCountedThreadSafe) like so to make it compile:
  // class PowerSaveBlocker::Delegate
  //     : public base::RefCounted<PowerSaveBlocker::Delegate> {
  //  private:
  //   friend class base::RefCounted<Delegate>;
  //   ~Delegate() {}
  // };
  scoped_refptr<Delegate> delegate_;

#if defined(USE_X11)
  // Since display sleep prevention also implies system suspend prevention, for
  // the Linux FreeDesktop API case, there needs to be a second delegate to
  // block system suspend when screen saver / display sleep is blocked.
  scoped_refptr<Delegate> freedesktop_suspend_delegate_;
#endif

  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(PowerSaveBlocker);
};

}  // namespace device

#endif  // SERVICES_DEVICE_WAKE_LOCK_POWER_SAVE_BLOCKER_POWER_SAVE_BLOCKER_H_
