// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/content/utility/payment_manifest_parser.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "components/payments/content/utility/fingerprint_parser.h"
#include "components/payments/core/error_logger.h"
#include "content/public/common/service_manager_connection.h"
#include "net/base/url_util.h"
#include "services/data_decoder/public/cpp/safe_json_parser.h"
#include "url/url_constants.h"

namespace payments {
namespace {

const size_t kMaximumNumberOfItems = 100U;

const char* const kDefaultApplications = "default_applications";
const char* const kHttpPrefix = "http://";
const char* const kHttpsPrefix = "https://";
const char* const kSupportedOrigins = "supported_origins";
const char* const kServiceWorker = "serviceworker";
const char* const kServiceWorkerSrc = "src";
const char* const kServiceWorkerScope = "scope";
const char* const kServiceWorkerUseCache = "use_cache";
const char* const kWebAppName = "name";
const char* const kWebAppIcons = "icons";
const char* const kWebAppIconSrc = "src";
const char* const kWebAppIconSizes = "sizes";
const char* const kWebAppIconType = "type";

// Parses the "default_applications": ["https://some/url"] from |dict| into
// |web_app_manifest_urls|. Returns 'false' for invalid data.
bool ParseDefaultApplications(base::DictionaryValue* dict,
                              std::vector<GURL>* web_app_manifest_urls,
                              const ErrorLogger& log) {
  DCHECK(dict);
  DCHECK(web_app_manifest_urls);

  base::ListValue* list = nullptr;
  if (!dict->GetList(kDefaultApplications, &list)) {
    log.Error(
        base::StringPrintf("\"%s\" must be a list.", kDefaultApplications));
    return false;
  }

  size_t apps_number = list->GetSize();
  if (apps_number > kMaximumNumberOfItems) {
    log.Error(base::StringPrintf("\"%s\" must contain at most %zu entries.",
                                 kDefaultApplications, kMaximumNumberOfItems));
    return false;
  }

  for (size_t i = 0; i < apps_number; ++i) {
    std::string item;
    if (!list->GetString(i, &item) || item.empty() ||
        !base::IsStringUTF8(item) ||
        !(base::StartsWith(item, kHttpsPrefix, base::CompareCase::SENSITIVE) ||
          base::StartsWith(item, kHttpPrefix, base::CompareCase::SENSITIVE))) {
      log.Error(base::StringPrintf(
          "Each entry in \"%s\" must be UTF8 string that starts with \"%s\" or "
          "\"%s\" (for localhost).",
          kDefaultApplications, kHttpsPrefix, kHttpPrefix));
      web_app_manifest_urls->clear();
      return false;
    }

    GURL url(item);
    if (!url.is_valid() ||
        !(url.SchemeIs(url::kHttpsScheme) ||
          (url.SchemeIs(url::kHttpScheme) && net::IsLocalhost(url)))) {
      log.Error(base::StringPrintf(
          "\"%s\" entry in \"%s\" is not a valid URL with HTTPS scheme and is "
          "not a valid localhost URL with HTTP scheme.",
          item.c_str(), kDefaultApplications));
      web_app_manifest_urls->clear();
      return false;
    }

    web_app_manifest_urls->push_back(url);
  }

  return true;
}

// Parses the "supported_origins": "*" (or ["https://some.origin"]) from |dict|
// into |supported_origins| and |all_origins_supported|. Returns 'false' for
// invalid data.
bool ParseSupportedOrigins(base::DictionaryValue* dict,
                           std::vector<url::Origin>* supported_origins,
                           bool* all_origins_supported,
                           const ErrorLogger& log) {
  DCHECK(dict);
  DCHECK(supported_origins);
  DCHECK(all_origins_supported);

  *all_origins_supported = false;

  {
    std::string item;
    if (dict->GetString(kSupportedOrigins, &item)) {
      if (item != "*") {
        log.Error(
            base::StringPrintf("Invalid value for \"%s\". Must be either \"*\" "
                               "or a list of RFC6454 origins.",
                               kSupportedOrigins));
        return false;
      }

      *all_origins_supported = true;
      return true;
    }
  }

  base::ListValue* list = nullptr;
  if (!dict->GetList(kSupportedOrigins, &list)) {
    log.Error(
        base::StringPrintf("\"%s\" must be either \"*\" or a list of origins.",
                           kSupportedOrigins));
    return false;
  }

  size_t supported_origins_number = list->GetSize();
  const size_t kMaximumNumberOfSupportedOrigins = 100000;
  if (supported_origins_number > kMaximumNumberOfSupportedOrigins) {
    log.Error(base::StringPrintf("\"%s\" must contain at most %zu entires.",
                                 kSupportedOrigins,
                                 kMaximumNumberOfSupportedOrigins));
    return false;
  }

  for (size_t i = 0; i < supported_origins_number; ++i) {
    std::string item;
    if (!list->GetString(i, &item) || item.empty() ||
        !base::IsStringUTF8(item) ||
        !(base::StartsWith(item, kHttpsPrefix, base::CompareCase::SENSITIVE) ||
          base::StartsWith(item, kHttpPrefix, base::CompareCase::SENSITIVE))) {
      supported_origins->clear();
      log.Error(base::StringPrintf(
          "Each entry in \"%s\" must be UTF8 string that starts with \"%s\" or "
          "\"%s\" (for localhost).",
          kSupportedOrigins, kHttpsPrefix, kHttpPrefix));
      return false;
    }

    GURL url(item);
    if (!url.is_valid() ||
        !(url.SchemeIs(url::kHttpsScheme) ||
          (url.SchemeIs(url::kHttpScheme) && net::IsLocalhost(url))) ||
        url.path() != "/" || url.has_query() || url.has_ref() ||
        url.has_username() || url.has_password()) {
      supported_origins->clear();
      log.Error(base::StringPrintf(
          "\"%s\" entry in \"%s\" is not a valid origin with HTTPS scheme and "
          "is not a valid localhost origin with HTTP scheme.",
          item.c_str(), kSupportedOrigins));
      return false;
    }

    supported_origins->push_back(url::Origin::Create(url));
  }

  return true;
}

// An object that allows both SafeJsonParser's callbacks (error/success) to run
// the same callback provided to ParsePaymentMethodManifest/ParseWebAppManifest.
// (since Callbacks are movable type, that callback has to be shared)
template <typename Callback>
class JsonParserCallback
    : public base::RefCounted<JsonParserCallback<Callback>> {
 public:
  JsonParserCallback(
      base::OnceCallback<void(Callback,
                              std::unique_ptr<base::Value>,
                              const std::string&)> parser_callback,
      Callback client_callback)
      : parser_callback_(std::move(parser_callback)),
        client_callback_(std::move(client_callback)) {}

  void OnSuccess(std::unique_ptr<base::Value> value) {
    std::move(parser_callback_)
        .Run(std::move(client_callback_), std::move(value),
             /*error_message=*/std::string());
  }

  void OnError(const std::string& error_message) {
    std::move(parser_callback_)
        .Run(std::move(client_callback_), /*value=*/nullptr, error_message);
  }

 private:
  friend class base::RefCounted<JsonParserCallback>;
  ~JsonParserCallback() = default;

  base::OnceCallback<
      void(Callback, std::unique_ptr<base::Value>, const std::string&)>
      parser_callback_;
  Callback client_callback_;
};

}  // namespace

PaymentManifestParser::WebAppIcon::WebAppIcon() = default;

PaymentManifestParser::WebAppIcon::~WebAppIcon() = default;

PaymentManifestParser::PaymentManifestParser(std::unique_ptr<ErrorLogger> log)
    : log_(std::move(log)), weak_factory_(this) {
  DCHECK(log_);
}

PaymentManifestParser::~PaymentManifestParser() = default;

void PaymentManifestParser::ParsePaymentMethodManifest(
    const std::string& content,
    PaymentMethodCallback callback) {
  parse_payment_callback_counter_++;
  DCHECK_GE(10U, parse_payment_callback_counter_);

  scoped_refptr<JsonParserCallback<PaymentMethodCallback>> json_callback =
      new JsonParserCallback<PaymentMethodCallback>(
          base::Bind(&PaymentManifestParser::OnPaymentMethodParse,
                     weak_factory_.GetWeakPtr()),
          std::move(callback));

  data_decoder::SafeJsonParser::Parse(
      content::ServiceManagerConnection::GetForProcess()->GetConnector(),
      content,
      base::Bind(&JsonParserCallback<PaymentMethodCallback>::OnSuccess,
                 json_callback),
      base::Bind(&JsonParserCallback<PaymentMethodCallback>::OnError,
                 json_callback));
}

void PaymentManifestParser::ParseWebAppManifest(const std::string& content,
                                                WebAppCallback callback) {
  parse_webapp_callback_counter_++;
  DCHECK_GE(10U, parse_webapp_callback_counter_);

  scoped_refptr<JsonParserCallback<WebAppCallback>> parser_callback =
      new JsonParserCallback<WebAppCallback>(
          base::Bind(&PaymentManifestParser::OnWebAppParse,
                     weak_factory_.GetWeakPtr()),
          std::move(callback));

  data_decoder::SafeJsonParser::Parse(
      content::ServiceManagerConnection::GetForProcess()->GetConnector(),
      content,
      base::Bind(&JsonParserCallback<WebAppCallback>::OnSuccess,
                 parser_callback),
      base::Bind(&JsonParserCallback<WebAppCallback>::OnError,
                 parser_callback));
}

void PaymentManifestParser::ParseWebAppInstallationInfo(
    const std::string& content,
    WebAppInstallationInfoCallback callback) {
  scoped_refptr<JsonParserCallback<WebAppInstallationInfoCallback>>
      sw_parser_callback =
          new JsonParserCallback<WebAppInstallationInfoCallback>(
              base::Bind(&PaymentManifestParser::OnWebAppParseInstallationInfo,
                         weak_factory_.GetWeakPtr()),
              std::move(callback));

  data_decoder::SafeJsonParser::Parse(
      content::ServiceManagerConnection::GetForProcess()->GetConnector(),
      content,
      base::Bind(&JsonParserCallback<WebAppInstallationInfoCallback>::OnSuccess,
                 sw_parser_callback),
      base::Bind(&JsonParserCallback<WebAppInstallationInfoCallback>::OnError,
                 sw_parser_callback));
}

// static
void PaymentManifestParser::ParsePaymentMethodManifestIntoVectors(
    std::unique_ptr<base::Value> value,
    const ErrorLogger& log,
    std::vector<GURL>* web_app_manifest_urls,
    std::vector<url::Origin>* supported_origins,
    bool* all_origins_supported) {
  DCHECK(web_app_manifest_urls);
  DCHECK(supported_origins);
  DCHECK(all_origins_supported);

  *all_origins_supported = false;

  std::unique_ptr<base::DictionaryValue> dict =
      base::DictionaryValue::From(std::move(value));
  if (!dict) {
    log.Error("Payment method manifest must be a JSON dictionary.");
    return;
  }

  if (dict->HasKey(kDefaultApplications) &&
      !ParseDefaultApplications(dict.get(), web_app_manifest_urls, log)) {
    return;
  }

  if (dict->HasKey(kSupportedOrigins) &&
      !ParseSupportedOrigins(dict.get(), supported_origins,
                             all_origins_supported, log)) {
    web_app_manifest_urls->clear();
  }
}

// static
bool PaymentManifestParser::ParseWebAppManifestIntoVector(
    std::unique_ptr<base::Value> value,
    const ErrorLogger& log,
    std::vector<WebAppManifestSection>* output) {
  std::unique_ptr<base::DictionaryValue> dict =
      base::DictionaryValue::From(std::move(value));
  if (!dict) {
    log.Error("Web app manifest must be a JSON dictionary.");
    return false;
  }

  base::ListValue* list = nullptr;
  if (!dict->GetList("related_applications", &list)) {
    log.Error("\"related_applications\" must be a list.");
    return false;
  }

  size_t related_applications_size = list->GetSize();
  for (size_t i = 0; i < related_applications_size; ++i) {
    base::DictionaryValue* related_application = nullptr;
    if (!list->GetDictionary(i, &related_application) || !related_application) {
      log.Error("\"related_applications\" must be a list of dictionaries.");
      output->clear();
      return false;
    }

    std::string platform;
    if (!related_application->GetString("platform", &platform) ||
        platform != "play") {
      continue;
    }

    if (output->size() >= kMaximumNumberOfItems) {
      log.Error(base::StringPrintf(
          "\"related_applications\" must contain at most %zu entries with "
          "\"platform\": \"play\".",
          kMaximumNumberOfItems));
      output->clear();
      return false;
    }

    const char* const kId = "id";
    const char* const kMinVersion = "min_version";
    const char* const kFingerprints = "fingerprints";
    if (!related_application->HasKey(kId) ||
        !related_application->HasKey(kMinVersion) ||
        !related_application->HasKey(kFingerprints)) {
      log.Error(base::StringPrintf(
          "Each \"platform\": \"play\" entry in \"related_applications\" must "
          "contain \"%s\", \"%s\", and \"%s\".",
          kId, kMinVersion, kFingerprints));
      return false;
    }

    WebAppManifestSection section;
    section.min_version = 0;

    if (!related_application->GetString(kId, &section.id) ||
        section.id.empty() || !base::IsStringASCII(section.id)) {
      log.Error(
          base::StringPrintf("\"%s\" must be a non-empty ASCII string.", kId));
      output->clear();
      return false;
    }

    std::string min_version;
    if (!related_application->GetString(kMinVersion, &min_version) ||
        min_version.empty() || !base::IsStringASCII(min_version) ||
        !base::StringToInt64(min_version, &section.min_version)) {
      log.Error(base::StringPrintf(
          "\"%s\" must be a string convertible into a number.", kMinVersion));
      output->clear();
      return false;
    }

    base::ListValue* fingerprints_list = nullptr;
    if (!related_application->GetList(kFingerprints, &fingerprints_list) ||
        fingerprints_list->empty() ||
        fingerprints_list->GetSize() > kMaximumNumberOfItems) {
      log.Error(base::StringPrintf(
          "\"%s\" must be a non-empty list of at most %zu items.",
          kFingerprints, kMaximumNumberOfItems));
      output->clear();
      return false;
    }

    size_t fingerprints_size = fingerprints_list->GetSize();
    for (size_t j = 0; j < fingerprints_size; ++j) {
      base::DictionaryValue* fingerprint_dict = nullptr;
      std::string fingerprint_type;
      std::string fingerprint_value;
      if (!fingerprints_list->GetDictionary(j, &fingerprint_dict) ||
          !fingerprint_dict ||
          !fingerprint_dict->GetString("type", &fingerprint_type) ||
          fingerprint_type != "sha256_cert" ||
          !fingerprint_dict->GetString("value", &fingerprint_value) ||
          fingerprint_value.empty() ||
          !base::IsStringASCII(fingerprint_value)) {
        log.Error(base::StringPrintf(
            "Each entry in \"%s\" must be a dictionary with \"type\": "
            "\"sha256_cert\" and a non-empty ASCII string \"value\".",
            kFingerprints));
        output->clear();
        return false;
      }

      std::vector<uint8_t> hash =
          FingerprintStringToByteArray(fingerprint_value, log);
      if (hash.empty()) {
        output->clear();
        return false;
      }

      section.fingerprints.push_back(hash);
    }

    output->push_back(section);
  }

  return true;
}

// static
bool PaymentManifestParser::ParseWebAppInstallationInfoIntoStructs(
    std::unique_ptr<base::Value> value,
    const ErrorLogger& log,
    WebAppInstallationInfo* installation_info,
    std::vector<WebAppIcon>* icons) {
  DCHECK(installation_info);
  DCHECK(icons);

  std::unique_ptr<base::DictionaryValue> dict =
      base::DictionaryValue::From(std::move(value));
  if (!dict) {
    log.Error("Web app manifest must be a JSON dictionary.");
    return false;
  }

  {
    base::DictionaryValue* service_worker_dict = nullptr;
    if (!dict->GetDictionary(kServiceWorker, &service_worker_dict)) {
      log.Error(
          base::StringPrintf("\"%s\" must be a dictionary", kServiceWorker));
      return false;
    }

    if (!service_worker_dict->GetString(kServiceWorkerSrc,
                                        &installation_info->sw_js_url) ||
        installation_info->sw_js_url.empty() ||
        !base::IsStringUTF8(installation_info->sw_js_url)) {
      log.Error(
          base::StringPrintf("\"%s\".\"%s\" must be a non-empty UTF8 string.",
                             kServiceWorker, kServiceWorkerSrc));
      return false;
    }

    service_worker_dict->GetString(kServiceWorkerScope,
                                   &installation_info->sw_scope);

    bool use_cache = false;
    if (service_worker_dict->GetBoolean(kServiceWorkerUseCache, &use_cache)) {
      installation_info->sw_use_cache = use_cache;
    }
  }

  dict->GetString(kWebAppName, &installation_info->name);
  if (installation_info->name.empty()) {
    log.Warn(
        base::StringPrintf("No \"%s\" string in the manifest.", kWebAppName));
  }

  // Extract icons.
  base::ListValue* icons_list = nullptr;
  if (!dict->GetList(kWebAppIcons, &icons_list)) {
    log.Warn(
        base::StringPrintf("No \"%s\" list in the manifest.", kWebAppIcons));
    return true;
  }

  for (const auto& icon : *icons_list) {
    if (!icon.is_dict()) {
      log.Warn(base::StringPrintf(
          "Each item in the list \"%s\" should be a dictionary.",
          kWebAppIcons));
      continue;
    }

    WebAppIcon web_app_icon;
    const base::Value* icon_src =
        icon.FindKeyOfType(kWebAppIconSrc, base::Value::Type::STRING);
    if (!icon_src || icon_src->GetString().empty() ||
        !base::IsStringUTF8(icon_src->GetString())) {
      log.Warn(base::StringPrintf(
          "Each dictionary in the list \"%s\" should contain a non-empty UTF8 "
          "string field \"%s\".",
          kWebAppIcons, kWebAppIconSrc));
      continue;
    }
    web_app_icon.src = icon_src->GetString();

    const base::Value* icon_sizes =
        icon.FindKeyOfType(kWebAppIconSizes, base::Value::Type::STRING);
    if (!icon_sizes || icon_sizes->GetString().empty() ||
        !base::IsStringUTF8(icon_sizes->GetString())) {
      log.Warn(base::StringPrintf(
          "Each dictionary in the list \"%s\" should contain a non-empty UTF8 "
          "string field \"%s\".",
          kWebAppIcons, kWebAppIconSizes));
    } else {
      web_app_icon.sizes = icon_sizes->GetString();
    }

    const base::Value* icon_type =
        icon.FindKeyOfType(kWebAppIconType, base::Value::Type::STRING);
    if (!icon_type || icon_type->GetString().empty() ||
        !base::IsStringUTF8(icon_type->GetString())) {
      log.Warn(base::StringPrintf(
          "Each dictionary in the list \"%s\" should contain a non-empty UTF8 "
          "string field \"%s\".",
          kWebAppIcons, kWebAppIconType));
    } else {
      web_app_icon.type = icon_type->GetString();
    }

    icons->emplace_back(web_app_icon);
  }

  return true;
}

void PaymentManifestParser::OnPaymentMethodParse(
    PaymentMethodCallback callback,
    std::unique_ptr<base::Value> value,
    const std::string& json_parser_error) {
  parse_payment_callback_counter_--;

  std::vector<GURL> web_app_manifest_urls;
  std::vector<url::Origin> supported_origins;
  bool all_origins_supported = false;

  if (json_parser_error.empty()) {
    ParsePaymentMethodManifestIntoVectors(
        std::move(value), *log_, &web_app_manifest_urls, &supported_origins,
        &all_origins_supported);
  } else {
    log_->Error(json_parser_error);
  }

  // Can trigger synchronous deletion of this object, so can't access any of
  // the member variables after this statement.
  std::move(callback).Run(web_app_manifest_urls, supported_origins,
                          all_origins_supported);
}

void PaymentManifestParser::OnWebAppParse(
    WebAppCallback callback,
    std::unique_ptr<base::Value> value,
    const std::string& json_parser_error) {
  parse_webapp_callback_counter_--;

  std::vector<WebAppManifestSection> manifest;
  if (json_parser_error.empty()) {
    ParseWebAppManifestIntoVector(std::move(value), *log_, &manifest);
  } else {
    log_->Error(json_parser_error);
  }

  // Can trigger synchronous deletion of this object, so can't access any of
  // the member variables after this statement.
  std::move(callback).Run(manifest);
}

void PaymentManifestParser::OnWebAppParseInstallationInfo(
    WebAppInstallationInfoCallback callback,
    std::unique_ptr<base::Value> value,
    const std::string& json_parser_error) {
  std::unique_ptr<WebAppInstallationInfo> installation_info;
  std::unique_ptr<std::vector<WebAppIcon>> icons;

  if (json_parser_error.empty()) {
    installation_info = std::make_unique<WebAppInstallationInfo>();
    icons = std::make_unique<std::vector<WebAppIcon>>();
    if (!ParseWebAppInstallationInfoIntoStructs(
            std::move(value), *log_, installation_info.get(), icons.get())) {
      installation_info.reset();
      icons.reset();
    }
  } else {
    log_->Error(json_parser_error);
  }

  // Can trigger synchronous deletion of this object, so can't access any of
  // the member variables after this statement.
  std::move(callback).Run(std::move(installation_info), std::move(icons));
}

}  // namespace payments
