// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CRYPTAUTH_FAKE_CRYPTAUTH_DEVICE_MANAGER_H_
#define COMPONENTS_CRYPTAUTH_FAKE_CRYPTAUTH_DEVICE_MANAGER_H_

#include <memory>

#include "base/macros.h"
#include "components/cryptauth/cryptauth_device_manager.h"
#include "components/cryptauth/proto/cryptauth_api.pb.h"

namespace cryptauth {

// Test double for CryptAuthDeviceManager.
class FakeCryptAuthDeviceManager : public CryptAuthDeviceManager {
 public:
  FakeCryptAuthDeviceManager();
  ~FakeCryptAuthDeviceManager() override;

  bool has_started() { return has_started_; }

  void set_last_sync_time(base::Time last_sync_time) {
    last_sync_time_ = last_sync_time;
  }

  void set_time_to_next_attempt(base::TimeDelta time_to_next_attempt) {
    time_to_next_attempt_ = time_to_next_attempt;
  }

  std::vector<ExternalDeviceInfo>& synced_devices() { return synced_devices_; }

  void set_synced_devices(std::vector<ExternalDeviceInfo> synced_devices) {
    synced_devices_ = synced_devices;
  }

  std::vector<ExternalDeviceInfo>& unlock_keys() { return unlock_keys_; }

  void set_unlock_keys(const std::vector<ExternalDeviceInfo>& unlock_keys) {
    unlock_keys_ = unlock_keys;
  }

  std::vector<ExternalDeviceInfo>& pixel_unlock_keys() {
    return pixel_unlock_keys_;
  }

  void set_pixel_unlock_keys(
      const std::vector<ExternalDeviceInfo>& pixel_unlock_keys) {
    pixel_unlock_keys_ = pixel_unlock_keys;
  }

  std::vector<ExternalDeviceInfo>& tether_hosts() { return tether_hosts_; }

  void set_tether_hosts(const std::vector<ExternalDeviceInfo>& tether_hosts) {
    tether_hosts_ = tether_hosts;
  }

  std::vector<ExternalDeviceInfo>& pixel_tether_hosts() {
    return pixel_tether_hosts_;
  }

  void set_pixel_tether_hosts(
      const std::vector<ExternalDeviceInfo>& pixel_tether_hosts) {
    pixel_tether_hosts_ = pixel_tether_hosts;
  }

  void set_is_recovering_from_failure(bool is_recovering_from_failure) {
    is_recovering_from_failure_ = is_recovering_from_failure;
  }

  void set_is_sync_in_progress(bool is_sync_in_progress) {
    is_sync_in_progress_ = is_sync_in_progress;
  }

  // Finishes the active sync; should only be called if a sync is in progress
  // due to a previous call to ForceSyncNow(). If |sync_result| is SUCCESS,
  // |sync_finish_time| will be stored as the last sync time and will be
  // returned by future calls to GetLastSyncTime().
  void FinishActiveSync(SyncResult sync_result,
                        DeviceChangeResult device_change_result,
                        base::Time sync_finish_time = base::Time());

  // Make these functions public for testing.
  using CryptAuthDeviceManager::NotifySyncStarted;
  using CryptAuthDeviceManager::NotifySyncFinished;

  // CryptAuthDeviceManager:
  void Start() override;
  void ForceSyncNow(InvocationReason invocation_reason) override;
  base::Time GetLastSyncTime() const override;
  base::TimeDelta GetTimeToNextAttempt() const override;
  bool IsSyncInProgress() const override;
  bool IsRecoveringFromFailure() const override;
  std::vector<ExternalDeviceInfo> GetSyncedDevices() const override;
  std::vector<ExternalDeviceInfo> GetUnlockKeys() const override;
  std::vector<ExternalDeviceInfo> GetPixelUnlockKeys() const override;
  std::vector<ExternalDeviceInfo> GetTetherHosts() const override;
  std::vector<ExternalDeviceInfo> GetPixelTetherHosts() const override;

 private:
  bool has_started_ = false;
  bool is_sync_in_progress_ = false;
  bool is_recovering_from_failure_ = false;
  base::Time last_sync_time_;
  base::TimeDelta time_to_next_attempt_;
  std::vector<ExternalDeviceInfo> synced_devices_;
  std::vector<ExternalDeviceInfo> unlock_keys_;
  std::vector<ExternalDeviceInfo> pixel_unlock_keys_;
  std::vector<ExternalDeviceInfo> tether_hosts_;
  std::vector<ExternalDeviceInfo> pixel_tether_hosts_;

  DISALLOW_COPY_AND_ASSIGN(FakeCryptAuthDeviceManager);
};

}  // namespace cryptauth

#endif  // COMPONENTS_CRYPTAUTH_FAKE_CRYPTAUTH_DEVICE_MANAGER_H_
