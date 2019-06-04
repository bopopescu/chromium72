// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_WRONG_HWID_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_WRONG_HWID_SCREEN_H_

#include "chrome/browser/chromeos/login/screens/wrong_hwid_screen.h"
#include "chrome/browser/chromeos/login/screens/wrong_hwid_screen_view.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class BaseScreenDelegate;

class MockWrongHWIDScreen : public WrongHWIDScreen {
 public:
  MockWrongHWIDScreen(BaseScreenDelegate* base_screen_delegate,
                      WrongHWIDScreenView* view);
  ~MockWrongHWIDScreen() override;
};

class MockWrongHWIDScreenView : public WrongHWIDScreenView {
 public:
  MockWrongHWIDScreenView();
  ~MockWrongHWIDScreenView() override;

  void SetDelegate(Delegate* delegate) override;

  MOCK_METHOD0(Show, void());
  MOCK_METHOD0(Hide, void());
  MOCK_METHOD1(MockSetDelegate, void(Delegate*));

 private:
  Delegate* delegate_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_WRONG_HWID_SCREEN_H_
