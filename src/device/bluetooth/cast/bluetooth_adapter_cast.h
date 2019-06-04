// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_CAST_BLUETOOTH_ADAPTER_CAST_H_
#define DEVICE_BLUETOOTH_CAST_BLUETOOTH_ADAPTER_CAST_H_

#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "chromecast/device/bluetooth/le/gatt_client_manager.h"
#include "chromecast/device/bluetooth/le/le_scan_manager.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_export.h"

namespace device {

class BluetoothDeviceCast;

// This class allows callers to discover, connect to, and interact with remote
// BLE peripherals. Upon creation, it scans for devices and observes the Cast
// bluetooth stack for events in remote BLE peripherals. This class does not
// support functionality needed only for Bluetooth Classic. It also does not
// act as a GATT server.
//
// THREADING
// This class is created and called on a single thread. It makes aysnchronous
// calls to the Cast bluetooth stack, which may live on another thread. Unless
// noted otherwise, callbacks will always be posted on the calling thread.
class DEVICE_BLUETOOTH_EXPORT BluetoothAdapterCast
    : public BluetoothAdapter,
      chromecast::bluetooth::GattClientManager::Observer,
      chromecast::bluetooth::LeScanManager::Observer {
 public:
  // Do not call this constructor directly; use CreateAdapter() instead. Neither
  // |gatt_client_manager| nor |le_scan_manager| are owned by this class. Both
  // must outlive |this|.
  BluetoothAdapterCast(
      chromecast::bluetooth::GattClientManager* gatt_client_manager,
      chromecast::bluetooth::LeScanManager* le_scan_manager);

  // BluetoothAdapter implementation:
  std::string GetAddress() const override;
  std::string GetName() const override;
  void SetName(const std::string& name,
               const base::Closure& callback,
               const ErrorCallback& error_callback) override;
  bool IsInitialized() const override;
  bool IsPresent() const override;
  bool IsPowered() const override;
  void SetPowered(bool powered,
                  const base::Closure& callback,
                  const ErrorCallback& error_callback) override;
  bool IsDiscoverable() const override;
  void SetDiscoverable(bool discoverable,
                       const base::Closure& callback,
                       const ErrorCallback& error_callback) override;
  bool IsDiscovering() const override;
  UUIDList GetUUIDs() const override;
  void CreateRfcommService(
      const BluetoothUUID& uuid,
      const ServiceOptions& options,
      const CreateServiceCallback& callback,
      const CreateServiceErrorCallback& error_callback) override;
  void CreateL2capService(
      const BluetoothUUID& uuid,
      const ServiceOptions& options,
      const CreateServiceCallback& callback,
      const CreateServiceErrorCallback& error_callback) override;
  void RegisterAdvertisement(
      std::unique_ptr<BluetoothAdvertisement::Data> advertisement_data,
      const CreateAdvertisementCallback& callback,
      const AdvertisementErrorCallback& error_callback) override;
  void SetAdvertisingInterval(
      const base::TimeDelta& min,
      const base::TimeDelta& max,
      const base::Closure& callback,
      const AdvertisementErrorCallback& error_callback) override;
  void ResetAdvertising(
      const base::Closure& callback,
      const AdvertisementErrorCallback& error_callback) override;
  BluetoothLocalGattService* GetGattService(
      const std::string& identifier) const override;
  bool SetPoweredImpl(bool powered) override;
  void AddDiscoverySession(
      BluetoothDiscoveryFilter* discovery_filter,
      const base::Closure& callback,
      DiscoverySessionErrorCallback error_callback) override;
  void RemoveDiscoverySession(
      BluetoothDiscoveryFilter* discovery_filter,
      const base::Closure& callback,
      DiscoverySessionErrorCallback error_callback) override;
  void SetDiscoveryFilter(
      std::unique_ptr<BluetoothDiscoveryFilter> discovery_filter,
      const base::Closure& callback,
      DiscoverySessionErrorCallback error_callback) override;
  void RemovePairingDelegateInternal(
      BluetoothDevice::PairingDelegate* pairing_delegate) override;

  // Return a WeakPtr for this class. Must be called on the sequence on which
  // this class was created.
  // TODO(slan): Remove this once this class talks to a dedicated Bluetooth
  // service (b/76155468)
  base::WeakPtr<BluetoothAdapterCast> GetWeakPtr();

  // |factory_cb| is used to inject a factory method from ChromecastService into
  // this class. It will be invoked when Create() is called.
  // TODO(slan): Remove this once this class talks to a dedicated Bluetooth
  // service (b/76155468)
  using FactoryCb =
      base::RepeatingCallback<base::WeakPtr<BluetoothAdapterCast>()>;
  static void SetFactory(FactoryCb factory_cb);

  // Resets the factory callback for test scenarios.
  static void ResetFactoryForTest();

  // Creates a BluetoothAdapterCast using the |factory_cb| set in SetFactory().
  // This method is intended to be called only by the WebBluetooth code in
  // //device/blutooth/. |callback| will be executed asynchronously
  // on the calling sequence.
  static base::WeakPtr<BluetoothAdapter> Create(InitCallback callback);

 private:
  ~BluetoothAdapterCast() override;

  // chromecast::bluetooth::GattClientManager::Observer implementation:
  void OnConnectChanged(
      scoped_refptr<chromecast::bluetooth::RemoteDevice> device,
      bool connected) override;
  void OnMtuChanged(scoped_refptr<chromecast::bluetooth::RemoteDevice> device,
                    int mtu) override;
  void OnCharacteristicNotification(
      scoped_refptr<chromecast::bluetooth::RemoteDevice> device,
      scoped_refptr<chromecast::bluetooth::RemoteCharacteristic> characteristic,
      std::vector<uint8_t> value) override;

  // chromecast::bluetooth::LeScanManager::Observer implementation:
  void OnNewScanResult(chromecast::bluetooth::LeScanResult) override;
  void OnScanEnableChanged(bool enabled) override;

  // Helper method to access |devices_| as BluetoothDeviceCast*.
  BluetoothDeviceCast* GetCastDevice(const std::string& address);

  // Runs |callback|. After this method returns, |this| is initialized. This
  // must run on the same sequence on which |this| is created.
  void InitializeAsynchronously(InitCallback callback);

  // Creates a BluetoothDeviceCast for |remote_device|, adds it to |devices_|,
  // and updates observers.
  void AddDevice(
      scoped_refptr<chromecast::bluetooth::RemoteDevice> remote_device);

  // Called when the scanner has enabled scanning.
  void OnScanEnabled(
      std::unique_ptr<chromecast::bluetooth::LeScanManager::ScanHandle>
          scan_handle);
  void OnGetDevice(scoped_refptr<chromecast::bluetooth::RemoteDevice> device);
  void OnGetScanResults(
      std::vector<chromecast::bluetooth::LeScanResult> results);

  struct DiscoveryParams {
    DiscoveryParams(device::BluetoothDiscoveryFilter* filter,
                    base::Closure success_callback,
                    DiscoverySessionErrorCallback error_callback);
    DiscoveryParams(DiscoveryParams&& params) noexcept;
    DiscoveryParams& operator=(DiscoveryParams&& params);
    ~DiscoveryParams();
    device::BluetoothDiscoveryFilter* filter = nullptr;
    base::Closure success_callback;
    DiscoverySessionErrorCallback error_callback;
  };

  std::queue<DiscoveryParams> pending_discovery_requests_;

  int num_discovery_sessions_ = 0;

  // Maps address to ScanResults received from |le_scan_manager_|.
  std::map<std::string, std::list<chromecast::bluetooth::LeScanResult>>
      pending_scan_results_;

  chromecast::bluetooth::GattClientManager* const gatt_client_manager_;
  chromecast::bluetooth::LeScanManager* const le_scan_manager_;

  std::unique_ptr<chromecast::bluetooth::LeScanManager::ScanHandle>
      scan_handle_;

  bool powered_ = true;
  bool initialized_ = false;

  std::string name_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<BluetoothAdapterCast> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothAdapterCast);
};

}  // namespace device

#endif  // DEVICE_BLUETOOTH_CAST_BLUETOOTH_ADAPTER_CAST_H_
