// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_FINGERPRINT_SETUP_SCREEN_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_FINGERPRINT_SETUP_SCREEN_VIEW_H_

#include "chrome/browser/chromeos/login/oobe_screen.h"

namespace chromeos {

class FingerprintSetupScreen;

// Interface for dependency injection between FingerprintSetupScreen and its
// WebUI representation.
class FingerprintSetupScreenView {
 public:
  constexpr static OobeScreen kScreenId = OobeScreen::SCREEN_FINGERPRINT_SETUP;

  virtual ~FingerprintSetupScreenView() = default;

  // Sets screen this view belongs to.
  virtual void Bind(FingerprintSetupScreen* screen) = 0;

  // Shows the contents of the screen.
  virtual void Show() = 0;

  // Hides the contents of the screen.
  virtual void Hide() = 0;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_FINGERPRINT_SETUP_SCREEN_VIEW_H_
