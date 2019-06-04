// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_cookies_view_handler.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/number_formatting.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/browsing_data_cache_storage_helper.h"
#include "chrome/browser/browsing_data/browsing_data_channel_id_helper.h"
#include "chrome/browser/browsing_data/browsing_data_cookie_helper.h"
#include "chrome/browser/browsing_data/browsing_data_database_helper.h"
#include "chrome/browser/browsing_data/browsing_data_file_system_helper.h"
#include "chrome/browser/browsing_data/browsing_data_flash_lso_helper.h"
#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"
#include "chrome/browser/browsing_data/browsing_data_local_storage_helper.h"
#include "chrome/browser/browsing_data/browsing_data_media_license_helper.h"
#include "chrome/browser/browsing_data/browsing_data_quota_helper.h"
#include "chrome/browser/browsing_data/browsing_data_service_worker_helper.h"
#include "chrome/browser/browsing_data/browsing_data_shared_worker_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/cookies_tree_model_util.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

namespace storage {
class FileSystemContext;
}

namespace {

constexpr char kEffectiveTopLevelDomainPlus1Name[] = "etldPlus1";
constexpr char kNumCookies[] = "numCookies";

int GetCategoryLabelID(CookieTreeNode::DetailedInfo::NodeType node_type) {
  constexpr struct {
    CookieTreeNode::DetailedInfo::NodeType node_type;
    int id;
  } kCategoryLabels[] = {
      // Multiple keys (node_type) may have the same value (id).

      {CookieTreeNode::DetailedInfo::TYPE_DATABASES,
       IDS_SETTINGS_COOKIES_DATABASE_STORAGE},
      {CookieTreeNode::DetailedInfo::TYPE_DATABASE,
       IDS_SETTINGS_COOKIES_DATABASE_STORAGE},

      {CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGES,
       IDS_SETTINGS_COOKIES_LOCAL_STORAGE},
      {CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGE,
       IDS_SETTINGS_COOKIES_LOCAL_STORAGE},

      {CookieTreeNode::DetailedInfo::TYPE_APPCACHES,
       IDS_SETTINGS_COOKIES_APPLICATION_CACHE},
      {CookieTreeNode::DetailedInfo::TYPE_APPCACHE,
       IDS_SETTINGS_COOKIES_APPLICATION_CACHE},

      {CookieTreeNode::DetailedInfo::TYPE_INDEXED_DBS,
       IDS_SETTINGS_COOKIES_DATABASE_STORAGE},
      {CookieTreeNode::DetailedInfo::TYPE_INDEXED_DB,
       IDS_SETTINGS_COOKIES_DATABASE_STORAGE},

      {CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEMS,
       IDS_SETTINGS_COOKIES_FILE_SYSTEM},
      {CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEM,
       IDS_SETTINGS_COOKIES_FILE_SYSTEM},

      {CookieTreeNode::DetailedInfo::TYPE_CHANNEL_IDS,
       IDS_SETTINGS_COOKIES_CHANNEL_ID},
      {CookieTreeNode::DetailedInfo::TYPE_CHANNEL_ID,
       IDS_SETTINGS_COOKIES_CHANNEL_ID},

      {CookieTreeNode::DetailedInfo::TYPE_SERVICE_WORKERS,
       IDS_SETTINGS_COOKIES_SERVICE_WORKER},
      {CookieTreeNode::DetailedInfo::TYPE_SERVICE_WORKER,
       IDS_SETTINGS_COOKIES_SERVICE_WORKER},

      {CookieTreeNode::DetailedInfo::TYPE_SHARED_WORKERS,
       IDS_SETTINGS_COOKIES_SHARED_WORKER},
      {CookieTreeNode::DetailedInfo::TYPE_SHARED_WORKER,
       IDS_SETTINGS_COOKIES_SHARED_WORKER},

      {CookieTreeNode::DetailedInfo::TYPE_CACHE_STORAGES,
       IDS_SETTINGS_COOKIES_CACHE_STORAGE},
      {CookieTreeNode::DetailedInfo::TYPE_CACHE_STORAGE,
       IDS_SETTINGS_COOKIES_CACHE_STORAGE},

      {CookieTreeNode::DetailedInfo::TYPE_FLASH_LSO,
       IDS_SETTINGS_COOKIES_FLASH_LSO},

      {CookieTreeNode::DetailedInfo::TYPE_MEDIA_LICENSES,
       IDS_SETTINGS_COOKIES_MEDIA_LICENSE},
      {CookieTreeNode::DetailedInfo::TYPE_MEDIA_LICENSE,
       IDS_SETTINGS_COOKIES_MEDIA_LICENSE},
  };
  // Before optimizing, consider the data size and the cost of L2 cache misses.
  // A linear search over a couple dozen integers is very fast.
  for (size_t i = 0; i < arraysize(kCategoryLabels); ++i) {
    if (kCategoryLabels[i].node_type == node_type) {
      return kCategoryLabels[i].id;
    }
  }
  NOTREACHED();
  return 0;
}

}  // namespace

namespace settings {

constexpr char kChildren[] = "children";
constexpr char kCount[] = "count";
constexpr char kId[] = "id";
constexpr char kItems[] = "items";
constexpr char kStart[] = "start";
constexpr char kLocalData[] = "localData";
constexpr char kSite[] = "site";
constexpr char kTotal[] = "total";

CookiesViewHandler::Request::Request() {
  Clear();
}

void CookiesViewHandler::Request::Clear() {
  should_send_list = false;
  callback_id_.clear();
}

CookiesViewHandler::CookiesViewHandler()
    : batch_update_(false), model_util_(new CookiesTreeModelUtil) {}

CookiesViewHandler::~CookiesViewHandler() {
}

void CookiesViewHandler::OnJavascriptAllowed() {
}

void CookiesViewHandler::OnJavascriptDisallowed() {
}

void CookiesViewHandler::RegisterMessages() {
  EnsureCookiesTreeModelCreated();

  web_ui()->RegisterMessageCallback(
      "localData.getDisplayList",
      base::BindRepeating(&CookiesViewHandler::HandleGetDisplayList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.removeAll",
      base::BindRepeating(&CookiesViewHandler::HandleRemoveAll,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.removeShownItems",
      base::BindRepeating(&CookiesViewHandler::HandleRemoveShownItems,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.removeItem",
      base::BindRepeating(&CookiesViewHandler::HandleRemoveItem,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.getCookieDetails",
      base::BindRepeating(&CookiesViewHandler::HandleGetCookieDetails,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.getNumCookiesList",
      base::BindRepeating(&CookiesViewHandler::HandleGetNumCookiesList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.getNumCookiesString",
      base::BindRepeating(&CookiesViewHandler::HandleGetNumCookiesString,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.removeCookie",
      base::BindRepeating(&CookiesViewHandler::HandleRemove,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "localData.reload",
      base::BindRepeating(&CookiesViewHandler::HandleReloadCookies,
                          base::Unretained(this)));
}

void CookiesViewHandler::TreeNodesAdded(ui::TreeModel* model,
                                        ui::TreeModelNode* parent,
                                        int start,
                                        int count) {
  // Skip if there is a batch update in progress.
  if (batch_update_)
    return;

  CookiesTreeModel* tree_model = static_cast<CookiesTreeModel*>(model);
  CookieTreeNode* parent_node = tree_model->AsNode(parent);

  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeList(
      parent_node, start, count, /*include_quota_nodes=*/false, children.get());

  base::DictionaryValue args;
  if (parent == tree_model->GetRoot())
    args.Set(kId, std::make_unique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent_node));
  args.SetInteger(kStart, start);
  args.Set(kChildren, std::move(children));
  FireWebUIListener("on-tree-item-added", args);
}

void CookiesViewHandler::TreeNodesRemoved(ui::TreeModel* model,
                                          ui::TreeModelNode* parent,
                                          int start,
                                          int count) {
  // Skip if there is a batch update in progress.
  if (batch_update_)
    return;

  CookiesTreeModel* tree_model = static_cast<CookiesTreeModel*>(model);

  base::DictionaryValue args;
  if (parent == tree_model->GetRoot())
    args.Set(kId, std::make_unique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(tree_model->AsNode(parent)));
  args.SetInteger(kStart, start);
  args.SetInteger(kCount, count);
  FireWebUIListener("on-tree-item-removed", args);
}

void CookiesViewHandler::TreeModelBeginBatch(CookiesTreeModel* model) {
  DCHECK(!batch_update_);  // There should be no nested batch begin.
  batch_update_ = true;
}

void CookiesViewHandler::TreeModelEndBatch(CookiesTreeModel* model) {
  DCHECK(batch_update_);
  batch_update_ = false;
  if (IsJavascriptAllowed()) {
    if (request_.should_send_list) {
      SendLocalDataList(model->GetRoot());
    } else if (!request_.callback_id_.empty()) {
      ResolveJavascriptCallback(base::Value(request_.callback_id_),
                                (base::Value()));
      request_.Clear();
    }
  }
}

void CookiesViewHandler::EnsureCookiesTreeModelCreated() {
  if (!cookies_tree_model_.get()) {
    Profile* profile = Profile::FromWebUI(web_ui());
    content::StoragePartition* storage_partition =
        content::BrowserContext::GetDefaultStoragePartition(profile);
    content::IndexedDBContext* indexed_db_context =
        storage_partition->GetIndexedDBContext();
    content::ServiceWorkerContext* service_worker_context =
        storage_partition->GetServiceWorkerContext();
    content::CacheStorageContext* cache_storage_context =
        storage_partition->GetCacheStorageContext();
    storage::FileSystemContext* file_system_context =
        storage_partition->GetFileSystemContext();
    auto container = std::make_unique<LocalDataContainer>(
        new BrowsingDataCookieHelper(storage_partition),
        new BrowsingDataDatabaseHelper(profile),
        new BrowsingDataLocalStorageHelper(profile),
        /*session_storage_helper=*/nullptr,
        new BrowsingDataAppCacheHelper(profile),
        new BrowsingDataIndexedDBHelper(indexed_db_context),
        BrowsingDataFileSystemHelper::Create(file_system_context),
        BrowsingDataQuotaHelper::Create(profile),
        BrowsingDataChannelIDHelper::Create(profile->GetRequestContext()),
        new BrowsingDataServiceWorkerHelper(service_worker_context),
        new BrowsingDataSharedWorkerHelper(storage_partition,
                                           profile->GetResourceContext()),
        new BrowsingDataCacheStorageHelper(cache_storage_context),
        BrowsingDataFlashLSOHelper::Create(profile),
        BrowsingDataMediaLicenseHelper::Create(file_system_context));
    cookies_tree_model_ = std::make_unique<CookiesTreeModel>(
        std::move(container), profile->GetExtensionSpecialStoragePolicy());
    cookies_tree_model_->AddCookiesTreeObserver(this);
  }
}

void CookiesViewHandler::HandleGetCookieDetails(const base::ListValue* args) {
  CHECK(request_.callback_id_.empty());
  CHECK_EQ(2U, args->GetSize());
  CHECK(args->GetString(0, &request_.callback_id_));
  std::string site;
  CHECK(args->GetString(1, &site));

  AllowJavascript();
  const CookieTreeNode* node = model_util_->GetTreeNodeFromTitle(
      cookies_tree_model_->GetRoot(), base::UTF8ToUTF16(site));

  if (!node) {
    RejectJavascriptCallback(base::Value(request_.callback_id_), base::Value());
    request_.Clear();
    return;
  }

  SendCookieDetails(node);
}

void CookiesViewHandler::HandleGetNumCookiesList(const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string callback_id;
  CHECK(args->GetString(0, &callback_id));
  const base::ListValue* etld_plus1_list;
  CHECK(args->GetList(1, &etld_plus1_list));

  AllowJavascript();
  CHECK(cookies_tree_model_.get());

  base::string16 etld_plus1;
  base::Value result(base::Value::Type::LIST);
  for (size_t i = 0; i < etld_plus1_list->GetSize(); ++i) {
    etld_plus1_list->GetString(i, &etld_plus1);
    // This method is only interested in the number of cookies, so don't save
    // |etld_plus1| as a new filter and keep the existing |sorted_sites_| list.
    cookies_tree_model_->UpdateSearchResults(etld_plus1);

    int num_cookies = 0;
    const CookieTreeNode* root = cookies_tree_model_->GetRoot();
    for (int i = 0; i < root->child_count(); ++i) {
      const CookieTreeNode* site = root->GetChild(i);
      const base::string16& title = site->GetTitle();
      if (!base::EndsWith(title, etld_plus1,
                          base::CompareCase::INSENSITIVE_ASCII)) {
        continue;
      }

      for (int j = 0; j < site->child_count(); ++j) {
        const CookieTreeNode* category = site->GetChild(j);
        if (category->GetDetailedInfo().node_type !=
            CookieTreeNode::DetailedInfo::TYPE_COOKIES) {
          continue;
        }

        for (int k = 0; k < category->child_count(); ++k) {
          if (category->GetChild(k)->GetDetailedInfo().node_type !=
              CookieTreeNode::DetailedInfo::TYPE_COOKIE) {
            continue;
          }

          ++num_cookies;
        }
      }
    }

    base::Value cookies_per_etld_plus1(base::Value::Type::DICTIONARY);
    cookies_per_etld_plus1.SetKey(kEffectiveTopLevelDomainPlus1Name,
                                  base::Value(etld_plus1));
    cookies_per_etld_plus1.SetKey(kNumCookies, base::Value(num_cookies));
    result.GetList().emplace_back(std::move(cookies_per_etld_plus1));
  }
  ResolveJavascriptCallback(base::Value(callback_id), result);

  // Restore the original |filter_|.
  cookies_tree_model_->UpdateSearchResults(filter_);
}

void CookiesViewHandler::HandleGetNumCookiesString(
    const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string callback_id;
  CHECK(args->GetString(0, &callback_id));
  int num_cookies;
  CHECK(args->GetInteger(1, &num_cookies));

  AllowJavascript();
  const base::string16 string =
      num_cookies > 0 ? l10n_util::GetPluralStringFUTF16(
                            IDS_SETTINGS_SITE_SETTINGS_NUM_COOKIES, num_cookies)
                      : base::string16();

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(string));
}

void CookiesViewHandler::HandleGetDisplayList(const base::ListValue* args) {
  CHECK(request_.callback_id_.empty());
  CHECK_EQ(2U, args->GetSize());
  CHECK(args->GetString(0, &request_.callback_id_));
  base::string16 filter;
  CHECK(args->GetString(1, &filter));

  AllowJavascript();
  request_.should_send_list = true;
  // Resetting the filter is a heavy operation, avoid unnecessary filtering.
  if (filter != filter_) {
    filter_ = filter;
    sorted_sites_.clear();
    cookies_tree_model_->UpdateSearchResults(filter_);
    return;
  }
  SendLocalDataList(cookies_tree_model_->GetRoot());
}

void CookiesViewHandler::HandleReloadCookies(const base::ListValue* args) {
  CHECK(request_.callback_id_.empty());
  CHECK_EQ(1U, args->GetSize());
  CHECK(args->GetString(0, &request_.callback_id_));

  AllowJavascript();
  cookies_tree_model_.reset();
  filter_.clear();
  sorted_sites_.clear();
  EnsureCookiesTreeModelCreated();
}

void CookiesViewHandler::HandleRemoveAll(const base::ListValue* args) {
  CHECK(request_.callback_id_.empty());
  CHECK_EQ(1U, args->GetSize());
  CHECK(args->GetString(0, &request_.callback_id_));

  AllowJavascript();
  cookies_tree_model_->DeleteAllStoredObjects();
  sorted_sites_.clear();
}

void CookiesViewHandler::HandleRemove(const base::ListValue* args) {
  std::string node_path;
  CHECK(args->GetString(0, &node_path));

  AllowJavascript();
  const CookieTreeNode* node = model_util_->GetTreeNodeFromPath(
      cookies_tree_model_->GetRoot(), node_path);
  if (node) {
    cookies_tree_model_->DeleteCookieNode(const_cast<CookieTreeNode*>(node));
    sorted_sites_.clear();
  }
}

void CookiesViewHandler::HandleRemoveShownItems(const base::ListValue* args) {
  CHECK_EQ(0U, args->GetSize());

  AllowJavascript();
  CookieTreeNode* parent = cookies_tree_model_->GetRoot();
  while (parent->child_count()) {
    cookies_tree_model_->DeleteCookieNode(parent->GetChild(0));
  }
}

void CookiesViewHandler::HandleRemoveItem(const base::ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  CHECK(request_.callback_id_.empty());
  base::string16 site;
  CHECK(args->GetString(0, &site));

  AllowJavascript();
  CookieTreeNode* parent = cookies_tree_model_->GetRoot();
  int parent_child_count = parent->child_count();
  for (int i = 0; i < parent_child_count; ++i) {
    CookieTreeNode* node = parent->GetChild(i);
    if (node->GetTitle() == site) {
      cookies_tree_model_->DeleteCookieNode(node);
      sorted_sites_.clear();
      return;
    }
  }
}

void CookiesViewHandler::SendLocalDataList(const CookieTreeNode* parent) {
  CHECK(cookies_tree_model_.get());
  CHECK(request_.should_send_list);
  const int parent_child_count = parent->child_count();
  if (sorted_sites_.empty()) {
    // Sort the list by site.
    sorted_sites_.reserve(parent_child_count);  // Optimization, hint size.
    for (int i = 0; i < parent_child_count; ++i) {
      const base::string16& title = parent->GetChild(i)->GetTitle();
      sorted_sites_.push_back(LabelAndIndex(title, i));
    }
    std::sort(sorted_sites_.begin(), sorted_sites_.end());
  }

  const int list_item_count = sorted_sites_.size();
  // The layers in the CookieTree are:
  //   root - Top level.
  //   site - www.google.com, example.com, etc.
  //   category - Cookies, Channel ID, Local Storage, etc.
  //   item - Info on the actual thing.
  // Gather list of sites with some highlights of the categories and items.
  std::unique_ptr<base::ListValue> site_list(new base::ListValue);
  for (int i = 0; i < list_item_count; ++i) {
    const CookieTreeNode* site = parent->GetChild(sorted_sites_[i].second);
    base::string16 description;
    for (int k = 0; k < site->child_count(); ++k) {
      if (description.size()) {
        description += base::ASCIIToUTF16(", ");
      }
      const CookieTreeNode* category = site->GetChild(k);
      const auto node_type = category->GetDetailedInfo().node_type;
      int item_count = category->child_count();
      switch (node_type) {
        case CookieTreeNode::DetailedInfo::TYPE_QUOTA:
          // TODO(crbug.com/642955): Omit quota values until bug is addressed.
          continue;
        case CookieTreeNode::DetailedInfo::TYPE_COOKIE:
          DCHECK_EQ(0, item_count);
          item_count = 1;
          FALLTHROUGH;
        case CookieTreeNode::DetailedInfo::TYPE_COOKIES:
          description += l10n_util::GetPluralStringFUTF16(
              IDS_SETTINGS_SITE_SETTINGS_NUM_COOKIES, item_count);
          break;
        default:
          int ids_value = GetCategoryLabelID(node_type);
          if (!ids_value) {
            // If we don't have a label to call it by, don't show it. Please add
            // a label ID if an expected category is not appearing in the UI.
            continue;
          }
          description += l10n_util::GetStringUTF16(ids_value);
          break;
      }
    }
    std::unique_ptr<base::DictionaryValue> list_info(new base::DictionaryValue);
    list_info->Set(kLocalData, std::make_unique<base::Value>(description));
    std::string title = base::UTF16ToUTF8(site->GetTitle());
    list_info->Set(kSite, std::make_unique<base::Value>(title));
    site_list->Append(std::move(list_info));
  }

  base::DictionaryValue response;
  response.Set(kItems, std::move(site_list));
  response.Set(kTotal, std::make_unique<base::Value>(list_item_count));

  ResolveJavascriptCallback(base::Value(request_.callback_id_), response);
  request_.Clear();
}

void CookiesViewHandler::SendChildren(const CookieTreeNode* parent) {
  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeList(parent, /*start=*/0, parent->child_count(),
      /*include_quota_nodes=*/false, children.get());

  base::DictionaryValue args;
  if (parent == cookies_tree_model_->GetRoot())
    args.Set(kId, std::make_unique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent));
  args.Set(kChildren, std::move(children));

  ResolveJavascriptCallback(base::Value(request_.callback_id_), args);
  request_.Clear();
}

void CookiesViewHandler::SendCookieDetails(const CookieTreeNode* parent) {
  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeDetails(parent, /*start=*/0, parent->child_count(),
                                   /*include_quota_nodes=*/false,
                                   children.get());

  base::DictionaryValue args;
  if (parent == cookies_tree_model_->GetRoot())
    args.Set(kId, std::make_unique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent));
  args.Set(kChildren, std::move(children));

  ResolveJavascriptCallback(base::Value(request_.callback_id_), args);
  request_.Clear();
}

}  // namespace settings
