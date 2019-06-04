// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_DEMO_PREFERENCES_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_DEMO_PREFERENCES_SCREEN_H_

#include "base/macros.h"
#include "chrome/browser/chromeos/login/screens/demo_preferences_screen.h"
#include "chrome/browser/chromeos/login/screens/demo_preferences_screen_view.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockDemoPreferencesScreen : public DemoPreferencesScreen {
 public:
  MockDemoPreferencesScreen(BaseScreenDelegate* base_screen_delegate,
                            DemoPreferencesScreenView* view);
  ~MockDemoPreferencesScreen() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDemoPreferencesScreen);
};

class MockDemoPreferencesScreenView : public DemoPreferencesScreenView {
 public:
  MockDemoPreferencesScreenView();
  ~MockDemoPreferencesScreenView() override;

  MOCK_METHOD0(Show, void());
  MOCK_METHOD0(Hide, void());
  MOCK_METHOD1(MockBind, void(DemoPreferencesScreen* screen));

  void Bind(DemoPreferencesScreen* screen) override;

 private:
  DemoPreferencesScreen* screen_;

  DISALLOW_COPY_AND_ASSIGN(MockDemoPreferencesScreenView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_MOCK_DEMO_PREFERENCES_SCREEN_H_
