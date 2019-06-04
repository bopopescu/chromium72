// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/magnifier/magnifier_scale_utils.h"

#include <algorithm>
#include <cmath>

#include "base/logging.h"
#include "base/numerics/ranges.h"

namespace ash {
namespace magnifier_scale_utils {

namespace {

// Converts the given |scale| to an index such that
// `kMagnificationScaleFactor ^ index = scale`.
int IndexFromScale(float scale) {
  // Remember from the Logarithm rules:
  // logBar(Foo) = log2(Foo) / log2(Bar).
  return std::round(std::log(scale) / std::log(kMagnificationScaleFactor));
}

}  // namespace

float GetScaleFromScroll(float linear_offset,
                         float current_scale,
                         float min_scale,
                         float max_scale) {
  DCHECK_GE(current_scale, min_scale);
  DCHECK_LE(current_scale, max_scale);

  // Convert the current scale back to its corresponding linear scale according
  // to the formula `scale = (max - min) * offset ^ 2 + min`.
  const float scale_range = max_scale - min_scale;
  float linear_adjustment =
      std::sqrt((current_scale - min_scale) / scale_range);
  // Add the new linear offset.
  linear_adjustment += linear_offset;

  // Convert back to the exponential scale.
  return scale_range * linear_adjustment * linear_adjustment + min_scale;
}

float GetNextMagnifierScaleValue(int delta_index,
                                 float current_scale,
                                 float min_scale,
                                 float max_scale) {
  const int current_index = IndexFromScale(current_scale);
  const int new_scale_index = current_index + delta_index;
  const float new_scale = std::pow(kMagnificationScaleFactor, new_scale_index);
  return base::ClampToRange(new_scale, min_scale, max_scale);
}

}  // namespace magnifier_scale_utils
}  // namespace ash
