// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_WAYLAND_GPU_DRM_RENDER_NODE_PATH_FINDER_H_
#define UI_OZONE_PLATFORM_WAYLAND_GPU_DRM_RENDER_NODE_PATH_FINDER_H_

#include "base/files/file_path.h"
#include "base/macros.h"

namespace ui {

// A helper class that finds a DRM render node device and returns a path to it.
class DrmRenderNodePathFinder {
 public:
  DrmRenderNodePathFinder();
  ~DrmRenderNodePathFinder();

  // Returns a path to a drm render node device. If it hasn't been found yet,
  // triggers FindDrmRenderNodePath and returns the path.
  base::FilePath GetDrmRenderNodePath();

 private:
  void FindDrmRenderNodePath();

  base::FilePath drm_render_node_path_;

  DISALLOW_COPY_AND_ASSIGN(DrmRenderNodePathFinder);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_WAYLAND_GPU_DRM_RENDER_NODE_PATH_FINDER_H_
