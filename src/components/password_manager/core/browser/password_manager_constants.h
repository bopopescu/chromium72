// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_MANAGER_CONSTANTS_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_MANAGER_CONSTANTS_H_

#include "base/files/file_path.h"

namespace password_manager {

extern const base::FilePath::CharType kAffiliationDatabaseFileName[];
extern const base::FilePath::CharType kLoginDataFileName[];
#if defined(USE_X11)
extern const base::FilePath::CharType kSecondLoginDataFileName[];
#endif  // defined(USE_X11)

// URL to the password manager account dashboard.
// TODO(crbug.com/862269): remove when "Smart Lock" is completely gone.
extern const char kPasswordManagerAccountDashboardURL[];

// URL to the help center article about Smart Lock;
// TODO(crbug.com/862269): remove when "Smart Lock" is completely gone.
extern const char kPasswordManagerHelpCenterSmartLock[];

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_MANAGER_CONSTANTS_H_
