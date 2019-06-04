// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search/one_google_bar/one_google_bar_service.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "chrome/browser/search/one_google_bar/one_google_bar_loader.h"
#include "services/identity/public/cpp/identity_manager.h"

class OneGoogleBarService::SigninObserver
    : public identity::IdentityManager::Observer {
 public:
  using SigninStatusChangedCallback = base::Closure;

  SigninObserver(identity::IdentityManager* identity_manager,
                 const SigninStatusChangedCallback& callback)
      : identity_manager_(identity_manager), callback_(callback) {
    identity_manager_->AddObserver(this);
  }

  ~SigninObserver() override { identity_manager_->RemoveObserver(this); }

 private:
  // IdentityManager::Observer implementation.
  void OnAccountsInCookieUpdated(
      const std::vector<AccountInfo>& accounts) override {
    callback_.Run();
  }

  identity::IdentityManager* const identity_manager_;
  SigninStatusChangedCallback callback_;
};

OneGoogleBarService::OneGoogleBarService(
    identity::IdentityManager* identity_manager,
    std::unique_ptr<OneGoogleBarLoader> loader)
    : loader_(std::move(loader)),
      signin_observer_(std::make_unique<SigninObserver>(
          identity_manager,
          base::Bind(&OneGoogleBarService::SigninStatusChanged,
                     base::Unretained(this)))) {}

OneGoogleBarService::~OneGoogleBarService() = default;

void OneGoogleBarService::Shutdown() {
  for (auto& observer : observers_) {
    observer.OnOneGoogleBarServiceShuttingDown();
  }

  signin_observer_.reset();
  DCHECK(!observers_.might_have_observers());
}

void OneGoogleBarService::Refresh() {
  loader_->Load(base::BindOnce(&OneGoogleBarService::OneGoogleBarDataLoaded,
                               base::Unretained(this)));
}

void OneGoogleBarService::AddObserver(OneGoogleBarServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void OneGoogleBarService::RemoveObserver(
    OneGoogleBarServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void OneGoogleBarService::SigninStatusChanged() {
  // If we have cached data, clear it and notify observers.
  if (one_google_bar_data_.has_value()) {
    one_google_bar_data_ = base::nullopt;
    NotifyObservers();
  }
}

void OneGoogleBarService::OneGoogleBarDataLoaded(
    OneGoogleBarLoader::Status status,
    const base::Optional<OneGoogleBarData>& data) {
  // In case of transient errors, keep our cached data (if any), but still
  // notify observers of the finished load (attempt).
  if (status != OneGoogleBarLoader::Status::TRANSIENT_ERROR) {
    one_google_bar_data_ = data;
  }
  NotifyObservers();
}

void OneGoogleBarService::NotifyObservers() {
  for (auto& observer : observers_) {
    observer.OnOneGoogleBarDataUpdated();
  }
}
