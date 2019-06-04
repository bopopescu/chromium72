// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/chrome_cleaner/logging/scoped_timed_task_logger.h"

#include "base/bind.h"
#include "base/logging.h"

namespace chrome_cleaner {

// static.
void ScopedTimedTaskLogger::LogIfExceedThreshold(
    const char* logging_text,
    const base::TimeDelta& threshold,
    const base::TimeDelta& elapsed_time) {
  DCHECK(logging_text);
  if (elapsed_time >= threshold) {
    LOG(WARNING) << logging_text << " took '" << elapsed_time.InSeconds()
                 << "' seconds.";
  }
}

ScopedTimedTaskLogger::ScopedTimedTaskLogger(TimerCallback timer_callback)
    : start_time_(base::Time::NowFromSystemTime()),
      timer_callback_(std::move(timer_callback)) {}

ScopedTimedTaskLogger::ScopedTimedTaskLogger(const char* logging_text)
    : ScopedTimedTaskLogger(
          base::BindRepeating(&LogIfExceedThreshold,
                              logging_text,
                              base::TimeDelta::FromSeconds(1))) {}

ScopedTimedTaskLogger::~ScopedTimedTaskLogger() {
  std::move(timer_callback_).Run(base::Time::NowFromSystemTime() - start_time_);
}

}  // namespace chrome_cleaner
