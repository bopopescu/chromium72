// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/intent_helper/arc_external_protocol_dialog.h"

#include <memory>

#include "base/macros.h"
#include "chrome/browser/chromeos/arc/arc_web_contents_data.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/arc/intent_helper/arc_intent_helper_bridge.h"
#include "url/gurl.h"

namespace arc {

namespace {

// Helper class to run tests that need a dummy WebContents.
class ArcExternalProtocolDialogTestUtils : public BrowserWithTestWindowTest {
 public:
  ArcExternalProtocolDialogTestUtils() = default;

 protected:
  void CreateTab(bool started_from_arc) {
    AddTab(browser(), GURL("http://www.tests.com"));

    web_contents_ = browser()->tab_strip_model()->GetWebContentsAt(0);
    if (started_from_arc) {
      web_contents_->SetUserData(&arc::ArcWebContentsData::kArcTransitionFlag,
                                 std::make_unique<arc::ArcWebContentsData>());
    }
  }

  bool WasTabStartedFromArc() {
    return GetAndResetSafeToRedirectToArcWithoutUserConfirmationFlagForTesting(
        web_contents_);
  }

 private:
  // Keep only one |WebContents| at a time.
  content::WebContents* web_contents_;

  DISALLOW_COPY_AND_ASSIGN(ArcExternalProtocolDialogTestUtils);
};

const char* kChromePackageName =
    ArcIntentHelperBridge::kArcIntentHelperPackageName;

// Creates a dummy GurlAndActivityInfo object.
GurlAndActivityInfo CreateEmptyGurlAndActivityInfo() {
  return std::make_pair(GURL(), ArcIntentHelperBridge::ActivityName(
                                    /*package_name=*/std::string(),
                                    /*activity_name=*/std::string()));
}

// Creates and returns a new IntentHandlerInfo object.
mojom::IntentHandlerInfoPtr Create(const std::string& name,
                                   const std::string& package_name,
                                   const std::string& activity_name,
                                   bool is_preferred,
                                   const GURL& fallback_url) {
  mojom::IntentHandlerInfoPtr ptr = mojom::IntentHandlerInfo::New();
  ptr->name = name;
  ptr->package_name = package_name;
  ptr->activity_name = activity_name;
  ptr->is_preferred = is_preferred;
  if (!fallback_url.is_empty())
    ptr->fallback_url = fallback_url.spec();
  return ptr;
}

}  // namespace

// Tests that when no apps are returned from ARC, GetAction returns
// SHOW_CHROME_OS_DIALOG.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithNoApp) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();

  // Marking this as safe to bypass or not makes no difference since there are
  // no handlers.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(GetActionResult::SHOW_CHROME_OS_DIALOG,
            GetActionForTesting(GURL("external-protocol:foo"), handlers,
                                handlers.size(), &url_and_activity_name,
                                &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(GetActionResult::SHOW_CHROME_OS_DIALOG,
            GetActionForTesting(GURL("external-protocol:foo"), handlers,
                                handlers.size(), &url_and_activity_name,
                                &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests that when one app is passed to GetAction but the user hasn't selected
// it and |in_out_safe_to_bypass_ui| is true, the function returns
// HANDLE_URL_IN_ARC.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOneAppBypassesIntentPicker) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", "com.google.package.name",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(GURL("external-protocol:foo"), handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests that when one app is passed to GetAction but the user hasn't selected
// it and |in_out_safe_to_bypass_ui| is false, the function returns
// ASK_USER.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOneAppDoesntBypassIntentPicker) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", "com.google.package.name",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(GURL("external-protocol:foo"), handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests that when 2+ apps are passed to GetAction but the user hasn't selected
// any the function returns ASK_USER, independently of whether or not is marked
// as safe to bypass the ui.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoAppWontBypassIntentPicker) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", "com.google.package.name",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));
  handlers.push_back(Create("package2", "com.google.package.name2",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(GURL("external-protocol:foo"), handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(GURL("external-protocol:foo"), handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests that when one preferred app is passed to GetAction, the function
// returns HANDLE_URL_IN_ARC even if the user hasn't selected the app, safe to
// bypass the UI is not relevant for this context.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithOnePreferredApp) {
  const GURL external_url("external-protocol:foo");
  const std::string package_name("com.google.package.name");
  const std::string activity_name("com.google.activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", package_name, activity_name,
                            /*is_preferred=*/true,
                            /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();

  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  // The flag was flipped since we have a preferred app.
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
}

// Tests that when one app is passed to GetAction, the user has already selected
// it, the function returns HANDLE_URL_IN_ARC. Since the user already selected
// safe to bypass ui it's always false.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithOneAppSelected) {
  const GURL external_url("external-protocol:foo");
  const std::string package_name("com.google.package.name");
  const std::string activity_name("fake_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", package_name, activity_name,
                            /*is_preferred=*/false,
                            /*fallback_url=*/GURL()));

  constexpr size_t kSelection = 0;
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_FALSE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests the same as TestGetActionWithOnePreferredApp but with two apps.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOnePreferredAppAndOneOther) {
  const GURL external_url("external-protocol:foo");
  const std::string package_name("com.google.package2.name");
  const std::string activity_name("fake_activity_name2");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("package", "com.google.package.name",
                            "fake_activity_name",
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));
  handlers.push_back(Create("package2", package_name, activity_name,
                            /*is_preferred=*/true,
                            /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  // For cases with 2+ apps it doesn't matter whether it was marked as safe to
  // bypass or not, it will only check for user's preferrences.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  // It is expected to correct the flag to true, regardless of the initial
  // value, since there is a preferred app.
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(external_url, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(external_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests that HANDLE_URL_IN_ARC is returned for geo: URL. The URL is special in
// that intent_helper (i.e. the Chrome proxy) can handle it but Chrome cannot.
// We have to send such a URL to intent_helper to let the helper rewrite the
// URL to https://maps.google.com/?latlon=xxx which Chrome can handle. Since the
// url needs to be fixed in ARC first, safe to bypass doesn't modify this
// behavior.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithGeoUrl) {
  const GURL geo_url("geo:37.7749,-122.4194");

  const std::string activity_name("chrome_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Chrome", kChromePackageName, activity_name,
                            /*is_preferred=*/true,
                            /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(geo_url, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(geo_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  // Value will be corrected as in previous scenarios.
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests that OPEN_URL_IN_CHROME is returned when a handler with a fallback http
// URL and kChromePackageName is passed to GetAction, even if the handler is not
// a preferred one.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithOneFallbackUrl) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");
  const std::string activity_name("fake_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Chrome", kChromePackageName, activity_name,
                            /*is_preferred=*/false, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();

  // Since the navigation is intended to stay in Chrome the UI is bypassed.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests the same with https and is_preferred == true.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithOnePreferredFallbackUrl) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=https://zxing.org;end");
  const GURL fallback_url("https://zxing.org");
  const std::string activity_name("fake_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Chrome", kChromePackageName, activity_name,
                            /*is_preferred=*/true, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();

  // Safe to bypass should be marked as true in the end, since the
  // OPEN_URL_IN_CHROME actually bypasses the UI, regardless of the flag.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  // Changing the flag will not modify the outcome.
  in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests that ASK_USER is returned when two handlers with fallback URLs are
// passed to GetAction. This may happen when the user has installed a 3rd party
// browser app, and then clicks a intent: URI with a http fallback.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithTwoFallbackUrls) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other browser", "com.other.browser",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Chrome", kChromePackageName,
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests the same but set Chrome as a preferred app. In this case, ASK_USER
// shouldn't be returned.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoFallbackUrlsChromePreferred) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");
  const std::string chrome_activity("chrome_activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other browser", "com.other.browser",
                            "fake_activity",
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Chrome", kChromePackageName, chrome_activity,
                            /*is_preferred=*/true, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(chrome_activity, url_and_activity_name.second.activity_name);
  // Remember that this flag gets fixed under the presence of a preferred app.
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests the same but set "other browser" as a preferred app. In this case,
// ASK_USER shouldn't be returned either.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoFallbackUrlsOtherBrowserPreferred) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");
  const std::string package_name = "com.other.browser";
  const std::string chrome_activity_name("chrome_activity_name");
  const std::string other_activity_name("other_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other browser", package_name, other_activity_name,
                            /*is_preferred=*/true, fallback_url));
  handlers.push_back(Create("Chrome", kChromePackageName, chrome_activity_name,
                            /*is_preferred=*/false, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(other_activity_name, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests the same but set Chrome as a user-selected app.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoFallbackUrlsChromeSelected) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");
  const std::string chrome_activity_name("chrome_activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other browser", "com.other.browser",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Chrome", kChromePackageName, chrome_activity_name,
                            /*is_preferred=*/false, fallback_url));

  constexpr size_t kSelection = 1;  // Chrome
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::OPEN_URL_IN_CHROME,
      GetActionForTesting(intent_url_with_fallback, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(chrome_activity_name, url_and_activity_name.second.activity_name);
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests the same but set "other browser" as a preferred app.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoFallbackUrlsOtherBrowserSelected) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=http://zxing.org;end");
  const GURL fallback_url("http://zxing.org");
  const std::string package_name = "com.other.browser";
  const std::string other_activity_name("other_activity_name");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other browser", package_name, other_activity_name,
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Chrome", kChromePackageName, "chrome_activity",
                            /*is_preferred=*/false, fallback_url));

  constexpr size_t kSelection = 0;  // the other browser
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  // Already selected app index, output should be corrected to false.
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(other_activity_name, url_and_activity_name.second.activity_name);
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests that HANDLE_URL_IN_ARC is returned when a handler with a fallback
// market: URL is passed to GetAction iff the flag to bypass the UI is set,
// otherwise UI will prompt to ASK_USER.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithOneMarketFallbackUrl) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Play Store", "com.google.play.store",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests the same but with is_preferred == true.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOnePreferredMarketFallbackUrl) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");
  const std::string play_store_package_name = "com.google.play.store";
  const std::string play_store_activity("play_store_activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Play Store", play_store_package_name,
                            play_store_activity,
                            /*is_preferred=*/true, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(play_store_package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(play_store_activity, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);

  in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(play_store_package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(play_store_activity, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests the same but with an app_seleteced_index.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOneSelectedMarketFallbackUrl) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");
  const std::string play_store_package_name = "com.google.play.store";
  const std::string play_store_activity("play_store_activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Play Store", play_store_package_name,
                            play_store_activity,
                            /*is_preferred=*/false, fallback_url));

  constexpr size_t kSelection = 0;
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  // App already selected, it doesn't really makes sense to call GetAction with
  // |in_out_safe_to_bypass_ui| set to true here.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(play_store_package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(play_store_activity, url_and_activity_name.second.activity_name);
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests that HANDLE_URL_IN_ARC is returned when a handler with a fallback
// market: URL is passed to GetAction.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithOneMarketFallbackUrlBypassIntentPicker) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Play Store", "com.google.play.store",
                            "play_store_activity", /*is_preferred=*/false,
                            fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = true;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests that ASK_USER is returned when two handlers with fallback market: URLs
// are passed to GetAction. Unlike the two browsers case, this rarely happens on
// the user's device, though.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithTwoMarketFallbackUrls) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Play Store", "com.google.play.store",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Other Store app", "com.other.play.store",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::ASK_USER,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_FALSE(in_out_safe_to_bypass_ui);
}

// Tests the same, but make the second handler a preferred one.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoMarketFallbackUrlsOnePreferred) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");
  const std::string play_store_package_name = "com.google.play.store";
  const std::string play_store_activity("play.store.act1");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other Store app", "com.other.play.store",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Play Store", play_store_package_name,
                            play_store_activity,
                            /*is_preferred=*/true, fallback_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(play_store_package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(play_store_activity, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Tests the same, but make the second handler a selected one.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithTwoMarketFallbackUrlsOneSelected) {
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;end");
  const GURL fallback_url("market://details?id=com.google.abc");
  const std::string play_store_package_name = "com.google.play.store";
  const std::string play_store_activity("play.store.act1");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Other Store app", "com.other.play.store",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, fallback_url));
  handlers.push_back(Create("Play Store", play_store_package_name,
                            play_store_activity,
                            /*is_preferred=*/false, fallback_url));

  const size_t kSelection = 1;  // Play Store
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  // After selection doesn't really makes sense to check this value.
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, kSelection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(fallback_url, url_and_activity_name.first);
  EXPECT_EQ(play_store_package_name, url_and_activity_name.second.package_name);
  EXPECT_EQ(play_store_activity, url_and_activity_name.second.activity_name);
}

// Tests the case where geo: URL is returned as a fallback. This should never
// happen because intent_helper ignores such a fallback, but just in case.
// GetAction shouldn't crash at least.
TEST(ArcExternalProtocolDialogTest, TestGetActionWithGeoUrlAsFallback) {
  // Note: geo: as a browser fallback is banned in the production code.
  const GURL intent_url_with_fallback(
      "intent://scan/#Intent;scheme=abc;package=com.google.abc;"
      "S.browser_fallback_url=geo:37.7749,-122.4194;end");
  const GURL geo_url("geo:37.7749,-122.4194");
  const std::string chrome_activity("chrome.activity");

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("Chrome", kChromePackageName, chrome_activity,
                            /*is_preferred=*/true, geo_url));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  // GetAction shouldn't return OPEN_URL_IN_CHROME because Chrome doesn't
  // directly support geo:.
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(intent_url_with_fallback, handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_EQ(geo_url, url_and_activity_name.first);
  EXPECT_EQ(kChromePackageName, url_and_activity_name.second.package_name);
  EXPECT_EQ(chrome_activity, url_and_activity_name.second.activity_name);
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

// Test that GetUrlToNavigateOnDeactivate returns an empty GURL when |handlers|
// is empty.
TEST(ArcExternalProtocolDialogTest, TestGetUrlToNavigateOnDeactivateEmpty) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  EXPECT_EQ(GURL(), GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Test that GetUrlToNavigateOnDeactivate returns an empty GURL when |handlers|
// only contains a (non-Chrome) app.
TEST(ArcExternalProtocolDialogTest, TestGetUrlToNavigateOnDeactivateAppOnly) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  // On production, when |handlers| only contains app(s), the fallback field is
  // empty, but to make the test more reliable, use non-empty fallback URL.
  handlers.push_back(Create("App", "app.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www")));
  EXPECT_EQ(GURL(), GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Test that GetUrlToNavigateOnDeactivate returns an empty GURL when |handlers|
// only contains (non-Chrome) apps.
TEST(ArcExternalProtocolDialogTest, TestGetUrlToNavigateOnDeactivateAppsOnly) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  // On production, when |handlers| only contains app(s), the fallback field is
  // empty, but to make the test more reliable, use non-empty fallback URL.
  handlers.push_back(Create("App1", "app1.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www")));
  handlers.push_back(Create("App2", "app2.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www")));
  EXPECT_EQ(GURL(), GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Test that GetUrlToNavigateOnDeactivate returns an empty GURL when |handlers|
// contains Chrome, but it's not for http(s).
TEST(ArcExternalProtocolDialogTest, TestGetUrlToNavigateOnDeactivateGeoUrl) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(
      Create("Chrome", kChromePackageName, /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("geo:37.4220,-122.0840")));
  EXPECT_EQ(GURL(), GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Test that GetUrlToNavigateOnDeactivate returns non-empty GURL when |handlers|
// contains Chrome and an app.
TEST(ArcExternalProtocolDialogTest,
     TestGetUrlToNavigateOnDeactivateChromeAndApp) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  // On production, all handlers have the same fallback URL, but to make sure
  // that "Chrome" is actually selected by the function, use different URLs.
  handlers.push_back(Create("A browser app", "browser.app.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www1/")));
  handlers.push_back(Create("Chrome", kChromePackageName,
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www2/")));
  handlers.push_back(Create("Yet another browser app",
                            "yet.another.browser.app.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("http://www3/")));

  EXPECT_EQ(GURL("http://www2/"),
            GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Does the same with https, just in case.
TEST(ArcExternalProtocolDialogTest,
     TestGetUrlToNavigateOnDeactivateChromeAndAppHttps) {
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("A browser app", "browser.app.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("https://www1/")));
  handlers.push_back(Create("Chrome", kChromePackageName,
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("https://www2/")));
  handlers.push_back(Create("Yet another browser app",
                            "yet.another.browser.app.package",
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("https://www3/")));

  EXPECT_EQ(GURL("https://www2/"),
            GetUrlToNavigateOnDeactivateForTesting(handlers));
}

// Checks that the flag is correctly attached to the current tab.
TEST_F(ArcExternalProtocolDialogTestUtils, TestTabIsStartedFromArc) {
  CreateTab(/*started_from_arc=*/true);

  EXPECT_TRUE(WasTabStartedFromArc());
}

// Tests the same as the previous, just for when the data is not attached to the
// tab.
TEST_F(ArcExternalProtocolDialogTestUtils, TestTabIsNotStartedFromArc) {
  CreateTab(/*started_from_arc=*/false);

  EXPECT_FALSE(WasTabStartedFromArc());
}

// Tests that IsChromeAnAppCandidate works as intended.
TEST(ArcExternalProtocolDialogTest, TestIsChromeAnAppCandidate) {
  // First 3 cases are valid, just switching the position of Chrome.
  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(
      Create("fake app 1", "fake.app.package", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.fo.com")));
  handlers.push_back(
      Create("fake app 2", "fake.app.package2", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.bar.com")));
  handlers.push_back(Create("Chrome", kChromePackageName,
                            /*activity_name=*/std::string(),
                            /*is_preferred=*/false, GURL("https://www/")));
  EXPECT_TRUE(IsChromeAnAppCandidateForTesting(handlers));

  std::vector<mojom::IntentHandlerInfoPtr> handlers2;
  handlers2.push_back(
      Create("fake app 1", "fake.app.package", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.fo.com")));
  handlers2.push_back(Create("Chrome", kChromePackageName,
                             /*activity_name=*/std::string(),
                             /*is_preferred=*/false, GURL("https://www/")));
  handlers2.push_back(
      Create("fake app 2", "fake.app.package2", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.bar.com")));
  EXPECT_TRUE(IsChromeAnAppCandidateForTesting(handlers2));

  std::vector<mojom::IntentHandlerInfoPtr> handlers3;
  handlers3.push_back(Create("Chrome", kChromePackageName,
                             /*activity_name=*/std::string(),
                             /*is_preferred=*/false, GURL("https://www/")));
  handlers3.push_back(
      Create("fake app 1", "fake.app.package", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.fo.com")));
  handlers3.push_back(
      Create("fake app 2", "fake.app.package2", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.bar.com")));
  EXPECT_TRUE(IsChromeAnAppCandidateForTesting(handlers3));

  // Only non-Chrome apps.
  std::vector<mojom::IntentHandlerInfoPtr> handlers4;
  handlers4.push_back(
      Create("fake app 1", "fake.app.package", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.fo.com")));
  handlers4.push_back(
      Create("fake app 2", "fake.app.package2", /*activity_name=*/std::string(),
             /*is_preferred=*/false, GURL("https://www.bar.com")));
  handlers4.push_back(Create("fake app 3", "fake.app.package3",
                             /*activity_name=*/std::string(),
                             /*is_preferred=*/false, GURL("https://www/")));
  EXPECT_FALSE(IsChromeAnAppCandidateForTesting(handlers4));

  // Empty vector case.
  EXPECT_FALSE(IsChromeAnAppCandidateForTesting(
      std::vector<mojom::IntentHandlerInfoPtr>()));
}

// Tests that when one app is passed to GetAction and it's for ARC IME, the
// picker won't be triggered.
TEST(ArcExternalProtocolDialogTest,
     TestGetActionWithArcImeSettingsActivityBypassesIntentPicker) {
  constexpr char kPackageForOpeningArcImeSettingsPage[] =
      "org.chromium.arc.applauncher";
  constexpr char kActivityForOpeningArcImeSettingsPage[] =
      "org.chromium.arc.applauncher.InputMethodSettingsActivity";

  std::vector<mojom::IntentHandlerInfoPtr> handlers;
  handlers.push_back(Create("ARC IME settings",
                            kPackageForOpeningArcImeSettingsPage,
                            kActivityForOpeningArcImeSettingsPage,
                            /*is_preferred=*/false, /*fallback_url=*/GURL()));

  const size_t no_selection = handlers.size();
  GurlAndActivityInfo url_and_activity_name = CreateEmptyGurlAndActivityInfo();
  bool in_out_safe_to_bypass_ui = false;
  EXPECT_EQ(
      GetActionResult::HANDLE_URL_IN_ARC,
      GetActionForTesting(GURL("intent:foo"), handlers, no_selection,
                          &url_and_activity_name, &in_out_safe_to_bypass_ui));
  EXPECT_TRUE(in_out_safe_to_bypass_ui);
}

}  // namespace arc
