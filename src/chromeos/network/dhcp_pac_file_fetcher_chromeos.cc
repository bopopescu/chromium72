// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/dhcp_pac_file_fetcher_chromeos.h"

#include "base/location.h"
#include "base/task_runner_util.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "net/proxy_resolution/pac_file_fetcher.h"
#include "net/proxy_resolution/pac_file_fetcher_impl.h"
#include "net/url_request/url_request_context.h"

namespace chromeos {

namespace {

// Runs on NetworkHandler::Get()->message_loop().
std::string GetPacUrlFromDefaultNetwork() {
  if (!NetworkHandler::IsInitialized())
    return std::string();
  const NetworkState* default_network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
  if (default_network)
    return default_network->GetWebProxyAutoDiscoveryUrl().spec();
  return std::string();
}

}  // namespace

DhcpPacFileFetcherChromeos::DhcpPacFileFetcherChromeos(
    net::URLRequestContext* url_request_context)
    : weak_ptr_factory_(this) {
  DCHECK(url_request_context);
  pac_file_fetcher_ = net::PacFileFetcherImpl::Create(url_request_context);
  if (NetworkHandler::IsInitialized())
    network_handler_task_runner_ = NetworkHandler::Get()->task_runner();
}

DhcpPacFileFetcherChromeos::~DhcpPacFileFetcherChromeos() = default;

int DhcpPacFileFetcherChromeos::Fetch(
    base::string16* utf16_text,
    net::CompletionOnceCallback callback,
    const net::NetLogWithSource& net_log,
    const net::NetworkTrafficAnnotationTag traffic_annotation) {
  if (!network_handler_task_runner_.get())
    return net::ERR_PAC_NOT_IN_DHCP;
  CHECK(callback);
  // DhcpPacFileFetcher only allows one Fetch in progress at a time.
  CHECK(!callback_);
  callback_ = std::move(callback);
  base::PostTaskAndReplyWithResult(
      network_handler_task_runner_.get(), FROM_HERE,
      base::BindOnce(&GetPacUrlFromDefaultNetwork),
      base::BindOnce(&DhcpPacFileFetcherChromeos::ContinueFetch,
                     weak_ptr_factory_.GetWeakPtr(), utf16_text,
                     traffic_annotation));
  return net::ERR_IO_PENDING;
}

void DhcpPacFileFetcherChromeos::Cancel() {
  callback_.Reset();
  pac_file_fetcher_->Cancel();
  // Invalidate any pending callbacks (i.e. calls to ContinueFetch).
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void DhcpPacFileFetcherChromeos::OnShutdown() {
  callback_.Reset();
  pac_file_fetcher_->OnShutdown();
}

const GURL& DhcpPacFileFetcherChromeos::GetPacURL() const {
  return pac_url_;
}

std::string DhcpPacFileFetcherChromeos::GetFetcherName() const {
  return "chromeos";
}

void DhcpPacFileFetcherChromeos::ContinueFetch(
    base::string16* utf16_text,
    const net::NetworkTrafficAnnotationTag traffic_annotation,
    std::string pac_url) {
  NET_LOG_EVENT("DhcpPacFileFetcher", pac_url);
  pac_url_ = GURL(pac_url);
  if (pac_url_.is_empty()) {
    std::move(callback_).Run(net::ERR_PAC_NOT_IN_DHCP);
    return;
  }

  int result = pac_file_fetcher_->Fetch(
      pac_url_, utf16_text,
      base::BindOnce(&DhcpPacFileFetcherChromeos::OnFetchCompleted,
                     weak_ptr_factory_.GetWeakPtr()),
      traffic_annotation);
  if (result != net::ERR_IO_PENDING)
    std::move(callback_).Run(result);
}

void DhcpPacFileFetcherChromeos::OnFetchCompleted(int result) {
  std::move(callback_).Run(result);
}

}  // namespace chromeos
