// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/smb_client/smb_service_helper.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace smb_client {

class SmbServiceHelperTest : public ::testing::Test {
 public:
  SmbServiceHelperTest() = default;
  ~SmbServiceHelperTest() override = default;

 protected:
  // Helpers for ParseUserPrincipleName.
  std::string user_name_;
  std::string realm_;

  bool ParseUserPrincipalName(const char* user_principal_name_) {
    return ::chromeos::smb_client::ParseUserPrincipalName(user_principal_name_,
                                                          &user_name_, &realm_);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SmbServiceHelperTest);
};

// a@b.c succeeds.
TEST_F(SmbServiceHelperTest, ParseUPNSuccess) {
  EXPECT_TRUE(ParseUserPrincipalName("user@domain.com"));
  EXPECT_EQ(user_name_, "user");
  EXPECT_EQ(realm_, "DOMAIN.COM");
}

// a@b.c.d.e succeeds.
TEST_F(SmbServiceHelperTest, ParseUPNSuccess_Long) {
  EXPECT_TRUE(ParseUserPrincipalName("user@a.domain.company.com"));
  EXPECT_EQ(user_name_, "user");
  EXPECT_EQ(realm_, "A.DOMAIN.COMPANY.COM");
}

// Capitalization works as expected.
TEST_F(SmbServiceHelperTest, ParseUPNSuccess_MixedCaps) {
  EXPECT_TRUE(ParseUserPrincipalName("UsEr@CoMPaNy.DOMain.com"));
  EXPECT_EQ(user_name_, "UsEr");
  EXPECT_EQ(realm_, "COMPANY.DOMAIN.COM");
}

// a.b@c.d succeeds, even though it is invalid (rejected by kinit).
TEST_F(SmbServiceHelperTest, ParseUPNSuccess_DotAtDot) {
  EXPECT_TRUE(ParseUserPrincipalName("user.team@domain.com"));
  EXPECT_EQ(user_name_, "user.team");
  EXPECT_EQ(realm_, "DOMAIN.COM");
}

// a@ fails (no workgroup.domain).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoRealm) {
  EXPECT_FALSE(ParseUserPrincipalName("user@"));
}

// a fails (no @workgroup.domain).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoAtRealm) {
  EXPECT_FALSE(ParseUserPrincipalName("user"));
}

// a. fails (no @workgroup.domain and trailing . is invalid, anyway).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoAtRealmButDot) {
  EXPECT_FALSE(ParseUserPrincipalName("user."));
}

// a@b@c fails (double at).
TEST_F(SmbServiceHelperTest, ParseUPNFail_AtAt) {
  EXPECT_FALSE(ParseUserPrincipalName("user@company@domain"));
}

// a@b@c fails (double at).
TEST_F(SmbServiceHelperTest, ParseUPNFail_AtAtDot) {
  EXPECT_FALSE(ParseUserPrincipalName("user@company@domain.com"));
}

// @b.c fails (empty user name).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoUpn) {
  EXPECT_FALSE(ParseUserPrincipalName("@company.domain"));
}

// b.c fails (no user name@).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoUpnAt) {
  EXPECT_FALSE(ParseUserPrincipalName("company.domain"));
}

// .b.c fails (no user name@ and initial . is invalid, anyway).
TEST_F(SmbServiceHelperTest, ParseUPNFail_NoUpnAtButDot) {
  EXPECT_FALSE(ParseUserPrincipalName(".company.domain"));
}

}  // namespace smb_client
}  // namespace chromeos
