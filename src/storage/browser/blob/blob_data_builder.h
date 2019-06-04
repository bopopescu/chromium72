// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BROWSER_BLOB_BLOB_DATA_BUILDER_H_
#define STORAGE_BROWSER_BLOB_BLOB_DATA_BUILDER_H_

#include <stddef.h>
#include <stdint.h>
#include <ostream>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/numerics/checked_math.h"
#include "storage/browser/blob/blob_data_item.h"
#include "storage/browser/blob/blob_data_snapshot.h"
#include "storage/browser/blob/blob_entry.h"
#include "storage/browser/blob/shareable_blob_data_item.h"
#include "storage/browser/blob/shareable_file_reference.h"
#include "storage/browser/fileapi/file_system_context.h"
#include "storage/browser/storage_browser_export.h"

namespace disk_cache {
class Entry;
}

namespace network {
class DataElement;
}

namespace storage {
class BlobSliceTest;
class BlobStorageContext;
class BlobStorageRegistry;

// This class is used to build blobs. It also facilitates the operation of
// 'pending' data, where the user knows the size and existence of a file or
// bytes item, but we don't have the memory or file yet. See AppendFuture* and
// PopulateFuture* methods for more description. Use
// BlobDataHandle::GetBlobStatus to check for an error after creating the blob.
class STORAGE_EXPORT BlobDataBuilder {
 public:
  using DataHandle = BlobDataItem::DataHandle;
  using ItemCopyEntry = BlobEntry::ItemCopyEntry;

  explicit BlobDataBuilder(const std::string& uuid);
  ~BlobDataBuilder();

  const std::string& uuid() const { return uuid_; }

  // Validates the data element that was sent over IPC, and copies the data if
  // it's a 'bytes' element. Data elements of BYTES_DESCRIPTION or
  // DISK_CACHE_ENTRY types are not valid IPC data element types, and cannot be
  // given to this method.
  // |blob_registry| is needed for data elements of type BLOB.
  void AppendIPCDataElement(
      const network::DataElement& ipc_data,
      const BlobStorageRegistry& blob_registry);

  // Copies the given data into the blob.
  void AppendData(const std::string& data) {
    AppendData(data.c_str(), data.size());
  }

  // Copies the given data into the blob.
  void AppendData(const char* data, size_t length);

  // Represents a piece of unpopulated data.
  class STORAGE_EXPORT FutureData {
   public:
    FutureData(FutureData&&);
    FutureData& operator=(FutureData&&);
    ~FutureData();

    // Populates a part of an item previously allocated with AppendFutureData.
    // The first call to PopulateFutureData lazily allocates the memory for the
    // data element.
    // Returns true if:
    // * The offset and length are valid, and
    // * data is a valid pointer.
    bool Populate(base::span<const char> data, size_t offset = 0) const;

    // Same as Populate, but rather than passing in the data to be
    // copied, this method returns a pointer where the caller can copy |length|
    // bytes of data to.
    // Returns nullptr if:
    // * The offset and length are not valid.
    base::span<char> GetDataToPopulate(size_t offset, size_t length) const;

   private:
    friend class BlobDataBuilder;
    FutureData(scoped_refptr<BlobDataItem>);

    scoped_refptr<BlobDataItem> item_;
    DISALLOW_COPY_AND_ASSIGN(FutureData);
  };

  // Adds an item that is flagged for future data population. The memory is not
  // allocated until the first call to PopulateFutureData. Returns the index of
  // the item (to be used in PopulateFutureData). |length| cannot be 0.
  FutureData AppendFutureData(size_t length);

  // Represents an unpopulated file.
  class STORAGE_EXPORT FutureFile {
   public:
    FutureFile(FutureFile&&);
    FutureFile& operator=(FutureFile&&);
    ~FutureFile();

    // Populates a part of an item previously allocated with AppendFutureFile.
    // Returns false if:
    // * The item has already been populated.
    bool Populate(scoped_refptr<ShareableFileReference> file_reference,
                  const base::Time& expected_modification_time);

   private:
    friend class BlobDataBuilder;
    FutureFile(scoped_refptr<BlobDataItem>);

    scoped_refptr<BlobDataItem> item_;
    DISALLOW_COPY_AND_ASSIGN(FutureFile);
  };

  // Adds an item that is flagged for future data population. Use
  // 'PopulateFutureFile' to set the file path and expected modification time
  // of this file. Returns the index of the item (to be used in
  // PopulateFutureFile). |length| cannot be 0.
  // Data for multiple items can be stored in the same 'future' file, just at
  // different offsets and lengths. The |file_id| is used to differentiate
  // between different 'future' files that will be used to store data for these
  // items.
  FutureFile AppendFutureFile(uint64_t offset,
                              uint64_t length,
                              uint64_t file_id);

  // You must know the length of the file, you cannot use kuint64max to specify
  // the whole file.  This method creates a ShareableFileReference to the given
  // file, which is stored in this builder.
  void AppendFile(const base::FilePath& file_path,
                  uint64_t offset,
                  uint64_t length,
                  const base::Time& expected_modification_time);

  void AppendBlob(const std::string& uuid,
                  uint64_t offset,
                  uint64_t length,
                  const BlobStorageRegistry& blob_registry);
  void AppendBlob(const std::string& uuid,
                  const BlobStorageRegistry& blob_registry);

  void AppendFileSystemFile(
      const GURL& url,
      uint64_t offset,
      uint64_t length,
      const base::Time& expected_modification_time,
      scoped_refptr<FileSystemContext> file_system_context);

  void AppendDiskCacheEntry(scoped_refptr<DataHandle> data_handle,
                            disk_cache::Entry* disk_cache_entry,
                            int disk_cache_stream_index);

  // The content of the side data is accessible with BlobReader::ReadSideData().
  void AppendDiskCacheEntryWithSideData(scoped_refptr<DataHandle> data_handle,
                                        disk_cache::Entry* disk_cache_entry,
                                        int disk_cache_stream_index,
                                        int disk_cache_side_stream_index);

  void set_content_type(const std::string& content_type) {
    content_type_ = content_type;
  }

  void set_content_disposition(const std::string& content_disposition) {
    content_disposition_ = content_disposition;
  }

  std::unique_ptr<BlobDataSnapshot> CreateSnapshot() const;

  const std::vector<scoped_refptr<ShareableBlobDataItem>>& items() const {
    return items_;
  }

  std::vector<scoped_refptr<ShareableBlobDataItem>> ReleaseItems() {
    return std::move(items_);
  }

  const std::vector<scoped_refptr<ShareableBlobDataItem>>&
  pending_transport_items() const {
    return pending_transport_items_;
  }

  std::vector<scoped_refptr<ShareableBlobDataItem>>
  ReleasePendingTransportItems() {
    return std::move(pending_transport_items_);
  }

  const std::vector<ItemCopyEntry>& copies() const { return copies_; }

  std::vector<ItemCopyEntry> ReleaseCopies() { return std::move(copies_); }

  const std::set<std::string>& dependent_blobs() const {
    return dependent_blob_uuids_;
  }

  bool IsValid() const {
    return !(found_memory_transport_ && found_file_transport_) &&
           !has_blob_errors_ && total_size_.IsValid() &&
           transport_quota_needed_.IsValid() && copy_quota_needed_.IsValid();
  }

  bool found_memory_transport() const { return found_memory_transport_; }

  bool found_file_transport() const { return found_file_transport_; }

  uint64_t total_size() const {
    return IsValid() ? total_size_.ValueOrDie() : 0u;
  }

  uint64_t total_memory_size() const {
    return IsValid() ? total_memory_size_.ValueOrDie() : 0u;
  }

  uint64_t transport_quota_needed() const {
    return IsValid() ? transport_quota_needed_.ValueOrDie() : 0u;
  }

  uint64_t copy_quota_needed() const {
    return IsValid() ? copy_quota_needed_.ValueOrDie() : 0u;
  }

 private:
  friend class BlobStorageContext;
  friend STORAGE_EXPORT void PrintTo(const BlobDataBuilder& x,
                                     ::std::ostream* os);
  friend class BlobSliceTest;

  void SliceBlob(const BlobEntry* entry,
                 uint64_t slice_offset,
                 uint64_t slice_size);

  std::string uuid_;
  std::string content_type_;
  std::string content_disposition_;

  base::CheckedNumeric<uint64_t> total_size_;
  base::CheckedNumeric<uint64_t> total_memory_size_;
  base::CheckedNumeric<uint64_t> transport_quota_needed_;
  base::CheckedNumeric<uint64_t> copy_quota_needed_;
  bool has_blob_errors_ = false;
  bool found_memory_transport_ = false;
  bool found_file_transport_ = false;

  std::vector<scoped_refptr<ShareableBlobDataItem>> items_;
  std::vector<scoped_refptr<ShareableBlobDataItem>> pending_transport_items_;
  std::set<std::string> dependent_blob_uuids_;
  std::vector<ItemCopyEntry> copies_;

  DISALLOW_COPY_AND_ASSIGN(BlobDataBuilder);
};

#if defined(UNIT_TEST)
inline bool operator==(const BlobDataSnapshot& a, const BlobDataBuilder& b) {
  return a == *b.CreateSnapshot();
}

inline bool operator==(const BlobDataBuilder& a, const BlobDataBuilder& b) {
  return *a.CreateSnapshot() == b;
}

inline bool operator==(const BlobDataBuilder& a, const BlobDataSnapshot& b) {
  return b == a;
}

inline bool operator!=(const BlobDataSnapshot& a, const BlobDataBuilder& b) {
  return !(a == b);
}

inline bool operator!=(const BlobDataBuilder& a, const BlobDataBuilder& b) {
  return !(a == b);
}

inline bool operator!=(const BlobDataBuilder& a, const BlobDataSnapshot& b) {
  return b != a;
}

#endif  // defined(UNIT_TEST)

}  // namespace storage
#endif  // STORAGE_BROWSER_BLOB_BLOB_DATA_BUILDER_H_
