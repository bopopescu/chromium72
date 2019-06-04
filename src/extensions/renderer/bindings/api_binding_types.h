// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_BINDINGS_API_BINDING_TYPES_H_
#define EXTENSIONS_RENDERER_BINDINGS_API_BINDING_TYPES_H_

#include <memory>
#include <vector>

#include "base/callback.h"
#include "v8/include/v8.h"

namespace extensions {
namespace binding {

// A value indicating an event has no maximum listener count.
extern const int kNoListenerMax;

// Types of changes for event listener registration.
enum class EventListenersChanged {
  // Unfiltered Events:

  // The first unfiltered listener for the associated context was added.
  kFirstUnfilteredListenerForContextAdded,
  // The first unfiltered listener for the associated context owner was added.
  // This also implies the first listener for the context was added.
  kFirstUnfilteredListenerForContextOwnerAdded,
  // The last unfiltered listener for the associated context was removed.
  kLastUnfilteredListenerForContextRemoved,
  // The last unfiltered listener for the associated context owner was removed.
  // This also implies the last listener for the context was removed.
  kLastUnfilteredListenerForContextOwnerRemoved,

  // Filtered Events:
  // TODO(https://crbug.com/873017): The fact that we only have added/removed
  // at the context owner level for filtered events can cause issues.

  // The first listener for the associated context owner with a specific
  // filter was added.
  kFirstListenerWithFilterForContextOwnerAdded,
  // The last listener for the associated context owner with a specific
  // filter was removed.
  kLastListenerWithFilterForContextOwnerRemoved,
};

// The browser thread that the request should be sent to.
enum class RequestThread {
  UI,
  IO,
};

// Adds an error message to the context's console.
using AddConsoleError =
    base::Callback<void(v8::Local<v8::Context>, const std::string& error)>;

}  // namespace binding
}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_BINDINGS_API_BINDING_TYPES_H_
