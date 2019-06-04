// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohi.h>

#include "base/base64.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager_factory.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/scoped_testing_local_state.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/user_manager/scoped_user_manager.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "crypto/scoped_test_nss_chromeos_user.h"
#include "crypto/scoped_test_system_nss_key_slot.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace {

// User that is associated with test user profile.
const char kTestUserId[] = "user_id@somewhere.com";

// Public part of the RSA key pair used as the RSA key pair associated with
// test user's Easy Unlock service.
const char kTestPublicKey[] = {
  0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
  0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00,
  0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xcb, 0x5a, 0x8d,
  0x34, 0xa2, 0xe3, 0x43, 0x16, 0x94, 0x8d, 0xce, 0xa9, 0x92, 0xb0, 0x35,
  0x5a, 0x34, 0x50, 0xd4, 0x7f, 0x14, 0x1f, 0xa3, 0x8e, 0x48, 0x2c, 0x42,
  0xe8, 0xe4, 0xf6, 0x38, 0x5a, 0xdf, 0x08, 0x6b, 0x0e, 0x78, 0xc9, 0xfc,
  0x72, 0x03, 0xb8, 0xd2, 0x75, 0x1d, 0x56, 0x8f, 0x6d, 0x8d, 0xe2, 0x65,
  0x3b, 0x66, 0xbb, 0x66, 0xe3, 0x3a, 0x00, 0xc1, 0x4a, 0xe2, 0xf2, 0xc8,
  0x2d, 0x95, 0x74, 0x5b, 0x65, 0xaa, 0xfd, 0xe1, 0x11, 0xf9, 0x9e, 0x73,
  0x3d, 0x96, 0xb5, 0xae, 0x19, 0x03, 0x74, 0x0f, 0xfa, 0xbd, 0x52, 0x72,
  0x83, 0x08, 0x1e, 0x53, 0x08, 0x30, 0xb6, 0xd3, 0xef, 0x4b, 0x2d, 0x65,
  0x3c, 0x7d, 0xba, 0x55, 0xfe, 0x7d, 0x1c, 0xc5, 0xf1, 0x4e, 0x9c, 0xae,
  0x27, 0xe2, 0x1b, 0x42, 0x2c, 0xd9, 0x6a, 0x81, 0x6c, 0x51, 0x2d, 0x7b,
  0x7d, 0x28, 0xe3, 0xab, 0xaf, 0x30, 0x33, 0xd1, 0x46, 0xd1, 0xbe, 0x62,
  0x2e, 0xd5, 0xfd, 0x32, 0x68, 0xb6, 0xe2, 0x95, 0x59, 0x6e, 0x69, 0xe9,
  0x9c, 0x24, 0xf7, 0x71, 0xde, 0x5f, 0xd5, 0xc5, 0x8a, 0x71, 0xb3, 0x65,
  0x77, 0xf9, 0x29, 0xf3, 0xce, 0x0a, 0x00, 0xca, 0xd7, 0xf9, 0x2e, 0x45,
  0x04, 0xb5, 0x68, 0x1f, 0xfe, 0x4e, 0xac, 0xdd, 0xaa, 0xc5, 0x24, 0x6e,
  0xec, 0x63, 0x36, 0x5f, 0xb9, 0x94, 0x0c, 0x7c, 0xf3, 0xcf, 0xa9, 0x44,
  0x80, 0x99, 0x13, 0x89, 0x68, 0xbc, 0x6c, 0xfb, 0xe7, 0x2c, 0x94, 0x2e,
  0x99, 0x31, 0xf1, 0x02, 0xd7, 0x27, 0xaf, 0xae, 0x69, 0xa9, 0x95, 0xd5,
  0xf2, 0x6a, 0x6c, 0x46, 0x95, 0xdb, 0x30, 0xc9, 0x9f, 0xbe, 0xa0, 0x71,
  0xc9, 0x74, 0xfb, 0xbb, 0x0a, 0x8b, 0xa1, 0x7c, 0x1a, 0xdf, 0xa3, 0xb2,
  0x18, 0x29, 0xe5, 0xf6, 0x94, 0x9f, 0xa3, 0x50, 0x11, 0x4b, 0xfe, 0x05,
  0xcb, 0x02, 0x03, 0x01, 0x00, 0x01
};

// Private part of the RSA key pair used as the RSA key pair associated with
// test user's Easy Unlock service.
const unsigned char kTestPrivateKey[] = {
  0x30, 0x82, 0x04, 0xbf, 0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a,
  0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x04, 0x82,
  0x04, 0xa9, 0x30, 0x82, 0x04, 0xa5, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01,
  0x01, 0x00, 0xcb, 0x5a, 0x8d, 0x34, 0xa2, 0xe3, 0x43, 0x16, 0x94, 0x8d,
  0xce, 0xa9, 0x92, 0xb0, 0x35, 0x5a, 0x34, 0x50, 0xd4, 0x7f, 0x14, 0x1f,
  0xa3, 0x8e, 0x48, 0x2c, 0x42, 0xe8, 0xe4, 0xf6, 0x38, 0x5a, 0xdf, 0x08,
  0x6b, 0x0e, 0x78, 0xc9, 0xfc, 0x72, 0x03, 0xb8, 0xd2, 0x75, 0x1d, 0x56,
  0x8f, 0x6d, 0x8d, 0xe2, 0x65, 0x3b, 0x66, 0xbb, 0x66, 0xe3, 0x3a, 0x00,
  0xc1, 0x4a, 0xe2, 0xf2, 0xc8, 0x2d, 0x95, 0x74, 0x5b, 0x65, 0xaa, 0xfd,
  0xe1, 0x11, 0xf9, 0x9e, 0x73, 0x3d, 0x96, 0xb5, 0xae, 0x19, 0x03, 0x74,
  0x0f, 0xfa, 0xbd, 0x52, 0x72, 0x83, 0x08, 0x1e, 0x53, 0x08, 0x30, 0xb6,
  0xd3, 0xef, 0x4b, 0x2d, 0x65, 0x3c, 0x7d, 0xba, 0x55, 0xfe, 0x7d, 0x1c,
  0xc5, 0xf1, 0x4e, 0x9c, 0xae, 0x27, 0xe2, 0x1b, 0x42, 0x2c, 0xd9, 0x6a,
  0x81, 0x6c, 0x51, 0x2d, 0x7b, 0x7d, 0x28, 0xe3, 0xab, 0xaf, 0x30, 0x33,
  0xd1, 0x46, 0xd1, 0xbe, 0x62, 0x2e, 0xd5, 0xfd, 0x32, 0x68, 0xb6, 0xe2,
  0x95, 0x59, 0x6e, 0x69, 0xe9, 0x9c, 0x24, 0xf7, 0x71, 0xde, 0x5f, 0xd5,
  0xc5, 0x8a, 0x71, 0xb3, 0x65, 0x77, 0xf9, 0x29, 0xf3, 0xce, 0x0a, 0x00,
  0xca, 0xd7, 0xf9, 0x2e, 0x45, 0x04, 0xb5, 0x68, 0x1f, 0xfe, 0x4e, 0xac,
  0xdd, 0xaa, 0xc5, 0x24, 0x6e, 0xec, 0x63, 0x36, 0x5f, 0xb9, 0x94, 0x0c,
  0x7c, 0xf3, 0xcf, 0xa9, 0x44, 0x80, 0x99, 0x13, 0x89, 0x68, 0xbc, 0x6c,
  0xfb, 0xe7, 0x2c, 0x94, 0x2e, 0x99, 0x31, 0xf1, 0x02, 0xd7, 0x27, 0xaf,
  0xae, 0x69, 0xa9, 0x95, 0xd5, 0xf2, 0x6a, 0x6c, 0x46, 0x95, 0xdb, 0x30,
  0xc9, 0x9f, 0xbe, 0xa0, 0x71, 0xc9, 0x74, 0xfb, 0xbb, 0x0a, 0x8b, 0xa1,
  0x7c, 0x1a, 0xdf, 0xa3, 0xb2, 0x18, 0x29, 0xe5, 0xf6, 0x94, 0x9f, 0xa3,
  0x50, 0x11, 0x4b, 0xfe, 0x05, 0xcb, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02,
  0x82, 0x01, 0x01, 0x00, 0xc3, 0xec, 0x1c, 0x7c, 0x08, 0x2b, 0xf9, 0xa9,
  0x39, 0xbb, 0x5e, 0xcf, 0x96, 0x1a, 0xdb, 0x6c, 0x6b, 0x57, 0x2d, 0x44,
  0xba, 0x78, 0xb9, 0x36, 0x0e, 0x67, 0x46, 0x97, 0xe8, 0x71, 0x29, 0x5e,
  0xb3, 0xe0, 0x02, 0x75, 0x50, 0xff, 0x1a, 0x90, 0x26, 0xf1, 0xdd, 0x23,
  0x24, 0xff, 0x0e, 0xf5, 0x38, 0x6c, 0x55, 0xa8, 0x63, 0x94, 0x4e, 0xce,
  0xc2, 0x45, 0x93, 0xf5, 0xb8, 0xae, 0xbd, 0x1a, 0xde, 0x11, 0xdb, 0x35,
  0x1b, 0x07, 0xbb, 0xdf, 0x7b, 0xa6, 0xa3, 0xd5, 0x44, 0xed, 0x0a, 0x2d,
  0xe3, 0x5b, 0xe1, 0x41, 0x6d, 0x42, 0x90, 0x3d, 0x9a, 0x86, 0xcc, 0xec,
  0xe9, 0x32, 0x5d, 0x03, 0x02, 0x65, 0x5c, 0x52, 0x69, 0xb8, 0x2b, 0xbe,
  0x23, 0x80, 0xa3, 0x5d, 0x98, 0xa5, 0xf0, 0x4d, 0x50, 0xd5, 0x7e, 0x6e,
  0x83, 0x92, 0xcf, 0xdb, 0x32, 0x63, 0x25, 0xc5, 0x32, 0xae, 0x17, 0xeb,
  0xda, 0x81, 0xa8, 0xcc, 0x37, 0x6a, 0xdb, 0x3b, 0xe1, 0x48, 0x5f, 0xfc,
  0x31, 0x98, 0x49, 0x53, 0x2a, 0xf0, 0x71, 0x67, 0x52, 0xdc, 0x01, 0x07,
  0x3e, 0xb1, 0x7f, 0xb1, 0xc4, 0x1a, 0x23, 0x3d, 0x7a, 0x94, 0x63, 0xb4,
  0xb6, 0x9a, 0xa3, 0x7e, 0x8d, 0x4a, 0xba, 0x9c, 0x88, 0xfd, 0xd2, 0x2e,
  0x32, 0x5f, 0xa6, 0x2c, 0xf4, 0xc8, 0x54, 0xa5, 0x7a, 0x5a, 0x02, 0x0c,
  0x80, 0xa4, 0x8b, 0x6c, 0x4a, 0xda, 0x00, 0x62, 0x77, 0xd9, 0x49, 0x11,
  0xae, 0xe5, 0x51, 0xc4, 0x54, 0x68, 0xe9, 0xbd, 0x9f, 0x95, 0x8f, 0x1b,
  0xb7, 0x0b, 0x25, 0x6b, 0xe7, 0x32, 0x55, 0x92, 0xb0, 0x0f, 0x10, 0xe2,
  0xc6, 0xef, 0x5f, 0xe1, 0x54, 0xdb, 0xe0, 0x2d, 0x59, 0xe4, 0xc0, 0x92,
  0x60, 0x5b, 0x25, 0xb0, 0x33, 0x1a, 0x6b, 0xa4, 0x03, 0xd2, 0xd2, 0x3b,
  0x09, 0xd6, 0xc4, 0x4e, 0xde, 0x09, 0xdc, 0x81, 0x02, 0x81, 0x81, 0x00,
  0xf9, 0xf8, 0x73, 0x97, 0x11, 0xfd, 0x87, 0x76, 0xd3, 0x70, 0x38, 0xbe,
  0x17, 0xe0, 0xdc, 0x55, 0x11, 0x95, 0x83, 0xa7, 0x4e, 0xbc, 0x8c, 0xe9,
  0x59, 0xe4, 0x64, 0xa2, 0xd3, 0xc9, 0xd9, 0x48, 0x1a, 0xe7, 0x96, 0x2c,
  0xc5, 0x21, 0x87, 0x77, 0x9e, 0x43, 0xaa, 0xdf, 0x26, 0x96, 0x22, 0xc2,
  0x14, 0x01, 0xbf, 0x56, 0xda, 0xe9, 0x36, 0xf5, 0x06, 0xa9, 0x0f, 0x9a,
  0xae, 0x86, 0xc6, 0x73, 0x8f, 0x46, 0x48, 0x8e, 0x5d, 0x17, 0xb7, 0xff,
  0x24, 0x85, 0x3a, 0xac, 0x9e, 0x65, 0x04, 0xc9, 0x24, 0x47, 0x05, 0xfa,
  0xf2, 0xda, 0x19, 0xd4, 0x39, 0x89, 0x29, 0xc7, 0x12, 0xb0, 0x89, 0x1a,
  0x96, 0x46, 0x79, 0x28, 0x80, 0x6d, 0xb8, 0xb6, 0x9a, 0x2e, 0x36, 0xa6,
  0x5a, 0xc5, 0x98, 0xfb, 0x26, 0xe7, 0xbc, 0xe3, 0x9f, 0xd5, 0x07, 0x0d,
  0xb0, 0xcd, 0x55, 0xe6, 0x1b, 0x1e, 0x16, 0x2b, 0x02, 0x81, 0x81, 0x00,
  0xd0, 0x42, 0x3e, 0xb6, 0x35, 0xf7, 0x40, 0xc7, 0xb4, 0xd1, 0xd0, 0x23,
  0xb5, 0xcc, 0x61, 0x42, 0x3b, 0x21, 0xa8, 0x19, 0x9d, 0xea, 0xf5, 0x8c,
  0xf2, 0xf7, 0x2a, 0xa4, 0xee, 0x81, 0x50, 0x16, 0x38, 0x99, 0x50, 0x72,
  0xe6, 0xf5, 0xae, 0xf1, 0x11, 0x6c, 0x08, 0xa1, 0x7e, 0x34, 0x3e, 0xea,
  0x6d, 0x21, 0x29, 0xad, 0x72, 0x8a, 0xa5, 0x4e, 0x0a, 0x21, 0x0b, 0x3c,
  0x32, 0xd9, 0xce, 0xdb, 0x2c, 0x5f, 0x88, 0x6f, 0x8f, 0xc1, 0x76, 0xcb,
  0x32, 0xb2, 0x4a, 0x99, 0x8a, 0x43, 0x3f, 0x7c, 0x30, 0x10, 0x80, 0xd9,
  0x3a, 0xd8, 0xf3, 0xa5, 0x5e, 0x69, 0x7f, 0x76, 0x3c, 0x79, 0x0e, 0xf5,
  0x5c, 0xdc, 0x14, 0x8a, 0x22, 0x1c, 0xdf, 0xb6, 0xfb, 0x95, 0xfc, 0xa1,
  0x7d, 0x29, 0xee, 0xee, 0xce, 0x82, 0xfd, 0xbb, 0xdc, 0x0e, 0xd9, 0xfb,
  0x99, 0xca, 0xe2, 0x48, 0x2d, 0x9a, 0x9e, 0xe1, 0x02, 0x81, 0x81, 0x00,
  0xbf, 0x5c, 0x97, 0x48, 0xd0, 0x89, 0xf1, 0x39, 0x63, 0x56, 0x66, 0xea,
  0x07, 0xa7, 0xa9, 0xa5, 0x2a, 0x27, 0xf6, 0xb8, 0x8f, 0x4b, 0x42, 0xe8,
  0xa5, 0x5b, 0x76, 0x3b, 0x3c, 0xbd, 0x2a, 0xac, 0xcb, 0x83, 0xfc, 0xf0,
  0x5b, 0x1d, 0x76, 0xf2, 0x78, 0xe3, 0x3e, 0x9d, 0x44, 0x91, 0xed, 0x1b,
  0xfc, 0x6a, 0xf6, 0x0a, 0xcc, 0xdd, 0x7a, 0xa8, 0x0b, 0xa8, 0x42, 0xfc,
  0xdc, 0x9c, 0xea, 0xb1, 0xae, 0xbe, 0x54, 0x6f, 0x40, 0x0f, 0x17, 0x59,
  0xa8, 0xa0, 0xa1, 0xb1, 0x62, 0x34, 0xdd, 0x7c, 0x0a, 0x5c, 0xa0, 0xd4,
  0x63, 0x33, 0xda, 0x50, 0x20, 0x97, 0xc3, 0xb6, 0xd5, 0xb4, 0xf5, 0xd0,
  0xb7, 0xb8, 0x4d, 0xaa, 0x56, 0xdf, 0x28, 0x68, 0x0a, 0x12, 0x54, 0xdd,
  0xf7, 0x61, 0x8b, 0xe2, 0xc0, 0xfe, 0xe9, 0x18, 0xac, 0xd4, 0x4d, 0x69,
  0x0a, 0xaf, 0xb7, 0x11, 0xc8, 0x32, 0xb9, 0x2f, 0x02, 0x81, 0x81, 0x00,
  0xce, 0x5f, 0xd7, 0x25, 0x59, 0x75, 0x1b, 0x8c, 0xcb, 0x72, 0xdf, 0x7f,
  0x83, 0xb8, 0x74, 0xe8, 0xdd, 0x10, 0x0d, 0x34, 0xd5, 0x78, 0xf0, 0xbc,
  0x2c, 0x49, 0x22, 0xc9, 0x2e, 0x50, 0x96, 0xbc, 0x6e, 0x79, 0xff, 0x6e,
  0xdd, 0xd0, 0xb8, 0xfb, 0xca, 0xf7, 0xf3, 0xd5, 0x94, 0xea, 0xd1, 0x2e,
  0x1d, 0xd6, 0xaf, 0x26, 0x62, 0x4b, 0x62, 0x64, 0x63, 0x45, 0x3b, 0x8c,
  0xfc, 0x17, 0x3b, 0x15, 0x96, 0x73, 0x55, 0x10, 0xb8, 0xb4, 0x4e, 0xb4,
  0x2e, 0x18, 0xe0, 0x34, 0x26, 0xff, 0x5c, 0xfa, 0x03, 0xe7, 0x56, 0xc7,
  0xed, 0xb8, 0xf0, 0x38, 0xff, 0xc6, 0x2b, 0xb9, 0x4f, 0x53, 0xe7, 0xae,
  0xdd, 0xc6, 0x79, 0xd4, 0x28, 0xd9, 0xd4, 0x17, 0xd0, 0x58, 0x61, 0x70,
  0xe6, 0x47, 0x97, 0xae, 0xae, 0x96, 0xc0, 0x3a, 0x59, 0x67, 0x9e, 0x3b,
  0xe5, 0xbb, 0x57, 0x61, 0x8f, 0x4f, 0x9a, 0x01, 0x02, 0x81, 0x80, 0x57,
  0xfa, 0x17, 0x0a, 0x87, 0x99, 0xdb, 0x93, 0x94, 0x10, 0x3f, 0xa9, 0xbf,
  0xa4, 0x02, 0x7d, 0xf1, 0x04, 0x41, 0x08, 0x68, 0x6a, 0x9b, 0x79, 0xfd,
  0xf4, 0x10, 0x8f, 0xc8, 0xdd, 0xfe, 0x0c, 0xc7, 0xc3, 0x42, 0x2d, 0xac,
  0x80, 0xc3, 0xfb, 0xa0, 0x8d, 0x31, 0x9b, 0x5b, 0xf8, 0xbb, 0x8c, 0x05,
  0x56, 0xaf, 0x49, 0xca, 0xa9, 0xe5, 0xab, 0x80, 0xa9, 0x39, 0xfd, 0xd4,
  0x78, 0x45, 0xaa, 0xd6, 0x27, 0x91, 0xa8, 0x76, 0x01, 0x98, 0xf0, 0x4f,
  0x48, 0xe0, 0x4e, 0x53, 0x53, 0x55, 0x5e, 0x2b, 0x1a, 0x4f, 0x00, 0x62,
  0xe4, 0x0e, 0x05, 0xa0, 0x94, 0x6e, 0xff, 0xd4, 0x13, 0x6f, 0x2e, 0x7c,
  0x08, 0x92, 0x20, 0xa9, 0x29, 0xee, 0xb0, 0xbb, 0x14, 0x52, 0xf9, 0x6d,
  0x28, 0xd9, 0xbd, 0x84, 0x11, 0x43, 0x71, 0xc2, 0x60, 0x69, 0xb3, 0x34,
  0xe5, 0xae, 0xd7, 0x75, 0x4c, 0xb8, 0x3d, 0x0a
};

// Closure that fail the test if it's called.
void ExpectNotCalledCallback() {
  ADD_FAILURE() << "Not reached";
}

// Used to track how may |EasyUnlockTpmKeyManager::PrepareTpmKey| callbacks
// have been called. It increases |*count| by 1.
void IncreaseCount(int* count) {
  ++(*count);
}

// Sets |*result| to |value| and runs |callback|.
// Used as a callback to EasyUnlockTpmKeyManager::SignUsingTpmKey in tests.
void RecordStringAndRunClosure(std::string* result,
                               const base::Closure& callback,
                               const std::string& value) {
  *result = value;
  callback.Run();
}

class EasyUnlockTpmKeyManagerTest : public testing::Test {
 public:
  EasyUnlockTpmKeyManagerTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
        user_manager_(new FakeChromeUserManager()),
        user_manager_enabler_(base::WrapUnique(user_manager_)),
        profile_manager_(TestingBrowserProcess::GetGlobal()) {}
  ~EasyUnlockTpmKeyManagerTest() override {}

  void SetUp() override {
    ASSERT_TRUE(profile_manager_.SetUp());
    const user_manager::User* user = user_manager_->AddUser(test_account_id_);
    username_hash_ = user->username_hash();

    signin_profile_ = profile_manager_.CreateTestingProfile(
        chrome::kInitialProfile,
        std::unique_ptr<sync_preferences::TestingPrefServiceSyncable>(),
        base::UTF8ToUTF16(chrome::kInitialProfile), 0 /* avatar id */,
        std::string() /* supervized user id */,
        TestingProfile::TestingFactories());

    user_profile_ = profile_manager_.CreateTestingProfile(
        test_account_id_.GetUserEmail(),
        std::unique_ptr<sync_preferences::TestingPrefServiceSyncable>(),
        base::UTF8ToUTF16(test_account_id_.GetUserEmail()), 0 /* avatar id */,
        std::string() /* supervized user id */,
        TestingProfile::TestingFactories());
  }

  void TearDown() override {
    if (test_nss_user_)
      ResetTestNssUser();
    profile_manager_.DeleteTestingProfile(test_account_id_.GetUserEmail());
    profile_manager_.DeleteTestingProfile(chrome::kInitialProfile);
  }

  bool InitTestNssUser() {
    bool success = false;
    base::RunLoop run_loop;
    // Has to be done on IO thread due to thread assertions in nss code.
    base::PostTaskWithTraitsAndReply(
        FROM_HERE, {content::BrowserThread::IO},
        base::BindOnce(&EasyUnlockTpmKeyManagerTest::InitTestNssUserOnIOThread,
                       base::Unretained(this), base::Unretained(&success)),
        run_loop.QuitClosure());
    run_loop.Run();
    return success;
  }

  void InitTestNssUserOnIOThread(bool* success) {
    test_nss_user_.reset(new crypto::ScopedTestNSSChromeOSUser(username_hash_));
    *success = test_nss_user_->constructed_successfully();
  }

  // Verifies that easy sign-in TPM key generation does not start before user
  // TPM is completely done, then finalizes user TPM initialization.
  // Note that easy sign-in key generation should not start before TPM is
  // initialized in order to prevent TPM initialization from blocking IO thread
  // while waiting for TPM lock (taken for key creation) to be released.
  void VerifyKeyGenerationNotStartedAndFinalizeTestNssUser() {
    EXPECT_FALSE(user_key_manager()->StartedCreatingTpmKeys());

    base::RunLoop run_loop;
    // Has to be done on IO thread due to thread assertions in nss code.
    base::PostTaskWithTraitsAndReply(
        FROM_HERE, {content::BrowserThread::IO},
        base::BindOnce(
            &EasyUnlockTpmKeyManagerTest::FinalizeTestNssUserOnIOThread,
            base::Unretained(this)),
        run_loop.QuitClosure());
    run_loop.Run();
  }

  void FinalizeTestNssUserOnIOThread() { test_nss_user_->FinishInit(); }

  void ResetTestNssUser() {
    base::RunLoop run_loop;
    // Has to be done on IO thread due to thread assertions in nss code.
    base::PostTaskWithTraitsAndReply(
        FROM_HERE, {content::BrowserThread::IO},
        base::BindOnce(&EasyUnlockTpmKeyManagerTest::ResetTestNssUserOnIOThread,
                       base::Unretained(this)),
        run_loop.QuitClosure());
    run_loop.Run();
  }

  void ResetTestNssUserOnIOThread() { test_nss_user_.reset(); }

  // Creates and sets test system NSS key slot.
  bool SetUpTestSystemSlot() {
    test_system_slot_.reset(new crypto::ScopedTestSystemNSSKeySlot());
    return test_system_slot_->ConstructedSuccessfully();
  }

  // Imports a private RSA key to the test system slot.
  // It returns whether the key has been imported. In order for the method to
  // succeed, the test system slot must have been set up
  // (using |SetUpTestSystemSlot|).
  bool ImportPrivateKey(const unsigned char* key, int key_size) {
    if (!test_system_slot_ || !test_system_slot_->slot()) {
      LOG(ERROR) << "System slot not initialized.";
      return false;
    }

    SECItem pki_der_user = {
      siBuffer,
      // NSS requires non-const data even though it is just for input.
      const_cast<unsigned char*>(key),
      key_size
    };

    return SECSuccess == PK11_ImportDERPrivateKeyInfo(test_system_slot_->slot(),
                                                      &pki_der_user,
                                                      NULL,    // nickname
                                                      NULL,    // publicValue
                                                      true,    // isPerm
                                                      true,    // isPrivate
                                                      KU_ALL,  // usage
                                                      NULL);
  }

  // Returns EasyUnlockTPMKeyManager for user profile.
  EasyUnlockTpmKeyManager* user_key_manager() {
    return EasyUnlockTpmKeyManagerFactory::GetInstance()->Get(user_profile_);
  }

  // Returns EasyUnlockTPMKeyManager for signin profile.
  EasyUnlockTpmKeyManager* signin_key_manager() {
    return EasyUnlockTpmKeyManagerFactory::GetInstance()->Get(signin_profile_);
  }

  // Sets TPM public key pref in the test user's profile prefs.
  static void SetLocalStatePublicKey(const AccountId& account_id,
                                     const std::string& value) {
    std::string encoded;
    base::Base64Encode(value, &encoded);
    DictionaryPrefUpdate update(g_browser_process->local_state(),
                                prefs::kEasyUnlockLocalStateTpmKeys);
    update->SetKey(account_id.GetUserEmail(), base::Value(encoded));
  }

 protected:
  const AccountId test_account_id_ = AccountId::FromUserEmail(kTestUserId);

 private:
  content::TestBrowserThreadBundle thread_bundle_;

  // The NSS system slot used by EasyUnlockTPMKeyManagers in tests.
  std::unique_ptr<crypto::ScopedTestSystemNSSKeySlot> test_system_slot_;
  std::unique_ptr<crypto::ScopedTestNSSChromeOSUser> test_nss_user_;

  // Needed to properly set up signin and user profiles for test.
  FakeChromeUserManager* user_manager_;
  user_manager::ScopedUserManager user_manager_enabler_;
  TestingProfileManager profile_manager_;

  // The testing profiles that own EasyUnlockTPMKeyManager services.
  // Owned by |profile_manager_|.
  TestingProfile* user_profile_;
  TestingProfile* signin_profile_;

  // The test user's username hash.
  std::string username_hash_;

  DISALLOW_COPY_AND_ASSIGN(EasyUnlockTpmKeyManagerTest);
};

TEST_F(EasyUnlockTpmKeyManagerTest, CreateKeyPair) {
  ASSERT_TRUE(InitTestNssUser());

  base::RunLoop run_loop;
  EXPECT_TRUE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_TRUE(signin_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop.QuitClosure()));
  EXPECT_TRUE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());

  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();
  run_loop.Run();

  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            signin_key_manager()->GetPublicTpmKey(test_account_id_));

  EXPECT_TRUE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */, base::Bind(&ExpectNotCalledCallback)));
}

TEST_F(EasyUnlockTpmKeyManagerTest, CreateKeyPairMultipleCallbacks) {
  ASSERT_TRUE(InitTestNssUser());

  int callback_count = 0;
  base::RunLoop run_loop;

  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop.QuitClosure()));
  EXPECT_FALSE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */,
      base::Bind(&IncreaseCount, &callback_count)));
  EXPECT_FALSE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */,
      base::Bind(&IncreaseCount, &callback_count)));
  // Verify that the method works with empty callback.
  EXPECT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 base::Closure()));

  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();
  EXPECT_EQ(0, callback_count);

  run_loop.Run();

  EXPECT_EQ(2, callback_count);
  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            signin_key_manager()->GetPublicTpmKey(test_account_id_));

  EXPECT_TRUE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */, base::Bind(&ExpectNotCalledCallback)));
}

TEST_F(EasyUnlockTpmKeyManagerTest, PublicKeySetInPrefs) {
  SetLocalStatePublicKey(
      test_account_id_, std::string(kTestPublicKey, arraysize(kTestPublicKey)));

  EXPECT_TRUE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */, base::Bind(&ExpectNotCalledCallback)));

  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            std::string(kTestPublicKey, arraysize(kTestPublicKey)));
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            signin_key_manager()->GetPublicTpmKey(test_account_id_));
}

TEST_F(EasyUnlockTpmKeyManagerTest, PublicKeySetInPrefsCheckPrivateKey) {
  ASSERT_TRUE(InitTestNssUser());

  SetLocalStatePublicKey(
      test_account_id_, std::string(kTestPublicKey, arraysize(kTestPublicKey)));

  base::RunLoop run_loop;
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(true /* check_private_key */,
                                                 run_loop.QuitClosure()));

  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();
  run_loop.Run();

  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_NE(user_key_manager()->GetPublicTpmKey(test_account_id_),
            std::string(kTestPublicKey, arraysize(kTestPublicKey)));
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            signin_key_manager()->GetPublicTpmKey(test_account_id_));
}

TEST_F(EasyUnlockTpmKeyManagerTest, PublicKeySetInPrefsCheckPrivateKey_OK) {
  ASSERT_TRUE(InitTestNssUser());
  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();
  ASSERT_TRUE(ImportPrivateKey(kTestPrivateKey, arraysize(kTestPrivateKey)));
  SetLocalStatePublicKey(
      test_account_id_, std::string(kTestPublicKey, arraysize(kTestPublicKey)));

  int callback_count = 0;
  base::RunLoop run_loop;
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(true /* check_private_key */,
                                                 run_loop.QuitClosure()));

  EXPECT_FALSE(user_key_manager()->PrepareTpmKey(
      false /* check_private_key */,
      base::Bind(&IncreaseCount, &callback_count)));

  run_loop.Run();

  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            std::string(kTestPublicKey, arraysize(kTestPublicKey)));
  EXPECT_EQ(user_key_manager()->GetPublicTpmKey(test_account_id_),
            signin_key_manager()->GetPublicTpmKey(test_account_id_));

  EXPECT_TRUE(user_key_manager()->PrepareTpmKey(
      true /* check_private_key */, base::Bind(&ExpectNotCalledCallback)));
}

TEST_F(EasyUnlockTpmKeyManagerTest, GetSystemSlotTimeoutTriggers) {
  ASSERT_TRUE(InitTestNssUser());

  base::RunLoop run_loop;
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop.QuitClosure()));

  base::RunLoop run_loop_get_slot_timeout;
  ASSERT_TRUE(user_key_manager()->StartGetSystemSlotTimeoutMs(0));
  run_loop_get_slot_timeout.RunUntilIdle();

  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();

  run_loop.Run();

  EXPECT_TRUE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
}

TEST_F(EasyUnlockTpmKeyManagerTest, GetSystemSlotTimeoutAfterSlotFetched) {
  ASSERT_TRUE(InitTestNssUser());
  base::RunLoop run_loop;
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop.QuitClosure()));

  base::RunLoop run_loop_slot;
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();
  ASSERT_TRUE(SetUpTestSystemSlot());
  run_loop_slot.RunUntilIdle();

  ASSERT_FALSE(user_key_manager()->StartGetSystemSlotTimeoutMs(0));

  run_loop.Run();

  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
}

TEST_F(EasyUnlockTpmKeyManagerTest, GetSystemSlotRetryAfterFailure) {
  ASSERT_TRUE(InitTestNssUser());
  base::RunLoop run_loop;
  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop.QuitClosure()));

  base::RunLoop run_loop_get_slot_timeout;
  ASSERT_TRUE(user_key_manager()->StartGetSystemSlotTimeoutMs(0));
  run_loop_get_slot_timeout.RunUntilIdle();

  run_loop.Run();

  EXPECT_TRUE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());

  base::RunLoop run_loop_retry;

  ASSERT_FALSE(user_key_manager()->PrepareTpmKey(false /* check_private_key */,
                                                 run_loop_retry.QuitClosure()));

  ASSERT_TRUE(SetUpTestSystemSlot());
  VerifyKeyGenerationNotStartedAndFinalizeTestNssUser();

  run_loop_retry.Run();

  EXPECT_FALSE(user_key_manager()->GetPublicTpmKey(test_account_id_).empty());
}

TEST_F(EasyUnlockTpmKeyManagerTest, SignData) {
  ASSERT_TRUE(SetUpTestSystemSlot());
  ASSERT_TRUE(ImportPrivateKey(kTestPrivateKey, arraysize(kTestPrivateKey)));
  SetLocalStatePublicKey(
      test_account_id_, std::string(kTestPublicKey, arraysize(kTestPublicKey)));

  base::RunLoop loop;
  std::string signed_data;
  signin_key_manager()->SignUsingTpmKey(
      test_account_id_, "data",
      base::Bind(&RecordStringAndRunClosure, &signed_data, loop.QuitClosure()));
  loop.Run();

  EXPECT_FALSE(signed_data.empty());
}

TEST_F(EasyUnlockTpmKeyManagerTest, SignNoPublicKeySet) {
  base::RunLoop loop;
  std::string signed_data;
  signin_key_manager()->SignUsingTpmKey(
      test_account_id_, "data",
      base::Bind(&RecordStringAndRunClosure, &signed_data, loop.QuitClosure()));
  loop.Run();

  EXPECT_TRUE(signed_data.empty());
}

TEST_F(EasyUnlockTpmKeyManagerTest, SignDataNoPrivateKeyPresent) {
  SetLocalStatePublicKey(
      test_account_id_, std::string(kTestPublicKey, arraysize(kTestPublicKey)));

  base::RunLoop loop;
  std::string signed_data;
  signin_key_manager()->SignUsingTpmKey(
      test_account_id_, "data",
      base::Bind(&RecordStringAndRunClosure, &signed_data, loop.QuitClosure()));

  ASSERT_TRUE(SetUpTestSystemSlot());

  loop.Run();

  EXPECT_TRUE(signed_data.empty());
}

}  // namespace
}  // namespace chromeos
