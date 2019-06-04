// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_MULTIDEVICE_SETUP_ELIGIBLE_HOST_DEVICES_PROVIDER_IMPL_H_
#define CHROMEOS_SERVICES_MULTIDEVICE_SETUP_ELIGIBLE_HOST_DEVICES_PROVIDER_IMPL_H_

#include "base/macros.h"
#include "chromeos/services/device_sync/public/cpp/device_sync_client.h"
#include "chromeos/services/multidevice_setup/eligible_host_devices_provider.h"
#include "components/cryptauth/remote_device_ref.h"

namespace chromeos {

namespace multidevice_setup {

// Concrete EligibleHostDevicesProvider implementation, which utilizes
// DeviceSyncClient to fetch devices.
class EligibleHostDevicesProviderImpl
    : public EligibleHostDevicesProvider,
      public device_sync::DeviceSyncClient::Observer {
 public:
  class Factory {
   public:
    static Factory* Get();
    static void SetFactoryForTesting(Factory* factory);
    virtual ~Factory();
    virtual std::unique_ptr<EligibleHostDevicesProvider> BuildInstance(
        device_sync::DeviceSyncClient* device_sync_client);

   private:
    static Factory* test_factory_;
  };

  ~EligibleHostDevicesProviderImpl() override;

 private:
  EligibleHostDevicesProviderImpl(
      device_sync::DeviceSyncClient* device_sync_client);

  // EligibleHostDevicesProvider:
  cryptauth::RemoteDeviceRefList GetEligibleHostDevices() const override;

  // device_sync::DeviceSyncClient::Observer:
  void OnNewDevicesSynced() override;

  void UpdateEligibleDevicesSet();

  device_sync::DeviceSyncClient* device_sync_client_;

  cryptauth::RemoteDeviceRefList eligible_devices_from_last_sync_;

  DISALLOW_COPY_AND_ASSIGN(EligibleHostDevicesProviderImpl);
};

}  // namespace multidevice_setup

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_MULTIDEVICE_SETUP_ELIGIBLE_HOST_DEVICES_PROVIDER_IMPL_H_
