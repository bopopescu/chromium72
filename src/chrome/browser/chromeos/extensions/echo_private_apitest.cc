// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/echo_private_api.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/scoped_cros_settings_test_helper.h"
#include "chrome/browser/chromeos/ui/echo_dialog_view.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chromeos/chromeos_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace utils = extension_function_test_utils;

namespace chromeos {

class ExtensionEchoPrivateApiTest : public extensions::ExtensionApiTest {
 public:
  enum DialogTestAction {
    DIALOG_TEST_ACTION_NONE,
    DIALOG_TEST_ACTION_ACCEPT,
    DIALOG_TEST_ACTION_CANCEL,
  };

  ExtensionEchoPrivateApiTest()
      : expected_dialog_buttons_(ui::DIALOG_BUTTON_NONE),
        dialog_action_(DIALOG_TEST_ACTION_NONE),
        dialog_invocation_count_(0) {
  }

  ~ExtensionEchoPrivateApiTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    extensions::ExtensionApiTest::SetUpCommandLine(command_line);

    // Force usage of stub cros settings provider instead of device settings
    // provider.
    command_line->AppendSwitch(switches::kStubCrosSettings);
  }

  void RunDefaultGetUserFunctionAndExpectResultEquals(int tab_id,
                                                      bool expected_result) {
    scoped_refptr<EchoPrivateGetUserConsentFunction> function(
        EchoPrivateGetUserConsentFunction::CreateForTest(
            base::BindRepeating(&ExtensionEchoPrivateApiTest::OnDialogShown,
                                base::Unretained(this))));
    function->set_has_callback(true);

    const std::string arguments = base::StringPrintf(
        R"([{"serviceName": "name", "origin": "https://test.com", "tabId": %d}])",
        tab_id);
    std::unique_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        function.get(), arguments, browser()));

    ASSERT_TRUE(result.get());
    ASSERT_EQ(base::Value::Type::BOOLEAN, result->type());

    bool result_as_boolean = false;
    ASSERT_TRUE(result->GetAsBoolean(&result_as_boolean));

    EXPECT_EQ(expected_result, result_as_boolean);
  }

  void OnDialogShown(chromeos::EchoDialogView* dialog) {
    dialog_invocation_count_++;
    ASSERT_LE(dialog_invocation_count_, 1);

    EXPECT_EQ(expected_dialog_buttons_, dialog->GetDialogButtons());

    // Don't accept the dialog if the dialog buttons don't match expectation.
    // Accepting a dialog which should not have accept option may crash the
    // test. The test already failed, so it's ok to cancel the dialog.
    DialogTestAction dialog_action = dialog_action_;
    if (dialog_action == DIALOG_TEST_ACTION_ACCEPT &&
        expected_dialog_buttons_ != dialog->GetDialogButtons()) {
      dialog_action = DIALOG_TEST_ACTION_CANCEL;
    }

    // Perform test action on the dialog.
    // The dialog should stay around until AcceptWindow or CancelWindow is
    // called, so base::Unretained is safe.
    if (dialog_action == DIALOG_TEST_ACTION_ACCEPT) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(base::IgnoreResult(&chromeos::EchoDialogView::Accept),
                         base::Unretained(dialog)));
    } else if (dialog_action == DIALOG_TEST_ACTION_CANCEL) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(base::IgnoreResult(&chromeos::EchoDialogView::Cancel),
                         base::Unretained(dialog)));
    }
  }

  int dialog_invocation_count() const {
    return dialog_invocation_count_;
  }

  // Open and activates tab in the test browser. Returns the ID of the opened
  // tab.
  int OpenAndActivateTab() {
    AddTabAtIndex(0, GURL("about:blank"), ui::PAGE_TRANSITION_LINK);
    browser()->tab_strip_model()->ActivateTabAt(0, true);
    return extensions::ExtensionTabUtil::GetTabId(
        browser()->tab_strip_model()->GetActiveWebContents());
  }

  bool CloseTabWithId(int tab_id) {
    TabStripModel* tab_strip = nullptr;
    int tab_index = -1;
    if (!extensions::ExtensionTabUtil::GetTabById(tab_id, profile(), false,
                                                  nullptr, &tab_strip, nullptr,
                                                  &tab_index)) {
      ADD_FAILURE() << "Tab not found " << tab_id;
      return false;
    }

    return tab_strip->CloseWebContentsAt(tab_index, 0);
  }

 protected:
  int expected_dialog_buttons_;
  DialogTestAction dialog_action_;
  chromeos::ScopedCrosSettingsTestHelper settings_helper_{
      /* create_settings_service= */ false};

 private:
  int dialog_invocation_count_;
};

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest, EchoTest) {
  EXPECT_TRUE(RunComponentExtensionTest("echo/component_extension"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_InvalidOrigin) {
  const int tab_id = OpenAndActivateTab();

  expected_dialog_buttons_ =  ui::DIALOG_BUTTON_NONE;
  dialog_action_ = DIALOG_TEST_ACTION_NONE;

  scoped_refptr<EchoPrivateGetUserConsentFunction> function(
      EchoPrivateGetUserConsentFunction::CreateForTest(
          base::BindRepeating(&ExtensionEchoPrivateApiTest::OnDialogShown,
                              base::Unretained(this))));

  std::string error = utils::RunFunctionAndReturnError(
      function.get(),
      base::StringPrintf(
          R"([{"serviceName": "name", "origin": "invalid", "tabId": %d}])",
          tab_id),
      browser());

  EXPECT_EQ("Invalid origin.", error);
  EXPECT_EQ(0, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest, GetUserConsent_NoTabIdSet) {
  expected_dialog_buttons_ = ui::DIALOG_BUTTON_NONE;
  dialog_action_ = DIALOG_TEST_ACTION_NONE;

  scoped_refptr<EchoPrivateGetUserConsentFunction> function(
      EchoPrivateGetUserConsentFunction::CreateForTest(
          base::BindRepeating(&ExtensionEchoPrivateApiTest::OnDialogShown,
                              base::Unretained(this))));

  std::string error = utils::RunFunctionAndReturnError(
      function.get(),
      R"([{"serviceName": "name", "origin": "https://test.com"}])", browser());

  EXPECT_EQ("Not called from an app window - the tabId is required.", error);
  EXPECT_EQ(0, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_InactiveTab) {
  const int tab_id = OpenAndActivateTab();
  // Open and activate another tab.
  OpenAndActivateTab();

  expected_dialog_buttons_ = ui::DIALOG_BUTTON_NONE;
  dialog_action_ = DIALOG_TEST_ACTION_NONE;

  scoped_refptr<EchoPrivateGetUserConsentFunction> function(
      EchoPrivateGetUserConsentFunction::CreateForTest(
          base::BindRepeating(&ExtensionEchoPrivateApiTest::OnDialogShown,
                              base::Unretained(this))));

  const std::string arguments = base::StringPrintf(
      R"([{"serviceName": "name", "origin": "https://test.com", "tabId": %d}])",
      tab_id);
  std::string error =
      utils::RunFunctionAndReturnError(function.get(), arguments, browser());

  EXPECT_EQ("Consent requested from an inactive tab.", error);
  EXPECT_EQ(0, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest, GetUserConsent_ClosedTab) {
  const int tab_id = OpenAndActivateTab();
  ASSERT_TRUE(CloseTabWithId(tab_id));

  expected_dialog_buttons_ = ui::DIALOG_BUTTON_NONE;
  dialog_action_ = DIALOG_TEST_ACTION_NONE;

  scoped_refptr<EchoPrivateGetUserConsentFunction> function(
      EchoPrivateGetUserConsentFunction::CreateForTest(
          base::BindRepeating(&ExtensionEchoPrivateApiTest::OnDialogShown,
                              base::Unretained(this))));

  const std::string arguments = base::StringPrintf(
      R"([{"serviceName": "name", "origin": "https://test.com", "tabId": %d}])",
      tab_id);
  std::string error =
      utils::RunFunctionAndReturnError(function.get(), arguments, browser());

  EXPECT_EQ("Tab not found.", error);
  EXPECT_EQ(0, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_AllowRedeemPrefNotSet) {
  const int tab_id = OpenAndActivateTab();
  expected_dialog_buttons_ = ui::DIALOG_BUTTON_CANCEL | ui::DIALOG_BUTTON_OK;
  dialog_action_ = DIALOG_TEST_ACTION_ACCEPT;

  RunDefaultGetUserFunctionAndExpectResultEquals(tab_id, true);

  EXPECT_EQ(1, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_AllowRedeemPrefTrue) {
  const int tab_id = OpenAndActivateTab();
  settings_helper_.SetBoolean(chromeos::kAllowRedeemChromeOsRegistrationOffers,
                              true);

  expected_dialog_buttons_ = ui::DIALOG_BUTTON_CANCEL | ui::DIALOG_BUTTON_OK;
  dialog_action_ = DIALOG_TEST_ACTION_ACCEPT;

  RunDefaultGetUserFunctionAndExpectResultEquals(tab_id, true);

  EXPECT_EQ(1, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_ConsentDenied) {
  const int tab_id = OpenAndActivateTab();
  settings_helper_.SetBoolean(chromeos::kAllowRedeemChromeOsRegistrationOffers,
                              true);

  expected_dialog_buttons_ = ui::DIALOG_BUTTON_CANCEL | ui::DIALOG_BUTTON_OK;
  dialog_action_ = DIALOG_TEST_ACTION_CANCEL;

  RunDefaultGetUserFunctionAndExpectResultEquals(tab_id, false);

  EXPECT_EQ(1, dialog_invocation_count());
}

IN_PROC_BROWSER_TEST_F(ExtensionEchoPrivateApiTest,
                       GetUserConsent_AllowRedeemPrefFalse) {
  const int tab_id = OpenAndActivateTab();
  settings_helper_.SetBoolean(chromeos::kAllowRedeemChromeOsRegistrationOffers,
                              false);

  expected_dialog_buttons_ = ui::DIALOG_BUTTON_CANCEL;
  dialog_action_ = DIALOG_TEST_ACTION_CANCEL;

  RunDefaultGetUserFunctionAndExpectResultEquals(tab_id, false);

  EXPECT_EQ(1, dialog_invocation_count());
}

}  // namespace chromeos
