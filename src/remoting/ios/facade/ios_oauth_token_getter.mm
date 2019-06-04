// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#include "remoting/ios/facade/ios_oauth_token_getter.h"

#include "base/strings/sys_string_conversions.h"
#include "remoting/ios/facade/remoting_authentication.h"
#include "remoting/ios/facade/remoting_service.h"

namespace remoting {

IosOauthTokenGetter::IosOauthTokenGetter() : weak_factory_(this) {
  weak_ptr_ = weak_factory_.GetWeakPtr();
}

IosOauthTokenGetter::~IosOauthTokenGetter() {}

void IosOauthTokenGetter::CallWithToken(const TokenCallback& on_access_token) {
  // This forces the block to copy the callback instead of just the reference.
  TokenCallback on_access_token_copied = on_access_token;
  [RemotingService.instance.authentication
      callbackWithAccessToken:^(RemotingAuthenticationStatus status,
                                NSString* userEmail, NSString* accessToken) {
        Status oauth_status;
        switch (status) {
          case RemotingAuthenticationStatusSuccess:
            oauth_status = Status::SUCCESS;
            break;
          case RemotingAuthenticationStatusAuthError:
            oauth_status = Status::AUTH_ERROR;
            break;
          case RemotingAuthenticationStatusNetworkError:
            oauth_status = Status::NETWORK_ERROR;
            break;
          default:
            NOTREACHED();
        }
        on_access_token_copied.Run(oauth_status,
                                   base::SysNSStringToUTF8(userEmail),
                                   base::SysNSStringToUTF8(accessToken));
      }];
}

void IosOauthTokenGetter::InvalidateCache() {
  // TODO(crbug.com/782071): Implement this once we make the caller invalidate
  // the cache.
  NOTIMPLEMENTED();
}

base::WeakPtr<IosOauthTokenGetter> IosOauthTokenGetter::GetWeakPtr() {
  return weak_ptr_;
}

}  // namespace remoting
