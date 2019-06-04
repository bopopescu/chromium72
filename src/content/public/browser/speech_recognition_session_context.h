// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_SPEECH_RECOGNITION_SESSION_CONTEXT_H_
#define CONTENT_PUBLIC_BROWSER_SPEECH_RECOGNITION_SESSION_CONTEXT_H_

#include <string>

#include "content/common/content_export.h"
#include "content/public/common/media_stream_request.h"
#include "ui/gfx/geometry/rect.h"
#include "url/origin.h"

namespace content {

// The context information required by clients of the SpeechRecognitionManager
// and its delegates for mapping the recognition session to other browser
// elements involved with it (e.g., the page element that requested the
// recognition). The manager keeps this struct attached to the recognition
// session during all the session lifetime, making its contents available to
// clients. (In this regard, see SpeechRecognitionManager::GetSessionContext().)
struct CONTENT_EXPORT SpeechRecognitionSessionContext {
  SpeechRecognitionSessionContext();
  SpeechRecognitionSessionContext(const SpeechRecognitionSessionContext& other);
  ~SpeechRecognitionSessionContext();

  int render_process_id;
  int render_frame_id;

  // The pair (|embedder_render_process_id|, |embedder_render_frame_id|)
  // represents a Browser plugin guest's embedder. This is filled in if the
  // session is from a guest Web Speech API. We use these to check if the
  // embedder (app) is permitted to use audio.
  int embedder_render_process_id;
  int embedder_render_frame_id;

  // Origin that is requesting recognition, for prompting security notifications
  // to the user.
  url::Origin security_origin;

  // The label for the permission request, it is used for request abortion.
  std::string label;

  // A list of devices being used by the recognition session.
  MediaStreamDevices devices;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_SPEECH_RECOGNITION_SESSION_CONTEXT_H_
