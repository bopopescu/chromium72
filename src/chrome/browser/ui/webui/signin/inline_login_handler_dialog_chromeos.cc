// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/inline_login_handler_dialog_chromeos.h"

#include <string>

#include "base/logging.h"
#include "base/macros.h"
#include "chrome/browser/ui/webui/chromeos/system_web_dialog_delegate.h"
#include "chrome/common/webui_url_constants.h"
#include "ui/aura/window.h"

namespace chromeos {

namespace {

InlineLoginHandlerDialogChromeOS* dialog = nullptr;

}  // namespace

// static
void InlineLoginHandlerDialogChromeOS::Show() {
  if (dialog) {
    dialog->dialog_window()->Focus();
    return;
  }

  // Will be deleted by |SystemWebDialogDelegate::OnDialogClosed|.
  dialog = new InlineLoginHandlerDialogChromeOS();
  dialog->ShowSystemDialog(false /* is_minimal_style */);
}

InlineLoginHandlerDialogChromeOS::InlineLoginHandlerDialogChromeOS()
    : SystemWebDialogDelegate(GURL(chrome::kChromeUIChromeSigninURL),
                              base::string16() /* title */) {}

InlineLoginHandlerDialogChromeOS::~InlineLoginHandlerDialogChromeOS() {
  DCHECK_EQ(this, dialog);
  dialog = nullptr;
}

std::string InlineLoginHandlerDialogChromeOS::GetDialogArgs() const {
  return std::string();
}

bool InlineLoginHandlerDialogChromeOS::ShouldShowDialogTitle() const {
  return false;
}

void InlineLoginHandlerDialogChromeOS::GetDialogSize(gfx::Size* size) const {
  constexpr int kSigninDialogWidth = 800;
  constexpr int kSigninDialogHeight = 700;
  size->SetSize(kSigninDialogWidth, kSigninDialogHeight);
}

}  // namespace chromeos
