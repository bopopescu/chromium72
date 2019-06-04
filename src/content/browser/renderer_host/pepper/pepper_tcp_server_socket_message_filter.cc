// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/pepper_tcp_server_socket_message_filter.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/task/post_task.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/pepper/browser_ppapi_host_impl.h"
#include "content/browser/renderer_host/pepper/content_browser_pepper_host_factory.h"
#include "content/browser/renderer_host/pepper/pepper_socket_utils.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/socket_permission_request.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/error_conversion.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/api_id.h"
#include "ppapi/shared_impl/ppb_tcp_socket_shared.h"
#include "ppapi/shared_impl/private/net_address_private_impl.h"
#include "services/network/public/mojom/network_context.mojom.h"

#if defined(OS_CHROMEOS)
#include "chromeos/network/firewall_hole.h"
#endif  // defined(OS_CHROMEOS)

using ppapi::NetAddressPrivateImpl;
using ppapi::host::NetErrorToPepperError;

namespace {

static size_t g_num_instances = 0;

}  // namespace

namespace content {

network::mojom::NetworkContext*
    PepperTCPServerSocketMessageFilter::network_context_for_testing = nullptr;

PepperTCPServerSocketMessageFilter::PepperTCPServerSocketMessageFilter(
    ContentBrowserPepperHostFactory* factory,
    BrowserPpapiHostImpl* host,
    PP_Instance instance,
    bool private_api)
    : ppapi_host_(host->GetPpapiHost()),
      factory_(factory),
      instance_(instance),
      state_(STATE_BEFORE_LISTENING),
      bound_addr_(NetAddressPrivateImpl::kInvalidNetAddress),
      external_plugin_(host->external_plugin()),
      private_api_(private_api),
      render_process_id_(0),
      render_frame_id_(0),
      weak_ptr_factory_(this) {
  ++g_num_instances;
  DCHECK(factory_);
  DCHECK(ppapi_host_);
  if (!host->GetRenderFrameIDsForInstance(
          instance, &render_process_id_, &render_frame_id_)) {
    NOTREACHED();
  }
}

PepperTCPServerSocketMessageFilter::~PepperTCPServerSocketMessageFilter() {
  --g_num_instances;
}

// static
void PepperTCPServerSocketMessageFilter::SetNetworkContextForTesting(
    network::mojom::NetworkContext* network_context) {
  network_context_for_testing = network_context;
}

// static
size_t PepperTCPServerSocketMessageFilter::GetNumInstances() {
  return g_num_instances;
}

void PepperTCPServerSocketMessageFilter::OnFilterDestroyed() {
  ResourceMessageFilter::OnFilterDestroyed();
  // Need to close all mojo pipes the socket on the UI thread. Calling Close()
  // also ensures that future messages will be ignored, so the mojo pipes won't
  // be re-created, so after Close() runs, |this| can be safely deleted on the
  // IO thread.
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::UI},
      base::BindOnce(&PepperTCPServerSocketMessageFilter::Close, this));
}

scoped_refptr<base::TaskRunner>
PepperTCPServerSocketMessageFilter::OverrideTaskRunnerForMessage(
    const IPC::Message& message) {
  switch (message.type()) {
    case PpapiHostMsg_TCPServerSocket_Listen::ID:
    case PpapiHostMsg_TCPServerSocket_Accept::ID:
    case PpapiHostMsg_TCPServerSocket_StopListening::ID:
      return base::CreateSingleThreadTaskRunnerWithTraits({BrowserThread::UI});
  }
  return nullptr;
}

int32_t PepperTCPServerSocketMessageFilter::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperTCPServerSocketMessageFilter, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_TCPServerSocket_Listen,
                                      OnMsgListen)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_TCPServerSocket_Accept,
                                        OnMsgAccept)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(
        PpapiHostMsg_TCPServerSocket_StopListening, OnMsgStopListening)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperTCPServerSocketMessageFilter::OnMsgListen(
    const ppapi::host::HostMessageContext* context,
    const PP_NetAddress_Private& addr,
    int32_t backlog) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(context);

  SocketPermissionRequest request =
      pepper_socket_utils::CreateSocketPermissionRequest(
          content::SocketPermissionRequest::TCP_LISTEN, addr);
  if (!pepper_socket_utils::CanUseSocketAPIs(external_plugin_,
                                             private_api_,
                                             &request,
                                             render_process_id_,
                                             render_frame_id_)) {
    return PP_ERROR_NOACCESS;
  }

  net::IPAddressBytes address;
  uint16_t port;

  if (state_ != STATE_BEFORE_LISTENING ||
      !NetAddressPrivateImpl::NetAddressToIPEndPoint(addr, &address, &port)) {
    Close();
    return PP_ERROR_FAILED;
  }

  network::mojom::NetworkContext* network_context = network_context_for_testing;
  if (!network_context) {
    RenderProcessHost* render_process_host =
        RenderProcessHost::FromID(render_process_id_);
    network_context =
        render_process_host->GetStoragePartition()->GetNetworkContext();
    if (!network_context)
      return PP_ERROR_FAILED;
  }

  state_ = STATE_LISTEN_IN_PROGRESS;

  ppapi::host::ReplyMessageContext reply_context =
      context->MakeReplyMessageContext();

  network_context->CreateTCPServerSocket(
      net::IPEndPoint(net::IPAddress(address), port), backlog,
      pepper_socket_utils::PepperTCPNetworkAnnotationTag(),
      mojo::MakeRequest(&socket_),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&PepperTCPServerSocketMessageFilter::OnListenCompleted,
                         weak_ptr_factory_.GetWeakPtr(), reply_context),
          net::ERR_FAILED, base::nullopt /* local_addr_out */));

  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperTCPServerSocketMessageFilter::OnMsgAccept(
    const ppapi::host::HostMessageContext* context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(context);

  if (state_ != STATE_LISTENING)
    return PP_ERROR_FAILED;

  state_ = STATE_ACCEPT_IN_PROGRESS;
  ppapi::host::ReplyMessageContext reply_context(
      context->MakeReplyMessageContext());

  network::mojom::SocketObserverPtr socket_observer;
  network::mojom::SocketObserverRequest socket_observer_request =
      mojo::MakeRequest(&socket_observer);
  socket_->Accept(
      std::move(socket_observer),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&PepperTCPServerSocketMessageFilter::OnAcceptCompleted,
                         base::Unretained(this), reply_context,
                         std::move(socket_observer_request)),
          net::ERR_FAILED, base::nullopt /* remote_addr */,
          network::mojom::TCPConnectedSocketPtr(),
          mojo::ScopedDataPipeConsumerHandle(),
          mojo::ScopedDataPipeProducerHandle()));
  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperTCPServerSocketMessageFilter::OnMsgStopListening(
    const ppapi::host::HostMessageContext* context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(context);

  Close();
  return PP_OK;
}

void PepperTCPServerSocketMessageFilter::OnListenCompleted(
    const ppapi::host::ReplyMessageContext& context,
    int net_result,
    const base::Optional<net::IPEndPoint>& local_addr) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Exit early if this is called during Close().
  if (state_ == STATE_CLOSED) {
    DCHECK_EQ(net::ERR_FAILED, net_result);
    SendListenError(context, PP_ERROR_FAILED);
    return;
  }

  DCHECK(socket_.is_bound());
  DCHECK_EQ(state_, STATE_LISTEN_IN_PROGRESS);

  if (net_result != net::OK) {
    SendListenError(context, NetErrorToPepperError(net_result));
    socket_.reset();
    state_ = STATE_BEFORE_LISTENING;
    return;
  }

  if (!local_addr ||
      !NetAddressPrivateImpl::IPEndPointToNetAddress(
          local_addr->address().bytes(), local_addr->port(), &bound_addr_)) {
    SendListenError(context, PP_ERROR_FAILED);
    socket_.reset();
    state_ = STATE_BEFORE_LISTENING;
    return;
  }

#if defined(OS_CHROMEOS)
  OpenFirewallHole(context, *local_addr);
#else
  SendListenReply(context, PP_OK, bound_addr_);
  state_ = STATE_LISTENING;
#endif
}

#if defined(OS_CHROMEOS)
void PepperTCPServerSocketMessageFilter::OpenFirewallHole(
    const ppapi::host::ReplyMessageContext& context,
    const net::IPEndPoint& local_addr) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  pepper_socket_utils::FirewallHoleOpenCallback callback = base::BindRepeating(
      &PepperTCPServerSocketMessageFilter::OnFirewallHoleOpened, this, context);
  pepper_socket_utils::OpenTCPFirewallHole(local_addr, callback);
}

void PepperTCPServerSocketMessageFilter::OnFirewallHoleOpened(
    const ppapi::host::ReplyMessageContext& context,
    std::unique_ptr<chromeos::FirewallHole> hole) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  LOG_IF(WARNING, !hole.get()) << "Firewall hole could not be opened.";
  firewall_hole_.reset(hole.release());

  SendListenReply(context, PP_OK, bound_addr_);
  state_ = STATE_LISTENING;
}
#endif  // defined(OS_CHROMEOS)

void PepperTCPServerSocketMessageFilter::OnAcceptCompleted(
    const ppapi::host::ReplyMessageContext& context,
    network::mojom::SocketObserverRequest socket_observer_request,
    int net_result,
    const base::Optional<net::IPEndPoint>& remote_addr,
    network::mojom::TCPConnectedSocketPtr connected_socket,
    mojo::ScopedDataPipeConsumerHandle receive_stream,
    mojo::ScopedDataPipeProducerHandle send_stream) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Exit early if this is called during Close().
  if (state_ == STATE_CLOSED) {
    DCHECK_EQ(net::ERR_FAILED, net_result);
    SendListenError(context, PP_ERROR_FAILED);
    return;
  }

  DCHECK_EQ(state_, STATE_ACCEPT_IN_PROGRESS);

  state_ = STATE_LISTENING;
  if (net_result != net::OK) {
    SendAcceptError(context, NetErrorToPepperError(net_result));
    return;
  }

  if (!remote_addr || !connected_socket.is_bound()) {
    SendAcceptError(context, NetErrorToPepperError(net_result));
    return;
  }

  DCHECK(socket_observer_request.is_pending());

  PP_NetAddress_Private pp_remote_addr =
      NetAddressPrivateImpl::kInvalidNetAddress;

  if (!NetAddressPrivateImpl::IPEndPointToNetAddress(
          remote_addr->address().bytes(), remote_addr->port(),
          &pp_remote_addr)) {
    SendAcceptError(context, PP_ERROR_ADDRESS_INVALID);
    return;
  }

  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(
          &PepperTCPServerSocketMessageFilter::OnAcceptCompletedOnIOThread,
          this, context, connected_socket.PassInterface(),
          std::move(socket_observer_request), std::move(receive_stream),
          std::move(send_stream), bound_addr_, pp_remote_addr));
}

void PepperTCPServerSocketMessageFilter::OnAcceptCompletedOnIOThread(
    const ppapi::host::ReplyMessageContext& context,
    network::mojom::TCPConnectedSocketPtrInfo connected_socket,
    network::mojom::SocketObserverRequest socket_observer_request,
    mojo::ScopedDataPipeConsumerHandle receive_stream,
    mojo::ScopedDataPipeProducerHandle send_stream,
    PP_NetAddress_Private pp_local_addr,
    PP_NetAddress_Private pp_remote_addr) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // |factory_| is guaranteed to be non-NULL here. Only those instances created
  // in CONNECTED state have a NULL |factory_|, while getting here requires
  // LISTENING state.
  std::unique_ptr<ppapi::host::ResourceHost> host =
      factory_->CreateAcceptedTCPSocket(
          instance_, ppapi::TCP_SOCKET_VERSION_PRIVATE,
          std::move(connected_socket), std::move(socket_observer_request),
          std::move(receive_stream), std::move(send_stream));
  if (!host) {
    SendAcceptError(context, PP_ERROR_NOSPACE);
    return;
  }

  int pending_host_id = ppapi_host_->AddPendingResourceHost(std::move(host));
  if (pending_host_id) {
    SendAcceptReply(context, PP_OK, pending_host_id, pp_local_addr,
                    pp_remote_addr);
  } else {
    SendAcceptError(context, PP_ERROR_NOSPACE);
  }
}

void PepperTCPServerSocketMessageFilter::Close() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Need to do these first, as destroying Mojo pipes may invoke some of the
  // callbacks with failure messages.
  weak_ptr_factory_.InvalidateWeakPtrs();
  state_ = STATE_CLOSED;

  socket_.reset();
#if defined(OS_CHROMEOS)
  firewall_hole_.reset();
#endif  // defined(OS_CHROMEOS)
}

void PepperTCPServerSocketMessageFilter::SendListenReply(
    const ppapi::host::ReplyMessageContext& context,
    int32_t pp_result,
    const PP_NetAddress_Private& local_addr) {
  ppapi::host::ReplyMessageContext reply_context(context);
  reply_context.params.set_result(pp_result);
  SendReply(reply_context,
            PpapiPluginMsg_TCPServerSocket_ListenReply(local_addr));
}

void PepperTCPServerSocketMessageFilter::SendListenError(
    const ppapi::host::ReplyMessageContext& context,
    int32_t pp_result) {
  SendListenReply(
      context, pp_result, NetAddressPrivateImpl::kInvalidNetAddress);
}

void PepperTCPServerSocketMessageFilter::SendAcceptReply(
    const ppapi::host::ReplyMessageContext& context,
    int32_t pp_result,
    int pending_resource_id,
    const PP_NetAddress_Private& local_addr,
    const PP_NetAddress_Private& remote_addr) {
  ppapi::host::ReplyMessageContext reply_context(context);
  reply_context.params.set_result(pp_result);
  SendReply(reply_context,
            PpapiPluginMsg_TCPServerSocket_AcceptReply(
                pending_resource_id, local_addr, remote_addr));
}

void PepperTCPServerSocketMessageFilter::SendAcceptError(
    const ppapi::host::ReplyMessageContext& context,
    int32_t pp_result) {
  SendAcceptReply(context,
                  pp_result,
                  0,
                  NetAddressPrivateImpl::kInvalidNetAddress,
                  NetAddressPrivateImpl::kInvalidNetAddress);
}

}  // namespace content
