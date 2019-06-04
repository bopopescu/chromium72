// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/mock_gbm_device.h"

#include <drm_fourcc.h>
#include <xf86drm.h>

#include "base/logging.h"
#include "base/numerics/safe_math.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/ozone/common/linux/drm_util_linux.h"
#include "ui/ozone/common/linux/gbm_buffer.h"

namespace ui {
namespace {

class MockGbmBuffer final : public ui::GbmBuffer {
 public:
  MockGbmBuffer(uint32_t format,
                uint32_t flags,
                uint64_t modifier,
                const gfx::Size& size,
                std::vector<gfx::NativePixmapPlane> planes,
                std::vector<uint32_t> handles)
      : format_(format),
        format_modifier_(modifier),
        flags_(flags),
        size_(size),
        planes_(std::move(planes)),
        handles_(std::move(handles)) {}

  ~MockGbmBuffer() override {}

  uint32_t GetFormat() const override { return format_; }
  uint64_t GetFormatModifier() const override { return format_modifier_; }
  uint32_t GetFlags() const override { return flags_; }
  size_t GetFdCount() const override { return 0; }
  gfx::Size GetSize() const override { return size_; }
  gfx::BufferFormat GetBufferFormat() const override {
    return ui::GetBufferFormatFromFourCCFormat(format_);
  }
  bool AreFdsValid() const override { return false; }
  size_t GetNumPlanes() const override { return planes_.size(); }
  int GetPlaneFd(size_t plane) const override {
    NOTREACHED();
    return -1;
  }
  int GetPlaneStride(size_t plane) const override {
    DCHECK_LT(plane, planes_.size());
    return planes_[plane].stride;
  }
  int GetPlaneOffset(size_t plane) const override {
    DCHECK_LT(plane, planes_.size());
    return planes_[plane].offset;
  }
  size_t GetPlaneSize(size_t plane) const override {
    DCHECK_LT(plane, planes_.size());
    return planes_[plane].size;
  }
  uint32_t GetPlaneHandle(size_t plane) const override {
    DCHECK_LT(plane, planes_.size());
    return handles_[plane];
  }
  uint32_t GetHandle() const override { return GetPlaneHandle(0); }
  gfx::NativePixmapHandle ExportHandle() const override {
    NOTIMPLEMENTED();
    return gfx::NativePixmapHandle();
  }

  sk_sp<SkSurface> GetSurface() override { return nullptr; }

 private:
  uint32_t format_ = 0;
  uint64_t format_modifier_ = 0;
  uint32_t flags_ = 0;
  std::vector<base::ScopedFD> fds_;
  gfx::Size size_;
  std::vector<gfx::NativePixmapPlane> planes_;
  std::vector<uint32_t> handles_;

  DISALLOW_COPY_AND_ASSIGN(MockGbmBuffer);
};

}  // namespace

MockGbmDevice::MockGbmDevice() {}

MockGbmDevice::~MockGbmDevice() {}

void MockGbmDevice::set_allocation_failure(bool should_fail_allocations) {
  should_fail_allocations_ = should_fail_allocations;
}

std::unique_ptr<GbmBuffer> MockGbmDevice::CreateBuffer(uint32_t format,
                                                       const gfx::Size& size,
                                                       uint32_t flags) {
  if (should_fail_allocations_)
    return nullptr;

  return CreateBufferWithModifiers(format, size, flags, {});
}

std::unique_ptr<GbmBuffer> MockGbmDevice::CreateBufferWithModifiers(
    uint32_t format,
    const gfx::Size& size,
    uint32_t flags,
    const std::vector<uint64_t>& modifiers) {
  uint32_t bytes_per_pixel;
  switch (format) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888:
      bytes_per_pixel = 4;
      break;
    case DRM_FORMAT_UYVY:
      bytes_per_pixel = 2;
      break;
    default:
      NOTREACHED() << "Unsupported format: " << format;
      return nullptr;
  }

  if (modifiers.size() > 1)
    return nullptr;

  uint64_t format_modifier =
      modifiers.size() ? modifiers[0] : DRM_FORMAT_MOD_NONE;
  switch (format_modifier) {
    case DRM_FORMAT_MOD_NONE:
    case I915_FORMAT_MOD_X_TILED:
      break;
    default:
      NOTREACHED() << "Unsupported format modifier: " << format_modifier;
      return nullptr;
  }

  uint32_t width = base::checked_cast<uint32_t>(size.width());
  uint32_t height = base::checked_cast<uint32_t>(size.height());
  uint32_t plane_stride = base::CheckMul(bytes_per_pixel, width).ValueOrDie();
  uint32_t plane_size = base::CheckMul(plane_stride, height).ValueOrDie();
  uint32_t plane_offset = 0;

  std::vector<gfx::NativePixmapPlane> planes;
  planes.push_back(gfx::NativePixmapPlane(plane_stride, plane_offset,
                                          plane_size, format_modifier));
  std::vector<uint32_t> handles;
  handles.push_back(next_handle_++);

  return std::make_unique<MockGbmBuffer>(format, flags, format_modifier, size,
                                         std::move(planes), std::move(handles));
}

std::unique_ptr<GbmBuffer> MockGbmDevice::CreateBufferFromFds(
    uint32_t format,
    const gfx::Size& size,
    std::vector<base::ScopedFD> fds,
    const std::vector<gfx::NativePixmapPlane>& planes) {
  NOTREACHED();
  return nullptr;
}

}  // namespace ui
