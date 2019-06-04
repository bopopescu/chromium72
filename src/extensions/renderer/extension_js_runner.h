// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_EXTENSION_JS_RUNNER_H_
#define EXTENSIONS_RENDERER_EXTENSION_JS_RUNNER_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "extensions/renderer/bindings/js_runner.h"

namespace extensions {
class ScriptContext;

// Implementation of a JSRunner that handles possible JS suspension.
class ExtensionJSRunner : public JSRunner {
 public:
  explicit ExtensionJSRunner(ScriptContext* script_context);
  ~ExtensionJSRunner() override;

  // JSRunner:
  void RunJSFunction(v8::Local<v8::Function> function,
                     v8::Local<v8::Context> context,
                     int argc,
                     v8::Local<v8::Value> argv[],
                     ResultCallback callback) override;
  v8::MaybeLocal<v8::Value> RunJSFunctionSync(
      v8::Local<v8::Function> function,
      v8::Local<v8::Context> context,
      int argc,
      v8::Local<v8::Value> argv[]) override;

 private:
  // Called with the result of executing the JS function.
  void OnFunctionComplete(ResultCallback callback,
                          const std::vector<v8::Local<v8::Value>>& results);

  // The associated ScriptContext. Guaranteed to outlive this object.
  ScriptContext* const script_context_;

  base::WeakPtrFactory<ExtensionJSRunner> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionJSRunner);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_EXTENSION_JS_RUNNER_H_
