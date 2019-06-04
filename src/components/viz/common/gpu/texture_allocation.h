// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_COMMON_GPU_TEXTURE_ALLOCATION_H_
#define COMPONENTS_VIZ_COMMON_GPU_TEXTURE_ALLOCATION_H_

#include "components/viz/common/resources/resource_format.h"
#include "components/viz/common/viz_common_export.h"
#include "third_party/khronos/GLES2/gl2.h"

#include <stdint.h>

namespace gfx {
class ColorSpace;
class Size;
}  // namespace gfx

namespace gpu {
struct Capabilities;
namespace gles2 {
class GLES2Interface;
}
}  // namespace gpu

namespace viz {

class VIZ_COMMON_EXPORT TextureAllocation {
 public:
  GLuint texture_id = 0;
  GLenum texture_target = 0;
  bool overlay_candidate = false;

  // Generates a texture id and sets it up for use, but without any storage
  // allocated pixels.
  static TextureAllocation MakeTextureId(gpu::gles2::GLES2Interface* gl,
                                         const gpu::Capabilities& caps,
                                         ResourceFormat format,
                                         bool use_gpu_memory_buffer_resources,
                                         bool for_framebuffer_attachment);

  // Allocates the storage for a texture id previously from MakeTextureId().
  // Can be called on a different context, if the texture id is mapped to
  // another context with a mailbox. The |format| should match the one given to
  // MakeTextureId().
  static void AllocateStorage(gpu::gles2::GLES2Interface* gl,
                              const gpu::Capabilities& caps,
                              ResourceFormat format,
                              const gfx::Size& size,
                              const TextureAllocation& alloc,
                              const gfx::ColorSpace& color_space);

  // Allocates storage for a texture id previously generated by MakeTextureId(),
  // and uploads the contents of |pixels| to it. |pixels| should point to a
  // bitmap with a width and height of |size|, and no additional row stride
  // padding.
  static void UploadStorage(gpu::gles2::GLES2Interface* gl,
                            const gpu::Capabilities& caps,
                            ResourceFormat format,
                            const gfx::Size& size,
                            const TextureAllocation& alloc,
                            const gfx::ColorSpace& color_space,
                            const void* pixels);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_COMMON_GPU_TEXTURE_ALLOCATION_H_
