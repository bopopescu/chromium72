// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/fake_audio_source.h"

namespace remoting {
namespace protocol {

FakeAudioSource::FakeAudioSource() = default;
FakeAudioSource::~FakeAudioSource() = default;

bool FakeAudioSource::Start(const PacketCapturedCallback& callback) {
  callback_ = callback;
  return true;
}

}  // namespace protocol
}  // namespace remoting
