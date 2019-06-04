// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_VIDEO_ACCELERATOR_GPU_ARC_VIDEO_PROTECTED_BUFFER_ALLOCATOR_H_
#define COMPONENTS_ARC_VIDEO_ACCELERATOR_GPU_ARC_VIDEO_PROTECTED_BUFFER_ALLOCATOR_H_

#include <memory>

#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "components/arc/common/video_protected_buffer_allocator.mojom.h"
#include "mojo/public/cpp/system/handle.h"

namespace arc {
class ProtectedBufferAllocator;
class ProtectedBufferManager;

// GpuArcProtectedBufferAllocator runs in the GPU process.
// It takes allocating/releasing protected buffer request from ARC via IPC
// channel.
// Closing Mojo IPC message pipe to this instance will automatically release
// all buffers allocated for this instance.
class GpuArcVideoProtectedBufferAllocator
    : public mojom::VideoProtectedBufferAllocator {
 public:
  ~GpuArcVideoProtectedBufferAllocator() override;

  static std::unique_ptr<GpuArcVideoProtectedBufferAllocator> Create(
      scoped_refptr<ProtectedBufferManager> protected_buffer_manager);

  // Implementation of mojom::VideoProtectedBufferAllocator
  void AllocateProtectedSharedMemory(
      mojo::ScopedHandle handle_fd,
      uint64_t size,
      AllocateProtectedSharedMemoryCallback callback) override;
  void AllocateProtectedNativePixmap(
      mojo::ScopedHandle handle_fd,
      mojom::HalPixelFormat format,
      const gfx::Size& picture_size,
      AllocateProtectedNativePixmapCallback callback) override;
  void ReleaseProtectedBuffer(mojo::ScopedHandle handle_fd) override;

 private:
  explicit GpuArcVideoProtectedBufferAllocator(
      std::unique_ptr<ProtectedBufferAllocator> protected_buffer_allocator);

  const std::unique_ptr<ProtectedBufferAllocator> protected_buffer_allocator_;

  THREAD_CHECKER(thread_checker_);
  DISALLOW_COPY_AND_ASSIGN(GpuArcVideoProtectedBufferAllocator);
};
}  // namespace arc

#endif  // COMPONENTS_ARC_VIDEO_ACCELERATOR_GPU_ARC_VIDEO_PROTECTED_BUFFER_ALLOCATOR_H_
