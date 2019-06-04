// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/metrics/subprocess_metrics_provider.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/notification_types.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class ExtensionFunctionalTest : public ExtensionBrowserTest {
 public:
  void InstallExtensionSilently(ExtensionService* service,
                                const char* filename) {
    ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
    size_t num_before = registry->enabled_extensions().size();

    base::FilePath path = test_data_dir_.AppendASCII(filename);

    TestExtensionRegistryObserver extension_observer(registry);

    scoped_refptr<CrxInstaller> installer(CrxInstaller::CreateSilent(service));
    installer->set_is_gallery_install(false);
    installer->set_allow_silent_install(true);
    installer->set_install_source(Manifest::INTERNAL);
    installer->set_off_store_install_allow_reason(
        CrxInstaller::OffStoreInstallAllowedInTest);

    observer_->Watch(NOTIFICATION_CRX_INSTALLER_DONE,
                     content::Source<CrxInstaller>(installer.get()));

    installer->InstallCrx(path);
    observer_->Wait();

    size_t num_after = registry->enabled_extensions().size();
    EXPECT_EQ(num_before + 1, num_after);

    extension_observer.WaitForExtensionLoaded();
    const Extension* extension =
        registry->enabled_extensions().GetByID(last_loaded_extension_id());
    EXPECT_TRUE(extension);
  }
};

// Failing on Linux: http://crbug.com/654945
#if defined(OS_LINUX)
#define MAYBE_TestSetExtensionsState DISABLED_TestSetExtensionsState
#else
#define MAYBE_TestSetExtensionsState TestSetExtensionsState
#endif
IN_PROC_BROWSER_TEST_F(ExtensionFunctionalTest, MAYBE_TestSetExtensionsState) {
  InstallExtensionSilently(extension_service(), "google_talk.crx");

  // Disable the extension and verify.
  util::SetIsIncognitoEnabled(last_loaded_extension_id(), profile(), false);
  ExtensionService* service = extension_service();
  service->DisableExtension(last_loaded_extension_id(),
                            disable_reason::DISABLE_USER_ACTION);
  EXPECT_FALSE(service->IsExtensionEnabled(last_loaded_extension_id()));

  // Enable the extension and verify.
  util::SetIsIncognitoEnabled(last_loaded_extension_id(), profile(), false);
  service->EnableExtension(last_loaded_extension_id());
  EXPECT_TRUE(service->IsExtensionEnabled(last_loaded_extension_id()));

  // Allow extension in incognito mode and verify.
  service->EnableExtension(last_loaded_extension_id());
  util::SetIsIncognitoEnabled(last_loaded_extension_id(), profile(), true);
  EXPECT_TRUE(util::IsIncognitoEnabled(last_loaded_extension_id(), profile()));

  // Disallow extension in incognito mode and verify.
  service->EnableExtension(last_loaded_extension_id());
  util::SetIsIncognitoEnabled(last_loaded_extension_id(), profile(), false);
  EXPECT_FALSE(util::IsIncognitoEnabled(last_loaded_extension_id(), profile()));
}

IN_PROC_BROWSER_TEST_F(ExtensionFunctionalTest,
                       FindingUnrelatedExtensionFramesFromAboutBlank) {
  // Load an extension before adding tabs.
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("simple_with_file"));
  ASSERT_TRUE(extension);
  GURL extension_url = extension->GetResourceURL("file.html");

  // Load the extension in two unrelated tabs.
  ui_test_utils::NavigateToURL(browser(), extension_url);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), extension_url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // Sanity-check test setup: 2 frames share a renderer process, but are not in
  // a related browsing instance.
  content::RenderFrameHost* tab1 =
      browser()->tab_strip_model()->GetWebContentsAt(0)->GetMainFrame();
  content::RenderFrameHost* tab2 =
      browser()->tab_strip_model()->GetWebContentsAt(1)->GetMainFrame();
  EXPECT_EQ(tab1->GetProcess(), tab2->GetProcess());
  EXPECT_FALSE(
      tab1->GetSiteInstance()->IsRelatedSiteInstance(tab2->GetSiteInstance()));

  // Name the 2 frames.
  EXPECT_TRUE(content::ExecuteScript(tab1, "window.name = 'tab1';"));
  EXPECT_TRUE(content::ExecuteScript(tab2, "window.name = 'tab2';"));

  // Open a new window from tab1 and store it in tab1_popup.
  content::RenderFrameHost* tab1_popup = nullptr;
  {
    content::WebContentsAddedObserver new_window_observer;
    bool did_create_popup = false;
    ASSERT_TRUE(ExecuteScriptAndExtractBool(
        tab1,
        "window.domAutomationController.send("
        "    !!window.open('about:blank', 'new_popup'));",
        &did_create_popup));
    ASSERT_TRUE(did_create_popup);
    content::WebContents* popup_window = new_window_observer.GetWebContents();
    WaitForLoadStop(popup_window);
    tab1_popup = popup_window->GetMainFrame();
  }
  EXPECT_EQ(GURL(url::kAboutBlankURL), tab1_popup->GetLastCommittedURL());

  // Verify that |tab1_popup| can find unrelated frames from the same extension
  // (i.e. that it can find |tab2|.
  // TODO(lukasza): https://crbug.com/786411: The verification below is helpful
  // to 1) verify about:blank-handling, parent-hopping done by
  // GetExtensionFromFrame in extension_frame_helper.cc and 2) verify the old
  // behavior.  We want to change the old behavior - this would expectedly make
  // the assestion below fail and in this case we would need to tweak the test
  // to look-up another window (most likely a background page).
  base::HistogramTester histogram_tester;
  std::string location_of_opened_window;
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      tab1_popup,
      "var w = window.open('', 'tab2');\n"
      "window.domAutomationController.send(w.location.href);",
      &location_of_opened_window));
  EXPECT_EQ(tab2->GetLastCommittedURL(), location_of_opened_window);

  // Verify UMA got recorded as expected.
  SubprocessMetricsProvider::MergeHistogramDeltasForTesting();
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Extensions.BrowsingInstanceViolation.ExtensionType"),
              testing::ElementsAre(base::Bucket(Manifest::TYPE_EXTENSION, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Extensions.BrowsingInstanceViolation.SourceExtensionViewType"),
      testing::ElementsAre(base::Bucket(VIEW_TYPE_TAB_CONTENTS, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Extensions.BrowsingInstanceViolation.TargetExtensionViewType"),
      testing::ElementsAre(base::Bucket(VIEW_TYPE_TAB_CONTENTS, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Extensions.BrowsingInstanceViolation.IsBackgroundSourceOrTarget"),
      testing::ElementsAre(base::Bucket(false, 1)));
}

}  // namespace extensions
