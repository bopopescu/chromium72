// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/preload_check.h"

#include "extensions/common/extension.h"

namespace extensions {

PreloadCheck::PreloadCheck(scoped_refptr<const Extension> extension)
    : extension_(extension) {}

PreloadCheck::~PreloadCheck() {}

base::string16 PreloadCheck::GetErrorMessage() const {
  return base::string16();
}

}  // namespace extensions
