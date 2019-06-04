// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_registration_object_host.h"

#include "base/task/post_task.h"
#include "base/time/time.h"
#include "content/browser/service_worker/service_worker_consts.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_object_host.h"
#include "content/browser/service_worker/service_worker_provider_host.h"
#include "content/common/service_worker/service_worker_utils.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "net/http/http_util.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration.mojom.h"

namespace content {

namespace {

constexpr base::TimeDelta kSelfUpdateDelay = base::TimeDelta::FromSeconds(30);
constexpr base::TimeDelta kMaxSelfUpdateDelay = base::TimeDelta::FromMinutes(3);

// Returns an object info to send over Mojo. The info must be sent immediately.
// See ServiceWorkerObjectHost::CreateCompleteObjectInfoToSend() for details.
blink::mojom::ServiceWorkerObjectInfoPtr CreateCompleteObjectInfoToSend(
    ServiceWorkerProviderHost* provider_host,
    ServiceWorkerVersion* version) {
  base::WeakPtr<ServiceWorkerObjectHost> service_worker_object_host =
      provider_host->GetOrCreateServiceWorkerObjectHost(version);
  if (!service_worker_object_host)
    return nullptr;
  return service_worker_object_host->CreateCompleteObjectInfoToSend();
}

void ExecuteUpdate(base::WeakPtr<ServiceWorkerContextCore> context,
                   int64_t registration_id,
                   bool force_bypass_cache,
                   bool skip_script_comparison,
                   ServiceWorkerContextCore::UpdateCallback callback,
                   blink::ServiceWorkerStatusCode status) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (status != blink::ServiceWorkerStatusCode::kOk) {
    // The delay was already very long and update() is rejected immediately.
    DCHECK_EQ(blink::ServiceWorkerStatusCode::kErrorTimeout, status);
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorTimeout,
                            ServiceWorkerConsts::kUpdateTimeoutErrorMesage,
                            registration_id);
    return;
  }

  if (!context) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            ServiceWorkerConsts::kShutdownErrorMessage,
                            registration_id);
    return;
  }

  ServiceWorkerRegistration* registration =
      context->GetLiveRegistration(registration_id);
  if (!registration) {
    // The service worker is no longer running, so update() won't be rejected.
    // We still run the callback so the caller knows.
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorTimeout,
                            ServiceWorkerConsts::kUpdateTimeoutErrorMesage,
                            registration_id);
    return;
  }

  context->UpdateServiceWorker(registration, force_bypass_cache,
                               skip_script_comparison, std::move(callback));
}

}  // anonymous namespace

ServiceWorkerRegistrationObjectHost::ServiceWorkerRegistrationObjectHost(
    base::WeakPtr<ServiceWorkerContextCore> context,
    ServiceWorkerProviderHost* provider_host,
    scoped_refptr<ServiceWorkerRegistration> registration)
    : provider_host_(provider_host),
      context_(context),
      registration_(registration),
      weak_ptr_factory_(this) {
  DCHECK(registration_.get());
  DCHECK(provider_host_);
  registration_->AddListener(this);
  bindings_.set_connection_error_handler(
      base::Bind(&ServiceWorkerRegistrationObjectHost::OnConnectionError,
                 base::Unretained(this)));
}

ServiceWorkerRegistrationObjectHost::~ServiceWorkerRegistrationObjectHost() {
  DCHECK(registration_.get());
  registration_->RemoveListener(this);
}

blink::mojom::ServiceWorkerRegistrationObjectInfoPtr
ServiceWorkerRegistrationObjectHost::CreateObjectInfo() {
  // |info->options->script_type| is never accessed anywhere, so just set it to
  // kClassic.
  // TODO(asamidoi, nhiroki): Remove |options| from
  // ServiceWorkerRegistrationObjectInfo, since |script_type| is a
  // non-per-registration property.
  blink::mojom::ScriptType script_type = blink::mojom::ScriptType::kClassic;

  auto info = blink::mojom::ServiceWorkerRegistrationObjectInfo::New();
  info->options = blink::mojom::ServiceWorkerRegistrationOptions::New(
      registration_->scope(), script_type, registration_->update_via_cache());
  info->registration_id = registration_->id();
  bindings_.AddBinding(this, mojo::MakeRequest(&info->host_ptr_info));
  info->request = mojo::MakeRequest(&remote_registration_);

  info->installing = CreateCompleteObjectInfoToSend(
      provider_host_, registration_->installing_version());
  info->waiting = CreateCompleteObjectInfoToSend(
      provider_host_, registration_->waiting_version());
  info->active = CreateCompleteObjectInfoToSend(
      provider_host_, registration_->active_version());
  return info;
}

void ServiceWorkerRegistrationObjectHost::OnVersionAttributesChanged(
    ServiceWorkerRegistration* registration,
    blink::mojom::ChangedServiceWorkerObjectsMaskPtr changed_mask,
    const ServiceWorkerRegistrationInfo& info) {
  DCHECK_EQ(registration->id(), registration_->id());
  SetServiceWorkerObjects(
      std::move(changed_mask), registration->installing_version(),
      registration->waiting_version(), registration->active_version());
}

void ServiceWorkerRegistrationObjectHost::OnUpdateViaCacheChanged(
    ServiceWorkerRegistration* registration) {
  remote_registration_->SetUpdateViaCache(registration->update_via_cache());
}

void ServiceWorkerRegistrationObjectHost::OnRegistrationFailed(
    ServiceWorkerRegistration* registration) {
  DCHECK_EQ(registration->id(), registration_->id());
  auto changed_mask =
      blink::mojom::ChangedServiceWorkerObjectsMask::New(true, true, true);
  SetServiceWorkerObjects(std::move(changed_mask), nullptr, nullptr, nullptr);
}

void ServiceWorkerRegistrationObjectHost::OnUpdateFound(
    ServiceWorkerRegistration* registration) {
  DCHECK(remote_registration_);
  remote_registration_->UpdateFound();
}

void ServiceWorkerRegistrationObjectHost::Update(UpdateCallback callback) {
  if (!CanServeRegistrationObjectHostMethods(&callback,
                                             kServiceWorkerUpdateErrorPrefix)) {
    return;
  }

  if (!registration_->GetNewestVersion()) {
    // This can happen if update() is called during initial script evaluation.
    // Abort the following steps according to the spec.
    std::move(callback).Run(
        blink::mojom::ServiceWorkerErrorType::kState,
        std::string(kServiceWorkerUpdateErrorPrefix) +
            std::string(ServiceWorkerConsts::kInvalidStateErrorMessage));
    return;
  }

  DelayUpdate(
      provider_host_->provider_type(), registration_.get(),
      provider_host_->running_hosted_version(),
      base::BindOnce(
          &ExecuteUpdate, context_, registration_->id(),
          false /* force_bypass_cache */, false /* skip_script_comparison */,
          base::BindOnce(&ServiceWorkerRegistrationObjectHost::UpdateComplete,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void ServiceWorkerRegistrationObjectHost::DelayUpdate(
    blink::mojom::ServiceWorkerProviderType provider_type,
    ServiceWorkerRegistration* registration,
    ServiceWorkerVersion* version,
    StatusCallback update_function) {
  DCHECK(registration);

  if (provider_type !=
          blink::mojom::ServiceWorkerProviderType::kForServiceWorker ||
      (version && version->HasControllee())) {
    // Don't delay update() if called by non-workers or by workers with
    // controllees.
    std::move(update_function).Run(blink::ServiceWorkerStatusCode::kOk);
    return;
  }

  base::TimeDelta delay = registration->self_update_delay();
  if (delay > kMaxSelfUpdateDelay) {
    std::move(update_function)
        .Run(blink::ServiceWorkerStatusCode::kErrorTimeout);
    return;
  }

  if (delay < kSelfUpdateDelay) {
    registration->set_self_update_delay(kSelfUpdateDelay);
  } else {
    registration->set_self_update_delay(delay * 2);
  }

  if (delay < base::TimeDelta::Min()) {
    // Only enforce the delay of update() iff |delay| exists.
    std::move(update_function).Run(blink::ServiceWorkerStatusCode::kOk);
    return;
  }

  base::PostDelayedTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(std::move(update_function),
                     blink::ServiceWorkerStatusCode::kOk),
      delay);
}

void ServiceWorkerRegistrationObjectHost::Unregister(
    UnregisterCallback callback) {
  if (!CanServeRegistrationObjectHostMethods(
          &callback, kServiceWorkerUnregisterErrorPrefix)) {
    return;
  }

  context_->UnregisterServiceWorker(
      registration_->scope(),
      base::AdaptCallbackForRepeating(base::BindOnce(
          &ServiceWorkerRegistrationObjectHost::UnregistrationComplete,
          weak_ptr_factory_.GetWeakPtr(), std::move(callback))));
}

void ServiceWorkerRegistrationObjectHost::EnableNavigationPreload(
    bool enable,
    EnableNavigationPreloadCallback callback) {
  if (!CanServeRegistrationObjectHostMethods(
          &callback,
          ServiceWorkerConsts::kEnableNavigationPreloadErrorPrefix)) {
    return;
  }

  if (!registration_->active_version()) {
    std::move(callback).Run(
        blink::mojom::ServiceWorkerErrorType::kState,
        std::string(ServiceWorkerConsts::kEnableNavigationPreloadErrorPrefix) +
            std::string(ServiceWorkerConsts::kNoActiveWorkerErrorMessage));
    return;
  }

  context_->storage()->UpdateNavigationPreloadEnabled(
      registration_->id(), registration_->scope().GetOrigin(), enable,
      base::AdaptCallbackForRepeating(base::BindOnce(
          &ServiceWorkerRegistrationObjectHost::
              DidUpdateNavigationPreloadEnabled,
          weak_ptr_factory_.GetWeakPtr(), enable, std::move(callback))));
}

void ServiceWorkerRegistrationObjectHost::GetNavigationPreloadState(
    GetNavigationPreloadStateCallback callback) {
  if (!CanServeRegistrationObjectHostMethods(
          &callback, ServiceWorkerConsts::kGetNavigationPreloadStateErrorPrefix,
          nullptr)) {
    return;
  }

  std::move(callback).Run(blink::mojom::ServiceWorkerErrorType::kNone,
                          base::nullopt,
                          registration_->navigation_preload_state().Clone());
}

void ServiceWorkerRegistrationObjectHost::SetNavigationPreloadHeader(
    const std::string& value,
    SetNavigationPreloadHeaderCallback callback) {
  if (!CanServeRegistrationObjectHostMethods(
          &callback,
          ServiceWorkerConsts::kSetNavigationPreloadHeaderErrorPrefix)) {
    return;
  }

  if (!registration_->active_version()) {
    std::move(callback).Run(
        blink::mojom::ServiceWorkerErrorType::kState,
        std::string(
            ServiceWorkerConsts::kSetNavigationPreloadHeaderErrorPrefix) +
            std::string(ServiceWorkerConsts::kNoActiveWorkerErrorMessage));
    return;
  }

  // TODO(falken): Ideally this would match Blink's isValidHTTPHeaderValue.
  // Chrome's check is less restrictive: it allows non-latin1 characters.
  if (!net::HttpUtil::IsValidHeaderValue(value)) {
    bindings_.ReportBadMessage(
        ServiceWorkerConsts::kBadNavigationPreloadHeaderValue);
    return;
  }

  context_->storage()->UpdateNavigationPreloadHeader(
      registration_->id(), registration_->scope().GetOrigin(), value,
      base::AdaptCallbackForRepeating(base::BindOnce(
          &ServiceWorkerRegistrationObjectHost::
              DidUpdateNavigationPreloadHeader,
          weak_ptr_factory_.GetWeakPtr(), value, std::move(callback))));
}

void ServiceWorkerRegistrationObjectHost::UpdateComplete(
    UpdateCallback callback,
    blink::ServiceWorkerStatusCode status,
    const std::string& status_message,
    int64_t registration_id) {
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::string error_message;
    blink::mojom::ServiceWorkerErrorType error_type;
    GetServiceWorkerErrorTypeForRegistration(status, status_message,
                                             &error_type, &error_message);
    std::move(callback).Run(error_type,
                            kServiceWorkerUpdateErrorPrefix + error_message);
    return;
  }

  std::move(callback).Run(blink::mojom::ServiceWorkerErrorType::kNone,
                          base::nullopt);
}

void ServiceWorkerRegistrationObjectHost::UnregistrationComplete(
    UnregisterCallback callback,
    blink::ServiceWorkerStatusCode status) {
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::string error_message;
    blink::mojom::ServiceWorkerErrorType error_type;
    GetServiceWorkerErrorTypeForRegistration(status, std::string(), &error_type,
                                             &error_message);
    std::move(callback).Run(
        error_type, kServiceWorkerUnregisterErrorPrefix + error_message);
    return;
  }

  std::move(callback).Run(blink::mojom::ServiceWorkerErrorType::kNone,
                          base::nullopt);
}

void ServiceWorkerRegistrationObjectHost::DidUpdateNavigationPreloadEnabled(
    bool enable,
    EnableNavigationPreloadCallback callback,
    blink::ServiceWorkerStatusCode status) {
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(callback).Run(
        blink::mojom::ServiceWorkerErrorType::kUnknown,
        std::string(ServiceWorkerConsts::kEnableNavigationPreloadErrorPrefix) +
            std::string(ServiceWorkerConsts::kDatabaseErrorMessage));
    return;
  }

  if (registration_)
    registration_->EnableNavigationPreload(enable);
  std::move(callback).Run(blink::mojom::ServiceWorkerErrorType::kNone,
                          base::nullopt);
}

void ServiceWorkerRegistrationObjectHost::DidUpdateNavigationPreloadHeader(
    const std::string& value,
    SetNavigationPreloadHeaderCallback callback,
    blink::ServiceWorkerStatusCode status) {
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(callback).Run(
        blink::mojom::ServiceWorkerErrorType::kUnknown,
        std::string(
            ServiceWorkerConsts::kSetNavigationPreloadHeaderErrorPrefix) +
            std::string(ServiceWorkerConsts::kDatabaseErrorMessage));
    return;
  }

  if (registration_)
    registration_->SetNavigationPreloadHeader(value);
  std::move(callback).Run(blink::mojom::ServiceWorkerErrorType::kNone,
                          base::nullopt);
}

void ServiceWorkerRegistrationObjectHost::SetServiceWorkerObjects(
    blink::mojom::ChangedServiceWorkerObjectsMaskPtr changed_mask,
    ServiceWorkerVersion* installing_version,
    ServiceWorkerVersion* waiting_version,
    ServiceWorkerVersion* active_version) {
  if (!(changed_mask->installing || changed_mask->waiting ||
        changed_mask->active))
    return;

  blink::mojom::ServiceWorkerObjectInfoPtr installing;
  blink::mojom::ServiceWorkerObjectInfoPtr waiting;
  blink::mojom::ServiceWorkerObjectInfoPtr active;
  if (changed_mask->installing) {
    installing =
        CreateCompleteObjectInfoToSend(provider_host_, installing_version);
  }
  if (changed_mask->waiting)
    waiting = CreateCompleteObjectInfoToSend(provider_host_, waiting_version);
  if (changed_mask->active)
    active = CreateCompleteObjectInfoToSend(provider_host_, active_version);

  DCHECK(remote_registration_);
  remote_registration_->SetServiceWorkerObjects(
      std::move(changed_mask), std::move(installing), std::move(waiting),
      std::move(active));
}

void ServiceWorkerRegistrationObjectHost::OnConnectionError() {
  // If there are still bindings, |this| is still being used.
  if (!bindings_.empty())
    return;
  // Will destroy |this|.
  provider_host_->RemoveServiceWorkerRegistrationObjectHost(
      registration()->id());
}

template <typename CallbackType, typename... Args>
bool ServiceWorkerRegistrationObjectHost::CanServeRegistrationObjectHostMethods(
    CallbackType* callback,
    const char* error_prefix,
    Args... args) {
  if (!context_) {
    std::move(*callback).Run(
        blink::mojom::ServiceWorkerErrorType::kAbort,
        std::string(error_prefix) +
            std::string(ServiceWorkerConsts::kShutdownErrorMessage),
        args...);
    return false;
  }

  // TODO(falken): This check can be removed once crbug.com/439697 is fixed.
  // (Also see crbug.com/776408)
  if (provider_host_->url().is_empty()) {
    std::move(*callback).Run(
        blink::mojom::ServiceWorkerErrorType::kSecurity,
        std::string(error_prefix) +
            std::string(ServiceWorkerConsts::kNoDocumentURLErrorMessage),
        args...);
    return false;
  }

  std::vector<GURL> urls = {provider_host_->url(), registration_->scope()};
  if (!ServiceWorkerUtils::AllOriginsMatchAndCanAccessServiceWorkers(urls)) {
    bindings_.ReportBadMessage(ServiceWorkerConsts::kBadMessageImproperOrigins);
    return false;
  }

  if (!provider_host_->AllowServiceWorker(registration_->scope())) {
    std::move(*callback).Run(
        blink::mojom::ServiceWorkerErrorType::kDisabled,
        std::string(error_prefix) +
            std::string(ServiceWorkerConsts::kUserDeniedPermissionMessage),
        args...);
    return false;
  }

  return true;
}

}  // namespace content
