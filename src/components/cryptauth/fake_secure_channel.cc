// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/fake_secure_channel.h"

#include <utility>

#include "base/logging.h"

namespace cryptauth {

FakeSecureChannel::SentMessage::SentMessage(const std::string& feature,
                                            const std::string& payload)
    : feature(feature), payload(payload) {}

FakeSecureChannel::FakeSecureChannel(std::unique_ptr<Connection> connection)
    : SecureChannel(std::move(connection)) {}

FakeSecureChannel::~FakeSecureChannel() {
  if (destructor_callback_)
    std::move(destructor_callback_).Run();
}

void FakeSecureChannel::ChangeStatus(const Status& new_status) {
  Status old_status = status_;
  status_ = new_status;

  // Copy to prevent channel from being removed during handler.
  std::vector<Observer*> observers_copy = observers_;
  for (auto* observer : observers_copy) {
    observer->OnSecureChannelStatusChanged(this, old_status, status_);
  }
}

void FakeSecureChannel::ReceiveMessage(const std::string& feature,
                                       const std::string& payload) {
  // Copy to prevent channel from being removed during handler.
  std::vector<Observer*> observers_copy = observers_;
  for (auto* observer : observers_copy)
    observer->OnMessageReceived(this, feature, payload);
}

void FakeSecureChannel::CompleteSendingMessage(int sequence_number) {
  DCHECK(next_sequence_number_ > sequence_number);
  // Copy to prevent channel from being removed during handler.
  std::vector<Observer*> observers_copy = observers_;
  for (auto* observer : observers_copy)
    observer->OnMessageSent(this, sequence_number);
}

void FakeSecureChannel::Initialize() {
  was_initialized_ = true;
  ChangeStatus(Status::CONNECTING);
}

int FakeSecureChannel::SendMessage(const std::string& feature,
                                   const std::string& payload) {
  sent_messages_.push_back(SentMessage(feature, payload));
  return next_sequence_number_++;
}

void FakeSecureChannel::Disconnect() {
  if (status() == Status::DISCONNECTING || status() == Status::DISCONNECTED)
    return;

  if (status() == Status::CONNECTING)
    ChangeStatus(Status::DISCONNECTED);
  else
    ChangeStatus(Status::DISCONNECTING);
}

void FakeSecureChannel::AddObserver(Observer* observer) {
  observers_.push_back(observer);
}

void FakeSecureChannel::RemoveObserver(Observer* observer) {
  observers_.erase(std::find(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

void FakeSecureChannel::GetConnectionRssi(
    base::OnceCallback<void(base::Optional<int32_t>)> callback) {
  std::move(callback).Run(rssi_to_return_);
}

base::Optional<std::string> FakeSecureChannel::GetChannelBindingData() {
  return channel_binding_data_;
}

}  // namespace cryptauth
