// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/external_web_apps.h"

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_path_override.h"
#include "chrome/browser/supervised_user/supervised_user_constants.h"
#include "chrome/browser/web_applications/components/web_app_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/testing_profile.h"
#include "components/account_id/account_id.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "components/user_manager/scoped_user_manager.h"
#endif

namespace {

constexpr char kGoodJsonTestDir[] = "good_json";
constexpr char kWebAppDefaultApps[] = "web_app_default_apps";
constexpr char kUserTypesTestDir[] = "user_types";

#if defined(OS_CHROMEOS)
constexpr char kAppAllUrl[] = "https://www.google.com/all";
constexpr char kAppChildUrl[] = "https://www.google.com/child";
constexpr char kAppGuestUrl[] = "https://www.google.com/guest";
constexpr char kAppManagedUrl[] = "https://www.google.com/managed";
constexpr char kAppSupervisedUrl[] = "https://www.google.com/supervised";
constexpr char kAppUnmanagedUrl[] = "https://www.google.com/unmanaged";
#endif

// Returns the chrome/test/data/web_app_default_apps/sub_dir directory that
// holds the *.json data files from which ScanDirForExternalWebAppsForTesting
// should extract URLs from.
static base::FilePath test_dir(const char* sub_dir) {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_TEST_DATA, &dir)) {
    ADD_FAILURE()
        << "base::PathService::Get could not resolve chrome::DIR_TEST_DATA";
  }
  return dir.AppendASCII(kWebAppDefaultApps).AppendASCII(sub_dir);
}

using AppInfos = std::vector<web_app::PendingAppManager::AppInfo>;

}  // namespace

class ScanDirForExternalWebAppsTest : public testing::Test {};

class ScanDirForExternalWebAppsWithProfileTest
    : public ScanDirForExternalWebAppsTest {
 public:
  ScanDirForExternalWebAppsWithProfileTest() = default;
  ~ScanDirForExternalWebAppsWithProfileTest() override = default;

  // ScanDirForExternalWebAppsTest:
  void SetUp() override {
    testing::Test::SetUp();
#if defined(OS_CHROMEOS)
    user_manager_enabler_ = std::make_unique<user_manager::ScopedUserManager>(
        std::make_unique<chromeos::FakeChromeUserManager>());
#endif
  }

  void TearDown() override {
#if defined(OS_CHROMEOS)
    user_manager_enabler_.reset();
#endif
    testing::Test::TearDown();
  }

 protected:
  // Helper that makes blocking call to |web_app::ScanForExternalWebApps| and
  // returns read app infos.
  static AppInfos ScanApps(Profile* profile, const base::FilePath& test_dir) {
#if defined(OS_CHROMEOS)
    base::ScopedPathOverride path_override(
        chrome::DIR_STANDALONE_EXTERNAL_EXTENSIONS, test_dir);
#endif
    AppInfos result;
    base::RunLoop run_loop;
    web_app::ScanForExternalWebApps(
        profile,
        base::BindOnce(
            [](base::RunLoop* run_loop, AppInfos* result, AppInfos apps) {
              *result = apps;
              run_loop->Quit();
            },
            &run_loop, &result));
    run_loop.Run();
    return result;
  }

  // Helper that creates simple test profile.
  std::unique_ptr<TestingProfile> CreateProfile() {
    TestingProfile::Builder profile_builder;
    return profile_builder.Build();
  }

  // Helper that creates simple test guest profile.
  std::unique_ptr<TestingProfile> CreateGuestProfile() {
    TestingProfile::Builder profile_builder;
    profile_builder.SetGuestSession();
    return profile_builder.Build();
  }

#if defined(OS_CHROMEOS)
  // Helper that creates simple test profile and logs it into user manager.
  // This makes profile appears as a primary profile in ChromeOS.
  std::unique_ptr<TestingProfile> CreateProfileAndLogin() {
    std::unique_ptr<TestingProfile> profile = CreateProfile();
    const AccountId account_id(AccountId::FromUserEmailGaiaId(
        profile->GetProfileUserName(), "1234567890"));
    user_manager()->AddUser(account_id);
    user_manager()->LoginUser(account_id);
    return profile;
  }

  // Helper that creates simple test guest profile and logs it into user
  // manager. This makes profile appears as a primary profile in ChromeOS.
  std::unique_ptr<TestingProfile> CreateGuestProfileAndLogin() {
    std::unique_ptr<TestingProfile> profile = CreateGuestProfile();
    user_manager()->AddGuestUser();
    user_manager()->LoginUser(user_manager()->GetGuestAccountId());
    return profile;
  }

  void VerifySetOfApps(Profile* profile, const std::set<GURL>& expectations) {
    const auto app_infos = ScanApps(profile, test_dir(kUserTypesTestDir));
    ASSERT_EQ(expectations.size(), app_infos.size());
    for (const auto& app_info : app_infos)
      ASSERT_EQ(1u, expectations.count(app_info.url));
  }
#endif

 private:
#if defined(OS_CHROMEOS)
  chromeos::FakeChromeUserManager* user_manager() {
    return static_cast<chromeos::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
  }

  // To supprot primary/non-primary users.
  std::unique_ptr<user_manager::ScopedUserManager> user_manager_enabler_;
#endif

  // To support context of browser threads.
  content::TestBrowserThreadBundle thread_bundle_;

  DISALLOW_COPY_AND_ASSIGN(ScanDirForExternalWebAppsWithProfileTest);
};

class ScanDirForExternalWebAppsNonPrimaryProfileTest
    : public ScanDirForExternalWebAppsTest {
  ScanDirForExternalWebAppsNonPrimaryProfileTest() = default;
  ~ScanDirForExternalWebAppsNonPrimaryProfileTest() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(ScanDirForExternalWebAppsNonPrimaryProfileTest);
};

TEST_F(ScanDirForExternalWebAppsTest, GoodJson) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir(kGoodJsonTestDir));

  // The good_json directory contains two good JSON files:
  // chrome_platform_status.json and google_io_2016.json.
  // google_io_2016.json is missing a "create_shortcuts" field, so the default
  // value of false should be used.
  std::vector<web_app::PendingAppManager::AppInfo> test_app_infos;
  {
    web_app::PendingAppManager::AppInfo info(
        GURL("https://www.chromestatus.com/features"),
        web_app::LaunchContainer::kTab,
        web_app::InstallSource::kExternalDefault);
    info.create_shortcuts = true;
    info.require_manifest = true;
    test_app_infos.push_back(std::move(info));
  }
  {
    web_app::PendingAppManager::AppInfo info(
        GURL("https://events.google.com/io2016/?utm_source=web_app_manifest"),
        web_app::LaunchContainer::kWindow,
        web_app::InstallSource::kExternalDefault);
    info.create_shortcuts = false;
    info.require_manifest = true;
    test_app_infos.push_back(std::move(info));
  }

  EXPECT_EQ(test_app_infos.size(), app_infos.size());
  for (const auto app_info : test_app_infos) {
    EXPECT_TRUE(base::ContainsValue(app_infos, app_info));
  }
}

TEST_F(ScanDirForExternalWebAppsTest, BadJson) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("bad_json"));

  // The bad_json directory contains one (malformed) JSON file.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, TxtButNoJson) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("txt_but_no_json"));

  // The txt_but_no_json directory contains one file, and the contents of that
  // file is valid JSON, but that file's name does not end with ".json".
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, MixedJson) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("mixed_json"));

  // The mixed_json directory contains one empty JSON file, one malformed JSON
  // file and one good JSON file. ScanDirForExternalWebAppsForTesting should
  // still pick up that one good JSON file: polytimer.json.
  EXPECT_EQ(1u, app_infos.size());
  if (app_infos.size() == 1) {
    EXPECT_EQ(app_infos[0].url.spec(),
              std::string("https://polytimer.rocks/?homescreen=1"));
  }
}

TEST_F(ScanDirForExternalWebAppsTest, MissingAppUrl) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("missing_app_url"));

  // The missing_app_url directory contains one JSON file which is correct
  // except for a missing "app_url" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, EmptyAppUrl) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("empty_app_url"));

  // The empty_app_url directory contains one JSON file which is correct
  // except for an empty "app_url" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, InvalidAppUrl) {
  const auto app_infos =
      web_app::ScanDirForExternalWebAppsForTesting(test_dir("invalid_app_url"));

  // The invalid_app_url directory contains one JSON file which is correct
  // except for an invalid "app_url" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, InvalidCreateShortcuts) {
  const auto app_infos = web_app::ScanDirForExternalWebAppsForTesting(
      test_dir("invalid_create_shortcuts"));

  // The invalid_create_shortcuts directory contains one JSON file which is
  // correct except for an invalid "create_shortctus" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, MissingLaunchContainer) {
  const auto app_infos = web_app::ScanDirForExternalWebAppsForTesting(
      test_dir("missing_launch_container"));

  // The missing_launch_container directory contains one JSON file which is
  // correct except for a missing "launch_container" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, InvalidLaunchContainer) {
  const auto app_infos = web_app::ScanDirForExternalWebAppsForTesting(
      test_dir("invalid_launch_container"));

  // The invalidg_launch_container directory contains one JSON file which is
  // correct except for an invalid "launch_container" field.
  EXPECT_EQ(0u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, EnabledByFinch) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      base::Feature{"test_feature_name", base::FEATURE_DISABLED_BY_DEFAULT});
  const auto app_infos = web_app::ScanDirForExternalWebAppsForTesting(
      test_dir("enabled_by_finch"));

  // The enabled_by_finch directory contains two JSON file containing apps
  // that have field trials. As the matching featureis enabled, they should be
  // in our list of apps to install.
  EXPECT_EQ(2u, app_infos.size());
}

TEST_F(ScanDirForExternalWebAppsTest, NotEnabledByFinch) {
  const auto app_infos = web_app::ScanDirForExternalWebAppsForTesting(
      test_dir("enabled_by_finch"));

  // The enabled_by_finch directory contains two JSON file containing apps
  // that have field trials. As the matching featureis enabled, they should not
  // be in our list of apps to install.
  EXPECT_EQ(0u, app_infos.size());
}

#if defined(OS_CHROMEOS)
TEST_F(ScanDirForExternalWebAppsWithProfileTest, ChildUser) {
  const auto profile = CreateProfileAndLogin();
  profile->SetSupervisedUserId(supervised_users::kChildAccountSUID);
  VerifySetOfApps(profile.get(), {GURL(kAppAllUrl), GURL(kAppChildUrl)});
}

TEST_F(ScanDirForExternalWebAppsWithProfileTest, GuestUser) {
  VerifySetOfApps(CreateGuestProfileAndLogin().get(),
                  {GURL(kAppAllUrl), GURL(kAppGuestUrl)});
}

TEST_F(ScanDirForExternalWebAppsWithProfileTest, ManagedUser) {
  const auto profile = CreateProfileAndLogin();
  policy::ProfilePolicyConnectorFactory::GetForBrowserContext(profile.get())
      ->OverrideIsManagedForTesting(true);
  VerifySetOfApps(profile.get(), {GURL(kAppAllUrl), GURL(kAppManagedUrl)});
}

TEST_F(ScanDirForExternalWebAppsWithProfileTest, SupervisedUser) {
  const auto profile = CreateProfileAndLogin();
  profile->SetSupervisedUserId("asdf");
  VerifySetOfApps(profile.get(), {GURL(kAppAllUrl), GURL(kAppSupervisedUrl)});
}

TEST_F(ScanDirForExternalWebAppsWithProfileTest, UnmanagedUser) {
  VerifySetOfApps(CreateProfileAndLogin().get(),
                  {GURL(kAppAllUrl), GURL(kAppUnmanagedUrl)});
}

TEST_F(ScanDirForExternalWebAppsWithProfileTest, NonPrimaryProfile) {
  EXPECT_TRUE(
      ScanApps(CreateProfile().get(), test_dir(kUserTypesTestDir)).empty());
}
#else
// No app is expected for non-ChromeOS builds.
TEST_F(ScanDirForExternalWebAppsWithProfileTest, NoApp) {
  EXPECT_TRUE(
      ScanApps(CreateProfile().get(), test_dir(kUserTypesTestDir)).empty());
}
#endif
