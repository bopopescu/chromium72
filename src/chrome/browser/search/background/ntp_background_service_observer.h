// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_BACKGROUND_NTP_BACKGROUND_SERVICE_OBSERVER_H_
#define CHROME_BROWSER_SEARCH_BACKGROUND_NTP_BACKGROUND_SERVICE_OBSERVER_H_

// Observer for NtpBackgroundService.
class NtpBackgroundServiceObserver {
 public:
  // Called when the CollectionInfo is updated, usually as the result of a
  // FetchCollectionInfo() call on the service. You can get the new data via
  // NtpBackgroundService::collection_info().
  virtual void OnCollectionInfoAvailable() = 0;

  // Called when the CollectionImages are updated, usually as the result of a
  // FetchCollectionImageInfo() call on the service. You can get the new data
  // via NtpBackgroundService::collection_images().
  virtual void OnCollectionImagesAvailable() = 0;

  // Called when the AlbumInfo is updated, usually as the result of a
  // PersonalAlbumsRequestOption() call on the service. You can get the new
  // data via NtpBackgroundService::album_info().
  virtual void OnAlbumInfoAvailable() = 0;

  // Called when the AlbumPhotos are updated, usually as the result of a
  // SettingPreviewRequest() call on the service. You can get the new data via
  // NtpBackgroundService::album_photos().
  virtual void OnAlbumPhotosAvailable() = 0;

  // Called when the OnNtpBackgroundService is shutting down. Observers that
  // might outlive the service should use this to unregister themselves, and
  // clear out any pointers to the service they might hold.
  virtual void OnNtpBackgroundServiceShuttingDown() {}
};

#endif  // CHROME_BROWSER_SEARCH_BACKGROUND_NTP_BACKGROUND_SERVICE_OBSERVER_H_
