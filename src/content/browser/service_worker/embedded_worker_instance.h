// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_EMBEDDED_WORKER_INSTANCE_H_
#define CONTENT_BROWSER_SERVICE_WORKER_EMBEDDED_WORKER_INSTANCE_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "content/browser/service_worker/embedded_worker_status.h"
#include "content/browser/service_worker/service_worker_metrics.h"
#include "content/common/content_export.h"
#include "content/common/service_worker/controller_service_worker.mojom.h"
#include "content/common/service_worker/embedded_worker.mojom.h"
#include "content/common/service_worker/service_worker.mojom.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/mojom/service_worker/service_worker.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_installed_scripts_manager.mojom.h"
#include "third_party/blink/public/platform/modules/cache_storage/cache_storage.mojom.h"
#include "url/gurl.h"

namespace content {

class EmbeddedWorkerRegistry;
class ServiceWorkerContentSettingsProxyImpl;
class ServiceWorkerContextCore;
class ServiceWorkerVersion;

namespace service_worker_new_script_loader_unittest {
class ServiceWorkerNewScriptLoaderTest;
FORWARD_DECLARE_TEST(ServiceWorkerNewScriptLoaderTest, AccessedNetwork);
}  // namespace service_worker_new_script_loader_unittest

// This gives an interface to control one EmbeddedWorker instance, which
// may be 'in-waiting' or running in one of the child processes added by
// AddProcessReference().
//
// Owned by ServiceWorkerVersion. Lives on the IO thread.
class CONTENT_EXPORT EmbeddedWorkerInstance
    : public mojom::EmbeddedWorkerInstanceHost {
 public:
  class DevToolsProxy;
  using StatusCallback =
      base::OnceCallback<void(blink::ServiceWorkerStatusCode)>;

  // This enum is used in UMA histograms. Append-only.
  enum StartingPhase {
    NOT_STARTING = 0,
    ALLOCATING_PROCESS = 1,
    // REGISTERING_TO_DEVTOOLS = 2,  // Obsolete
    SENT_START_WORKER = 3,
    SCRIPT_DOWNLOADING = 4,
    SCRIPT_LOADED = 5,
    // SCRIPT_EVALUATED = 6,  // Obsolete
    // THREAD_STARTED = 7,  // Obsolete
    // Script read happens after SENT_START_WORKER and before SCRIPT_LOADED
    // (installed scripts only)
    SCRIPT_READ_STARTED = 8,
    SCRIPT_READ_FINISHED = 9,
    SCRIPT_STREAMING = 10,
    SCRIPT_EVALUATION = 11,
    // Add new values here and update enums.xml.
    STARTING_PHASE_MAX_VALUE,
  };

  // DEPRECATED, only for use by ServiceWorkerVersion.
  // TODO(crbug.com/855852): Remove this interface.
  class Listener {
   public:
    virtual ~Listener() {}

    virtual void OnStarting() {}
    virtual void OnProcessAllocated() {}
    virtual void OnRegisteredToDevToolsManager() {}
    virtual void OnStartWorkerMessageSent() {}
    virtual void OnScriptEvaluationStart() {}
    virtual void OnStarted(blink::mojom::ServiceWorkerStartStatus status) {}

    // Called when status changed to STOPPING. The renderer has been sent a Stop
    // IPC message and OnStopped() will be called upon successful completion.
    virtual void OnStopping() {}

    // Called when status changed to STOPPED. Usually, this is called upon
    // receiving an ACK from renderer that the worker context terminated.
    // OnStopped() is also called if Stop() aborted an ongoing start attempt
    // even before the Start IPC message was sent to the renderer.  In this
    // case, OnStopping() is not called; the worker is "stopped" immediately
    // (the Start IPC is never sent).
    virtual void OnStopped(EmbeddedWorkerStatus old_status) {}

    // Called when the browser-side IPC endpoint for communication with the
    // worker died. When this is called, status is STOPPED.
    virtual void OnDetached(EmbeddedWorkerStatus old_status) {}

    virtual void OnReportException(const base::string16& error_message,
                                   int line_number,
                                   int column_number,
                                   const GURL& source_url) {}
    virtual void OnReportConsoleMessage(int source_identifier,
                                        int message_level,
                                        const base::string16& message,
                                        int line_number,
                                        const GURL& source_url) {}
  };

  ~EmbeddedWorkerInstance() override;

  // Starts the worker. It is invalid to call this when the worker is not in
  // STOPPED status.
  //
  // |sent_start_callback| is invoked once the Start IPC is sent, or if an error
  // prevented that from happening. The callback is not invoked in some cases,
  // e.g., when Stop() is called and aborts the start procedure. Note that when
  // the callback is invoked with kOk status, the service worker has not yet
  // finished starting. Observe OnStarted()/OnStopped() for when start completed
  // or failed.
  void Start(mojom::EmbeddedWorkerStartParamsPtr params,
             StatusCallback sent_start_callback);

  // Stops the worker. It is invalid to call this when the worker is not in
  // STARTING or RUNNING status.
  //
  // Stop() typically sends a Stop IPC to the renderer, and this instance enters
  // STOPPING status, with Listener::OnStopped() called upon completion. It can
  // synchronously complete if this instance is STARTING but the Start IPC
  // message has not yet been sent. In that case, the start procedure is
  // aborted, and this instance enters STOPPED status.
  void Stop();

  // Stops the worker if the worker is not being debugged (i.e. devtools is
  // not attached). This method is called by a stop-worker timer to kill
  // idle workers.
  void StopIfNotAttachedToDevTools();

  // Resumes the worker if it paused after download.
  void ResumeAfterDownload();

  int embedded_worker_id() const { return embedded_worker_id_; }
  EmbeddedWorkerStatus status() const { return status_; }
  StartingPhase starting_phase() const {
    DCHECK_EQ(EmbeddedWorkerStatus::STARTING, status());
    return starting_phase_;
  }
  int restart_count() const { return restart_count_; }
  int process_id() const;
  int thread_id() const { return thread_id_; }
  int worker_devtools_agent_route_id() const;

  // DEPRECATED, only for use by ServiceWorkerVersion.
  // TODO(crbug.com/855852): Remove the Listener interface.
  void AddObserver(Listener* listener);
  void RemoveObserver(Listener* listener);

  void SetDevToolsAttached(bool attached);
  bool devtools_attached() const { return devtools_attached_; }

  bool network_accessed_for_script() const {
    return network_accessed_for_script_;
  }

  ServiceWorkerMetrics::StartSituation start_situation() const {
    DCHECK(status() == EmbeddedWorkerStatus::STARTING ||
           status() == EmbeddedWorkerStatus::RUNNING);
    return start_situation_;
  }

  // Called when the main script load accessed the network.
  void OnNetworkAccessedForScriptLoad();

  // Called when reading the main script from the service worker script cache
  // begins and ends.
  void OnScriptReadStarted();
  void OnScriptReadFinished();

  // Called when the worker is installed.
  void OnWorkerVersionInstalled();

  // Called when the worker is doomed.
  void OnWorkerVersionDoomed();

  // Add message to the devtools console.
  void AddMessageToConsole(blink::WebConsoleMessage::Level level,
                           const std::string& message);

  static std::string StatusToString(EmbeddedWorkerStatus status);
  static std::string StartingPhaseToString(StartingPhase phase);

  using CreateNetworkFactoryCallback = base::RepeatingCallback<void(
      network::mojom::URLLoaderFactoryRequest request,
      int process_id,
      network::mojom::URLLoaderFactoryPtrInfo original_factory)>;
  // Allows overriding the URLLoaderFactory creation for loading subresources
  // from service workers (i.e., fetch()) and for loading non-installed service
  // worker scripts.
  static void SetNetworkFactoryForTesting(
      const CreateNetworkFactoryCallback& url_loader_factory_callback);

  // Forces this instance into STOPPED status and releases any state about the
  // running worker. Called when connection with the renderer died or the
  // renderer is unresponsive.  Essentially, it throws away any information
  // about the renderer-side worker, and frees this instance up to start a new
  // worker.
  void Detach();

  base::WeakPtr<EmbeddedWorkerInstance> AsWeakPtr();

 private:
  typedef base::ObserverList<Listener>::Unchecked ListenerList;
  class StartTask;
  class WorkerProcessHandle;
  friend class EmbeddedWorkerRegistry;
  friend class EmbeddedWorkerInstanceTest;
  FRIEND_TEST_ALL_PREFIXES(EmbeddedWorkerInstanceTest, StartAndStop);
  FRIEND_TEST_ALL_PREFIXES(EmbeddedWorkerInstanceTest, DetachDuringStart);
  FRIEND_TEST_ALL_PREFIXES(EmbeddedWorkerInstanceTest, StopDuringStart);
  FRIEND_TEST_ALL_PREFIXES(service_worker_new_script_loader_unittest::
                               ServiceWorkerNewScriptLoaderTest,
                           AccessedNetwork);

  // Constructor is called via EmbeddedWorkerRegistry::CreateWorker().
  // This instance holds a ref of |registry|.
  EmbeddedWorkerInstance(base::WeakPtr<ServiceWorkerContextCore> context,
                         ServiceWorkerVersion* owner_version,
                         int embedded_worker_id);

  // Called back from StartTask after a process is allocated on the UI thread.
  void OnProcessAllocated(std::unique_ptr<WorkerProcessHandle> handle,
                          ServiceWorkerMetrics::StartSituation start_situation);

  // Called back from StartTask after the worker is registered to
  // WorkerDevToolsManager.
  void OnRegisteredToDevToolsManager(
      std::unique_ptr<DevToolsProxy> devtools_proxy,
      bool wait_for_debugger);

  // Sends the StartWorker message to the renderer.
  //
  // |cache_storage| is an optional optimization so the service worker can
  // use the Cache Storage API immediately upon startup.
  //
  // S13nServiceWorker:
  // |factory| is used for loading non-installed scripts. It can internally be a
  // bundle of factories instead of just the direct network factory to support
  // non-NetworkService schemes like chrome-extension:// URLs.
  void SendStartWorker(mojom::EmbeddedWorkerStartParamsPtr params,
                       scoped_refptr<network::SharedURLLoaderFactory> factory,
                       blink::mojom::CacheStoragePtrInfo cache_storage);

  // Implements mojom::EmbeddedWorkerInstanceHost.
  // These functions all run on the IO thread.
  void RequestTermination(RequestTerminationCallback callback) override;
  void CountFeature(blink::mojom::WebFeature feature) override;
  void OnReadyForInspection() override;
  void OnScriptLoaded() override;
  void OnScriptEvaluationStart() override;
  // Changes the internal worker status from STARTING to RUNNING.
  void OnStarted(blink::mojom::ServiceWorkerStartStatus status,
                 int thread_id,
                 mojom::EmbeddedWorkerStartTimingPtr start_timing) override;
  // Resets the embedded worker instance to the initial state. Changes
  // the internal status from STARTING or RUNNING to STOPPED.
  void OnStopped() override;
  void OnReportException(const base::string16& error_message,
                         int line_number,
                         int column_number,
                         const GURL& source_url) override;
  void OnReportConsoleMessage(int source_identifier,
                              int message_level,
                              const base::string16& message,
                              int line_number,
                              const GURL& source_url) override;

  // Resets all running state. After this function is called, |status_| is
  // STOPPED.
  void ReleaseProcess();

  // Called back from StartTask when the startup sequence failed. Calls
  // ReleaseProcess() and invokes |callback| with |status|. May destroy |this|.
  void OnSetupFailed(StatusCallback callback,
                     blink::ServiceWorkerStatusCode status);

  base::WeakPtr<ServiceWorkerContextCore> context_;
  scoped_refptr<EmbeddedWorkerRegistry> registry_;
  ServiceWorkerVersion* owner_version_;

  // Unique within an EmbeddedWorkerRegistry.
  const int embedded_worker_id_;

  EmbeddedWorkerStatus status_;
  StartingPhase starting_phase_;
  int restart_count_;

  // Current running information.
  std::unique_ptr<EmbeddedWorkerInstance::WorkerProcessHandle> process_handle_;
  int thread_id_;

  // |client_| is used to send messages to the renderer process. The browser
  // process should not disconnect the pipe because associated interfaces may be
  // using it. The renderer process will disconnect the pipe when appropriate.
  mojom::EmbeddedWorkerInstanceClientPtr client_;

  // Binding for EmbeddedWorkerInstanceHost, runs on IO thread.
  mojo::AssociatedBinding<EmbeddedWorkerInstanceHost> instance_host_binding_;

  // Whether devtools is attached or not.
  bool devtools_attached_;

  // True if the script load request accessed the network. If the script was
  // served from HTTPCache or ServiceWorkerDatabase this value is false.
  bool network_accessed_for_script_;

  ListenerList listener_list_;
  std::unique_ptr<DevToolsProxy> devtools_proxy_;

  std::unique_ptr<StartTask> inflight_start_task_;

  // This is valid only after a process is allocated for the worker.
  ServiceWorkerMetrics::StartSituation start_situation_ =
      ServiceWorkerMetrics::StartSituation::UNKNOWN;

  std::unique_ptr<ServiceWorkerContentSettingsProxyImpl> content_settings_;
  base::WeakPtrFactory<EmbeddedWorkerInstance> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(EmbeddedWorkerInstance);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_EMBEDDED_WORKER_INSTANCE_H_
