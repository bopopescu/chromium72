// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WINDOW_DIALOG_DIALOG_OBSERVER_H_
#define UI_VIEWS_WINDOW_DIALOG_DIALOG_OBSERVER_H_

#include "ui/views/views_export.h"

namespace views {

// Allows properties on a ui::DialogModel to be observed.
class VIEWS_EXPORT DialogObserver {
 public:
  // Invoked when a dialog signals a model change. E.g., the enabled buttons, or
  // the button titles.
  virtual void OnDialogModelChanged() = 0;
};

}  // namespace views

#endif  // UI_VIEWS_WINDOW_DIALOG_DIALOG_OBSERVER_H_
