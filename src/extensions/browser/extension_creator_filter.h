// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_CREATOR_FILTER_H_
#define EXTENSIONS_BROWSER_EXTENSION_CREATOR_FILTER_H_

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"

namespace extensions {

// Determines which files should be included in a packaged extension.
// Designed specifically to operate with the callback in chrome/common/zip.
class ExtensionCreatorFilter : public base::RefCounted<ExtensionCreatorFilter> {
 public:
  ExtensionCreatorFilter(const base::FilePath& extension_dir);

  // Returns true if the given |file_path| should be included in a packed
  // extension.
  bool ShouldPackageFile(const base::FilePath& file_path);

 private:
  friend class base::RefCounted<ExtensionCreatorFilter>;
  ~ExtensionCreatorFilter() {}

  const base::FilePath reserved_metadata_dir_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionCreatorFilter);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_CREATOR_FILTER_H_
