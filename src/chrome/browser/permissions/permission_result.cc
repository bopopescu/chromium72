// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/permission_result.h"

PermissionResult::PermissionResult(ContentSetting cs,
                                   PermissionStatusSource pss)
    : content_setting(cs), source(pss) {}

PermissionResult::~PermissionResult() {}
