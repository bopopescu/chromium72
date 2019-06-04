// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_IOS_FACADE_IOS_CLIENT_RUNTIME_DELEGATE_H_
#define REMOTING_IOS_FACADE_IOS_CLIENT_RUNTIME_DELEGATE_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "remoting/client/chromoting_client_runtime.h"

namespace remoting {

class IosOauthTokenGetter;

class IosClientRuntimeDelegate : public ChromotingClientRuntime::Delegate {
 public:
  IosClientRuntimeDelegate();
  ~IosClientRuntimeDelegate() override;

  // remoting::ChromotingClientRuntime::Delegate overrides.
  void RuntimeWillShutdown() override;
  void RuntimeDidShutdown() override;
  base::WeakPtr<OAuthTokenGetter> oauth_token_getter() override;

  base::WeakPtr<IosClientRuntimeDelegate> GetWeakPtr();

 private:
  std::unique_ptr<IosOauthTokenGetter> token_getter_;
  ChromotingClientRuntime* runtime_;

  base::WeakPtrFactory<IosClientRuntimeDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(IosClientRuntimeDelegate);
};

}  // namespace remoting

#endif  // REMOTING_IOS_FACADE_CLIENT_RUNTIME_DELEGATE_H_
