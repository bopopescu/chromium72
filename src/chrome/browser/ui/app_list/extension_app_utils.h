// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_UTILS_H_
#define CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_UTILS_H_

#include <string>

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;
}

namespace app_list {

bool ShouldShowInLauncher(const extensions::Extension* extension,
                          content::BrowserContext* context);
bool HideInLauncherById(std::string extension_id);

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_UTILS_H_
