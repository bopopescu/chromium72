// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/manifest.h"
#include "extensions/common/permissions/permissions_data.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace extensions {

namespace {
constexpr int kTabId = 42;
}

TEST(ComponentExtensionUrlPattern, AllUrls) {
  // Component extensions do not have access to "chrome" scheme URLs through
  // the "<all_urls>" meta-pattern.
  auto all_urls = ExtensionBuilder("all urls")
                      .AddPermission("<all_urls>")
                      .SetLocation(Manifest::COMPONENT)
                      .Build();
  std::string error;
  EXPECT_FALSE(all_urls->permissions_data()->CanAccessPage(
      GURL("chrome://settings"), kTabId, &error))
      << error;
  // Non-chrome scheme should be fine.
  EXPECT_TRUE(all_urls->permissions_data()->CanAccessPage(
      GURL("https://example.com"), kTabId, &error))
      << error;
}

TEST(ComponentExtensionUrlPattern, ChromeVoxExtension) {
  // The ChromeVox extension has access to "chrome" scheme URLs through the
  // "<all_urls>" meta-pattern because it's whitelisted.
  auto all_urls = ExtensionBuilder("all urls")
                      .AddPermission("<all_urls>")
                      .SetLocation(Manifest::COMPONENT)
                      .SetID(extension_misc::kChromeVoxExtensionId)
                      .Build();
  std::string error;
  EXPECT_TRUE(all_urls->permissions_data()->CanAccessPage(
      GURL("chrome://settings"), kTabId, &error))
      << error;
}

TEST(ComponentExtensionUrlPattern, ExplicitChromeUrl) {
  // Explicitly specifying a pattern that allows access to the chrome
  // scheme is OK.
  auto chrome_urls = ExtensionBuilder("chrome urls")
                         .AddPermission("chrome://*/*")
                         .SetLocation(Manifest::COMPONENT)
                         .Build();
  std::string error;
  EXPECT_TRUE(chrome_urls->permissions_data()->CanAccessPage(
      GURL("chrome://settings"), kTabId, &error))
      << error;
}

}  // namespace extensions
