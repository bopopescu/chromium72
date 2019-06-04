// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "google_apis/gaia/gaia_auth_fetcher.h"

#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/macros.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/process/process.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/values.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "google_apis/gaia/oauth2_id_token_decoder.h"
#include "google_apis/gaia/oauth_multilogin_result.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request_status.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/resource_response.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace {

const int kLoadFlagsIgnoreCookies = net::LOAD_DO_NOT_SEND_COOKIES |
                                    net::LOAD_DO_NOT_SAVE_COOKIES;

const size_t kMaxMessageSize = 1024 * 1024;  // 1MB

std::unique_ptr<const GaiaAuthConsumer::ClientOAuthResult>
ExtractOAuth2TokenPairResponse(const std::string& data) {
  std::unique_ptr<base::Value> value = base::JSONReader::Read(data);
  if (!value.get() || value->type() != base::Value::Type::DICTIONARY)
    return nullptr;

  base::DictionaryValue* dict =
        static_cast<base::DictionaryValue*>(value.get());

  std::string refresh_token;
  std::string access_token;
  std::string id_token;
  int expires_in_secs;
  if (!dict->GetStringWithoutPathExpansion("refresh_token", &refresh_token) ||
      !dict->GetStringWithoutPathExpansion("access_token", &access_token) ||
      !dict->GetIntegerWithoutPathExpansion("expires_in", &expires_in_secs)) {
    return nullptr;
  }

  // Extract ID token when obtaining refresh token. Do not fail if absent,
  // but log to keep track.
  if (!dict->GetStringWithoutPathExpansion("id_token", &id_token)) {
    LOG(ERROR) << "Missing ID token on refresh token fetch response.";
  }
  gaia::TokenServiceFlags service_flags = gaia::ParseServiceFlags(id_token);

  return std::make_unique<const GaiaAuthConsumer::ClientOAuthResult>(
      refresh_token, access_token, expires_in_secs,
      service_flags.is_child_account,
      service_flags.is_under_advanced_protection);
}

void GetCookiesFromResponse(
    const network::HttpRawRequestResponseInfo::HeadersVector& headers,
    net::ResponseCookies* cookies) {
  for (const auto& header : headers) {
    if (header.first == "Set-Cookie" && !header.second.empty()) {
      cookies->push_back(header.second);
    }
  }
}

// Parses server responses for token revocation.
GaiaAuthConsumer::TokenRevocationStatus
GetTokenRevocationStatusFromResponseData(const std::string& data,
                                         int response_code) {
  if (response_code == net::HTTP_OK)
    return GaiaAuthConsumer::TokenRevocationStatus::kSuccess;

  if (response_code == net::HTTP_INTERNAL_SERVER_ERROR)
    return GaiaAuthConsumer::TokenRevocationStatus::kServerError;

  std::unique_ptr<base::Value> value = base::JSONReader::Read(data);
  if (!value.get() || value->type() != base::Value::Type::DICTIONARY)
    return GaiaAuthConsumer::TokenRevocationStatus::kUnknownError;

  base::DictionaryValue* dict =
      static_cast<base::DictionaryValue*>(value.get());
  std::string error;
  if (!dict->GetStringWithoutPathExpansion("error", &error))
    return GaiaAuthConsumer::TokenRevocationStatus::kUnknownError;

  if (error == "invalid_token")
    return GaiaAuthConsumer::TokenRevocationStatus::kInvalidToken;
  if (error == "invalid_request")
    return GaiaAuthConsumer::TokenRevocationStatus::kInvalidRequest;

  return GaiaAuthConsumer::TokenRevocationStatus::kUnknownError;
}

std::string GaiaSourceToString(gaia::GaiaSource source) {
  std::string source_string;
  switch (source) {
    case gaia::GaiaSource::kChrome:
      source_string = GaiaConstants::kChromeSource;
      break;
    case gaia::GaiaSource::kChromeOS:
      source_string = GaiaConstants::kChromeOSSource;
      break;
    case gaia::GaiaSource::kAccountReconcilorDice:
      source_string = "ChromiumAccountReconcilorDice";
      break;
    case gaia::GaiaSource::kAccountReconcilorMirror:
      source_string = "ChromiumAccountReconcilor";
      break;
    case gaia::GaiaSource::kOAuth2LoginVerifier:
      source_string = "ChromiumOAuth2LoginVerifier";
      break;
    case gaia::GaiaSource::kSigninManager:
      source_string = "ChromiumSigninManager";
      break;
  }

  // All sources should start with Chromium or chromeos for better server logs.
  DCHECK(source_string == "chromeos" ||
         base::StartsWith(source_string, "Chromium",
                          base::CompareCase::SENSITIVE));
  return source_string;
}

}  // namespace

// static
const char GaiaAuthFetcher::kIssueAuthTokenFormat[] =
    "SID=%s&"
    "LSID=%s&"
    "service=%s&"
    "Session=%s";
// static
const char GaiaAuthFetcher::kOAuth2CodeToTokenPairBodyFormat[] =
    "scope=%s&"
    "grant_type=authorization_code&"
    "client_id=%s&"
    "client_secret=%s&"
    "code=%s";
// static
const char GaiaAuthFetcher::kOAuth2CodeToTokenPairDeviceIdParam[] =
    "device_id=%s&device_type=chrome";
// static
const char GaiaAuthFetcher::kOAuth2RevokeTokenBodyFormat[] =
    "token=%s";
// static
const char GaiaAuthFetcher::kGetUserInfoFormat[] =
    "LSID=%s";
// static
const char GaiaAuthFetcher::kMergeSessionFormat[] =
    "?uberauth=%s&"
    "continue=%s&"
    "source=%s";
// static
const char GaiaAuthFetcher::kUberAuthTokenURLFormat[] =
    "?source=%s&"
    "issueuberauth=1";

const char GaiaAuthFetcher::kOAuthLoginFormat[] = "service=%s&source=%s";

// static
const char GaiaAuthFetcher::kAccountDeletedError[] = "AccountDeleted";
// static
const char GaiaAuthFetcher::kAccountDisabledError[] = "AccountDisabled";
// static
const char GaiaAuthFetcher::kBadAuthenticationError[] = "BadAuthentication";
// static
const char GaiaAuthFetcher::kCaptchaError[] = "CaptchaRequired";
// static
const char GaiaAuthFetcher::kServiceUnavailableError[] =
    "ServiceUnavailable";
// static
const char GaiaAuthFetcher::kErrorParam[] = "Error";
// static
const char GaiaAuthFetcher::kErrorUrlParam[] = "Url";
// static
const char GaiaAuthFetcher::kCaptchaUrlParam[] = "CaptchaUrl";
// static
const char GaiaAuthFetcher::kCaptchaTokenParam[] = "CaptchaToken";

// static
const char GaiaAuthFetcher::kSecondFactor[] = "Info=InvalidSecondFactor";
// static
const char GaiaAuthFetcher::kWebLoginRequired[] = "Info=WebLoginRequired";

// static
const char GaiaAuthFetcher::kAuthHeaderFormat[] =
    "Authorization: GoogleLogin auth=%s";
// static
const char GaiaAuthFetcher::kOAuthHeaderFormat[] = "Authorization: OAuth %s";
// static
const char GaiaAuthFetcher::kOAuthMultiBearerHeaderFormat[] =
    "Authorization: MultiBearer %s";
// static
const char GaiaAuthFetcher::kOAuth2BearerHeaderFormat[] =
    "Authorization: Bearer %s";

GaiaAuthFetcher::GaiaAuthFetcher(
    GaiaAuthConsumer* consumer,
    gaia::GaiaSource source,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(url_loader_factory),
      consumer_(consumer),
      source_(GaiaSourceToString(source)),
      oauth2_token_gurl_(GaiaUrls::GetInstance()->oauth2_token_url()),
      oauth2_revoke_gurl_(GaiaUrls::GetInstance()->oauth2_revoke_url()),
      get_user_info_gurl_(GaiaUrls::GetInstance()->get_user_info_url()),
      merge_session_gurl_(GaiaUrls::GetInstance()->merge_session_url()),
      uberauth_token_gurl_(GaiaUrls::GetInstance()->oauth1_login_url().Resolve(
          base::StringPrintf(kUberAuthTokenURLFormat, source_.c_str()))),
      oauth_login_gurl_(GaiaUrls::GetInstance()->oauth1_login_url()),
      oauth_multilogin_gurl_(GaiaUrls::GetInstance()->oauth_multilogin_url()),
      list_accounts_gurl_(
          GaiaUrls::GetInstance()->ListAccountsURLWithSource(source_)),
      logout_gurl_(GaiaUrls::GetInstance()->LogOutURLWithSource(source_)),
      get_check_connection_info_url_(
          GaiaUrls::GetInstance()->GetCheckConnectionInfoURLWithSource(
              source_)) {}

GaiaAuthFetcher::~GaiaAuthFetcher() {}

bool GaiaAuthFetcher::HasPendingFetch() {
  return fetch_pending_;
}

void GaiaAuthFetcher::SetPendingFetch(bool pending_fetch) {
  fetch_pending_ = pending_fetch;
}

void GaiaAuthFetcher::CancelRequest() {
  url_loader_.reset();
  original_url_ = GURL();
  fetch_pending_ = false;
}

bool GaiaAuthFetcher::IsMultiloginUrl(const GURL& url) {
  return base::StartsWith(url.spec(), oauth_multilogin_gurl_.spec(),
                          base::CompareCase::SENSITIVE);
}

void GaiaAuthFetcher::CreateAndStartGaiaFetcher(
    const std::string& body,
    const std::string& headers,
    const GURL& gaia_gurl,
    int load_flags,
    const net::NetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = gaia_gurl;
  original_url_ = gaia_gurl;

  if (!(load_flags & net::LOAD_DO_NOT_SEND_COOKIES)) {
    DCHECK_EQ(GaiaUrls::GetInstance()->gaia_url(), gaia_gurl.GetOrigin())
        << gaia_gurl;
    resource_request->site_for_cookies = GaiaUrls::GetInstance()->gaia_url();
  }

  if (!body.empty())
    resource_request->method = "POST";

  if (!headers.empty())
    resource_request->headers.AddHeadersFromString(headers);

  // The Gaia token exchange requests do not require any cookie-based
  // identification as part of requests.  We suppress sending any cookies to
  // maintain a separation between the user's browsing and Chrome's internal
  // services.  Where such mixing is desired (MergeSession or OAuthLogin), it
  // will be done explicitly.
  resource_request->load_flags = load_flags;

  // Use raw headers as the cookies are filtered-out of the response when
  // serialized at the IPC layer.
  resource_request->report_raw_headers = true;

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 traffic_annotation);
  if (!body.empty())
    url_loader_->AttachStringForUpload(body,
                                       "application/x-www-form-urlencoded");

  url_loader_->SetAllowHttpErrorResults(true);

  VLOG(2) << "Gaia fetcher URL: " << gaia_gurl.spec();
  VLOG(2) << "Gaia fetcher headers: " << headers;
  VLOG(2) << "Gaia fetcher body: " << body;

  // Fetchers are sometimes cancelled because a network change was detected,
  // especially at startup and after sign-in on ChromeOS. Retrying once should
  // be enough in those cases; let the fetcher retry up to 3 times just in case.
  // http://crbug.com/163710
  url_loader_->SetRetryOptions(
      3, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);

  fetch_pending_ = true;

  // Unretained is OK below as |url_loader_| is owned by this.
  url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&GaiaAuthFetcher::OnURLLoadComplete,
                     base::Unretained(this)),
      kMaxMessageSize);
}

// static
std::string GaiaAuthFetcher::MakeIssueAuthTokenBody(
    const std::string& sid,
    const std::string& lsid,
    const char* const service) {
  std::string encoded_sid = net::EscapeUrlEncodedData(sid, true);
  std::string encoded_lsid = net::EscapeUrlEncodedData(lsid, true);

  // All tokens should be session tokens except the gaia auth token.
  bool session = true;
  if (!strcmp(service, GaiaConstants::kGaiaService))
    session = false;

  return base::StringPrintf(kIssueAuthTokenFormat,
                            encoded_sid.c_str(),
                            encoded_lsid.c_str(),
                            service,
                            session ? "true" : "false");
}

// static
std::string GaiaAuthFetcher::MakeGetTokenPairBody(
    const std::string& auth_code,
    const std::string& device_id) {
  std::string encoded_scope = net::EscapeUrlEncodedData(
      GaiaConstants::kOAuth1LoginScope, true);
  std::string encoded_client_id = net::EscapeUrlEncodedData(
      GaiaUrls::GetInstance()->oauth2_chrome_client_id(), true);
  std::string encoded_client_secret = net::EscapeUrlEncodedData(
      GaiaUrls::GetInstance()->oauth2_chrome_client_secret(), true);
  std::string encoded_auth_code = net::EscapeUrlEncodedData(auth_code, true);
  std::string body = base::StringPrintf(
      kOAuth2CodeToTokenPairBodyFormat, encoded_scope.c_str(),
      encoded_client_id.c_str(), encoded_client_secret.c_str(),
      encoded_auth_code.c_str());
  if (!device_id.empty()) {
    body += "&" + base::StringPrintf(kOAuth2CodeToTokenPairDeviceIdParam,
                                     device_id.c_str());
  }
  return body;
}

// static
std::string GaiaAuthFetcher::MakeRevokeTokenBody(
    const std::string& auth_token) {
  return base::StringPrintf(kOAuth2RevokeTokenBodyFormat, auth_token.c_str());
}

// static
std::string GaiaAuthFetcher::MakeGetUserInfoBody(const std::string& lsid) {
  std::string encoded_lsid = net::EscapeUrlEncodedData(lsid, true);
  return base::StringPrintf(kGetUserInfoFormat, encoded_lsid.c_str());
}

// static
std::string GaiaAuthFetcher::MakeMergeSessionQuery(
    const std::string& auth_token,
    const std::string& external_cc_result,
    const std::string& continue_url,
    const std::string& source) {
  std::string encoded_auth_token = net::EscapeUrlEncodedData(auth_token, true);
  std::string encoded_continue_url = net::EscapeUrlEncodedData(continue_url,
                                                               true);
  std::string encoded_source = net::EscapeUrlEncodedData(source, true);
  std::string result = base::StringPrintf(kMergeSessionFormat,
                                          encoded_auth_token.c_str(),
                                          encoded_continue_url.c_str(),
                                          encoded_source.c_str());
  if (!external_cc_result.empty()) {
    base::StringAppendF(&result, "&externalCcResult=%s",
                        net::EscapeUrlEncodedData(
                            external_cc_result, true).c_str());
  }

  return result;
}

// static
std::string GaiaAuthFetcher::MakeGetAuthCodeHeader(
    const std::string& auth_token) {
  return base::StringPrintf(kAuthHeaderFormat, auth_token.c_str());
}

// Helper method that extracts tokens from a successful reply.
// static
void GaiaAuthFetcher::ParseClientLoginResponse(const std::string& data,
                                               std::string* sid,
                                               std::string* lsid,
                                               std::string* token) {
  using std::vector;
  using std::pair;
  using std::string;
  sid->clear();
  lsid->clear();
  token->clear();
  base::StringPairs tokens;
  base::SplitStringIntoKeyValuePairs(data, '=', '\n', &tokens);
  for (base::StringPairs::iterator i = tokens.begin();
      i != tokens.end(); ++i) {
    if (i->first == "SID") {
      sid->assign(i->second);
    } else if (i->first == "LSID") {
      lsid->assign(i->second);
    } else if (i->first == "Auth") {
      token->assign(i->second);
    }
  }
  // If this was a request for uberauth token, then that's all we've got in
  // data.
  if (sid->empty() && lsid->empty() && token->empty())
    token->assign(data);
}

// static
std::string GaiaAuthFetcher::MakeOAuthLoginBody(const std::string& service,
                                                const std::string& source) {
  std::string encoded_service = net::EscapeUrlEncodedData(service, true);
  std::string encoded_source = net::EscapeUrlEncodedData(source, true);
  return base::StringPrintf(kOAuthLoginFormat,
                            encoded_service.c_str(),
                            encoded_source.c_str());
}

// static
void GaiaAuthFetcher::ParseClientLoginFailure(const std::string& data,
                                              std::string* error,
                                              std::string* error_url,
                                              std::string* captcha_url,
                                              std::string* captcha_token) {
  using std::vector;
  using std::pair;
  using std::string;

  base::StringPairs tokens;
  base::SplitStringIntoKeyValuePairs(data, '=', '\n', &tokens);
  for (base::StringPairs::iterator i = tokens.begin();
       i != tokens.end(); ++i) {
    if (i->first == kErrorParam) {
      error->assign(i->second);
    } else if (i->first == kErrorUrlParam) {
      error_url->assign(i->second);
    } else if (i->first == kCaptchaUrlParam) {
      captcha_url->assign(i->second);
    } else if (i->first == kCaptchaTokenParam) {
      captcha_token->assign(i->second);
    }
  }
}

void GaiaAuthFetcher::StartRevokeOAuth2Token(const std::string& auth_token) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  VLOG(1) << "Starting OAuth2 token revocation";
  request_body_ = MakeRevokeTokenBody(auth_token);
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_revoke_token", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description: "This request revokes an OAuth 2.0 refresh token."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "an OAuth 2.0 refresh token needs to be revoked."
          data: "The OAuth 2.0 refresh token that should be revoked."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(request_body_, std::string(), oauth2_revoke_gurl_,
                            kLoadFlagsIgnoreCookies, traffic_annotation);
}

void GaiaAuthFetcher::StartAuthCodeForOAuth2TokenExchange(
    const std::string& auth_code) {
  StartAuthCodeForOAuth2TokenExchangeWithDeviceId(auth_code, std::string());
}

void GaiaAuthFetcher::StartAuthCodeForOAuth2TokenExchangeWithDeviceId(
    const std::string& auth_code,
    const std::string& device_id) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  VLOG(1) << "Starting OAuth token pair fetch";
  request_body_ = MakeGetTokenPairBody(auth_code, device_id);
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_exchange_device_id", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request exchanges an authorization code for an OAuth 2.0 "
            "refresh token."
          trigger:
            "This request is part of Gaia Auth API, and may be triggered at "
            "the end of the Chrome sign-in flow."
          data:
            "The Google console client ID and client secret of the Chrome "
            "application, the OAuth 2.0 authorization code, and the ID of the "
            "device."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(request_body_, std::string(), oauth2_token_gurl_,
                            kLoadFlagsIgnoreCookies, traffic_annotation);
}

void GaiaAuthFetcher::StartGetUserInfo(const std::string& lsid) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  VLOG(1) << "Starting GetUserInfo for lsid=" << lsid;
  request_body_ = MakeGetUserInfoBody(lsid);
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_get_user_info", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request fetches user information of a Google account."
          trigger:
            "This fetcher is only used after signing in with a child account."
          data: "The value of the Google authentication LSID cookie."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(request_body_, std::string(), get_user_info_gurl_,
                            kLoadFlagsIgnoreCookies, traffic_annotation);
}

void GaiaAuthFetcher::StartMergeSession(const std::string& uber_token,
                                        const std::string& external_cc_result) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  VLOG(1) << "Starting MergeSession with uber_token=" << uber_token;

  // The continue URL is a required parameter of the MergeSession API, but in
  // this case we don't actually need or want to navigate to it.  Setting it to
  // an arbitrary Google URL.
  //
  // In order for the new session to be merged correctly, the server needs to
  // know what sessions already exist in the browser.  The fetcher needs to be
  // created such that it sends the cookies with the request, which is
  // different from all other requests the fetcher can make.
  std::string continue_url("http://www.google.com");
  std::string query = MakeMergeSessionQuery(uber_token, external_cc_result,
                                            continue_url, source_);
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_merge_sessions", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request adds an account to the Google authentication cookies."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "a new Google account is added to the browser."
          data:
            "This request includes the user-auth token and sometimes a string "
            "containing the result of connection checks for various Google web "
            "properties."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(std::string(), std::string(),
                            merge_session_gurl_.Resolve(query),
                            net::LOAD_NORMAL, traffic_annotation);
}

void GaiaAuthFetcher::StartTokenFetchForUberAuthExchange(
    const std::string& access_token,
    bool is_bound_to_channel_id) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  VLOG(1) << "Starting StartTokenFetchForUberAuthExchange with access_token="
           << access_token;
  std::string authentication_header =
      base::StringPrintf(kOAuthHeaderFormat, access_token.c_str());
  int load_flags =
      is_bound_to_channel_id ? net::LOAD_NORMAL : kLoadFlagsIgnoreCookies;
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_fetch_for_uber", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request exchanges an Oauth2 access token for an uber-auth "
            "token. This token may be used to add an account to the Google "
            "authentication cookies."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "a new Google account is added to the browser."
          data: "This request contains an OAuth 2.0 access token. "
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(std::string(), authentication_header,
                            uberauth_token_gurl_, load_flags,
                            traffic_annotation);
}

void GaiaAuthFetcher::StartOAuthLogin(const std::string& access_token,
                                      const std::string& service) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  request_body_ = MakeOAuthLoginBody(service, source_);
  std::string authentication_header =
      base::StringPrintf(kOAuth2BearerHeaderFormat, access_token.c_str());
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_login", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request exchanges an OAuthLogin-scoped OAuth 2.0 access "
            "token for a ClientLogin-style service tokens. The response to "
            "this request is the same as the response to a ClientLogin "
            "request, except that captcha challenges are never issued."
          trigger:
            "This request is part of Gaia Auth API, and is triggered after "
            "signing in with a child account."
          data:
            "This request contains an OAuth 2.0 access token and the service "
            "for which a ClientLogin-style should be delivered."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(request_body_, authentication_header,
                            oauth_login_gurl_, net::LOAD_NORMAL,
                            traffic_annotation);
}

void GaiaAuthFetcher::StartListAccounts() {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";
  list_accounts_system_uptime_ = base::SysInfo::Uptime();
#if !defined(OS_IOS) && !defined(OS_ANDROID)
  list_accounts_process_uptime_ =
      base::Time::Now() - base::Process::Current().CreationTime();
#endif

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_list_accounts", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request is used to list the accounts in the Google "
            "authentication cookies."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "the list of all available accounts in the Google authentication "
            "cookies is required."
          data: "None."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(" ",  // To force an HTTP POST.
                            "Origin: https://www.google.com",
                            list_accounts_gurl_, net::LOAD_NORMAL,
                            traffic_annotation);
}

void GaiaAuthFetcher::StartOAuthMultilogin(
    const std::vector<MultiloginTokenIDPair>& accounts) {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  UMA_HISTOGRAM_COUNTS_100("Signin.Multilogin.NumberOfAccounts",
                           accounts.size());

  std::vector<std::string> authorization_header_parts;
  for (const MultiloginTokenIDPair& account : accounts) {
    authorization_header_parts.push_back(base::StringPrintf(
        "%s:%s", account.token_.c_str(), account.gaia_id_.c_str()));
  }

  std::string authorization_header = base::StringPrintf(
      kOAuthMultiBearerHeaderFormat,
      base::JoinString(authorization_header_parts, ",").c_str());

  std::string parameters = base::StringPrintf(
      "?source=%s", net::EscapeUrlEncodedData(source_, true).c_str());

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_multilogin", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request is used to set chrome accounts in browser in the "
            "Google authentication cookies for several google websites "
            "(e.g. youtube)."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "accounts in cookies are not consistent with accounts in browser."
          data:
            "This request includes the vector of account ids and auth-login "
            "tokens."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(" ",  // Non-empty to force a POST
                            authorization_header,
                            oauth_multilogin_gurl_.Resolve(parameters),
                            net::LOAD_NORMAL, traffic_annotation);
}

void GaiaAuthFetcher::StartLogOut() {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_log_out", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request is part of the Chrome - Google authentication API "
            "and allows its callers to sign out all Google accounts from the "
            "content area."
          trigger:
            "This request is part of Gaia Auth API, and is triggered whenever "
            "signing out of all Google accounts is required."
          data: "None."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(std::string(), std::string(), logout_gurl_,
                            net::LOAD_NORMAL, traffic_annotation);
}

void GaiaAuthFetcher::StartGetCheckConnectionInfo() {
  DCHECK(!fetch_pending_) << "Tried to fetch two things at once!";

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("gaia_auth_check_connection_info", R"(
        semantics {
          sender: "Chrome - Google authentication API"
          description:
            "This request is used to fetch from the Google authentication "
            "server the the list of URLs to check its connection info."
          trigger:
            "This request is part of Gaia Auth API, and is triggered once "
            "after a Google account is added to the browser."
          data: "None."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled in settings, but if the user "
            "signs out of Chrome, this request would not be made."
          chrome_policy {
            SigninAllowed {
              policy_options {mode: MANDATORY}
              SigninAllowed: false
            }
          }
        })");
  CreateAndStartGaiaFetcher(std::string(), std::string(),
                            get_check_connection_info_url_,
                            kLoadFlagsIgnoreCookies, traffic_annotation);
}

// static
GoogleServiceAuthError GaiaAuthFetcher::GenerateAuthError(
    const std::string& data,
    net::Error net_error) {
  if (net_error != net::OK) {
    if (net_error == net::ERR_ABORTED) {
      return GoogleServiceAuthError(GoogleServiceAuthError::REQUEST_CANCELED);
    }
    DLOG(WARNING) << "Could not reach Google Accounts servers: errno "
                  << net_error;
    return GoogleServiceAuthError::FromConnectionError(net_error);
  }

  if (IsSecondFactorSuccess(data))
    return GoogleServiceAuthError(GoogleServiceAuthError::TWO_FACTOR);

  if (IsWebLoginRequiredSuccess(data))
    return GoogleServiceAuthError(GoogleServiceAuthError::WEB_LOGIN_REQUIRED);

  std::string error;
  std::string url;
  std::string captcha_url;
  std::string captcha_token;
  ParseClientLoginFailure(data, &error, &url, &captcha_url, &captcha_token);
  DLOG(WARNING) << "ClientLogin failed with " << error;

  if (error == kCaptchaError) {
    return GoogleServiceAuthError::FromClientLoginCaptchaChallenge(
        captcha_token,
        GURL(GaiaUrls::GetInstance()->captcha_base_url().Resolve(captcha_url)),
        GURL(url));
  }
  if (error == kAccountDeletedError)
    return GoogleServiceAuthError(GoogleServiceAuthError::ACCOUNT_DELETED);
  if (error == kAccountDisabledError)
    return GoogleServiceAuthError(GoogleServiceAuthError::ACCOUNT_DISABLED);
  if (error == kBadAuthenticationError) {
    return GoogleServiceAuthError(
        GoogleServiceAuthError::FromInvalidGaiaCredentialsReason(
            GoogleServiceAuthError::InvalidGaiaCredentialsReason::
                CREDENTIALS_REJECTED_BY_SERVER));
  }
  if (error == kServiceUnavailableError) {
    return GoogleServiceAuthError(
        GoogleServiceAuthError::SERVICE_UNAVAILABLE);
  }

  DLOG(WARNING) << "Incomprehensible response from Google Accounts servers.";
  return GoogleServiceAuthError(GoogleServiceAuthError::SERVICE_UNAVAILABLE);
}

void GaiaAuthFetcher::OnOAuth2TokenPairFetched(const std::string& data,
                                               net::Error net_error,
                                               int response_code) {
  std::unique_ptr<const GaiaAuthConsumer::ClientOAuthResult> result;
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    result = ExtractOAuth2TokenPairResponse(data);
  }

  if (result) {
    consumer_->OnClientOAuthSuccess(*result);
  } else {
    consumer_->OnClientOAuthFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnOAuth2RevokeTokenFetched(const std::string& data,
                                                 net::Error net_error,
                                                 int response_code) {
  GaiaAuthConsumer::TokenRevocationStatus revocation_status =
      GaiaAuthConsumer::TokenRevocationStatus::kUnknownError;

  switch (net_error) {
    case net::OK:
      revocation_status =
          GetTokenRevocationStatusFromResponseData(data, response_code);
      break;
    case net::ERR_IO_PENDING:
      NOTREACHED();
      break;
    case net::ERR_ABORTED:
      revocation_status =
          GaiaAuthConsumer::TokenRevocationStatus::kConnectionCanceled;
      break;
    default:
      revocation_status =
          (net_error == net::ERR_TIMED_OUT)
              ? GaiaAuthConsumer::TokenRevocationStatus::kConnectionTimeout
              : GaiaAuthConsumer::TokenRevocationStatus::kConnectionFailed;
      break;
  }

  consumer_->OnOAuth2RevokeTokenCompleted(revocation_status);
}

void GaiaAuthFetcher::OnListAccountsFetched(const std::string& data,
                                            net::Error net_error,
                                            int response_code) {
  // Log error rates and details for ListAccounts, for investigation of
  // https://crbug.com/876306.
  base::UmaHistogramSparse("Gaia.AuthFetcher.ListAccounts.NetErrorCodes",
                           -net_error);
  if (net_error == net::OK) {
    UMA_HISTOGRAM_LONG_TIMES(
        "Gaia.AuthFetcher.ListAccounts.SystemUptime.Success",
        list_accounts_system_uptime_);
#if !defined(OS_IOS) && !defined(OS_ANDROID)
    UMA_HISTOGRAM_LONG_TIMES(
        "Gaia.AuthFetcher.ListAccounts.ProcessUptime.Success",
        list_accounts_process_uptime_);
#endif
  } else {
    UMA_HISTOGRAM_LONG_TIMES("Gaia.AuthFetcher.ListAccounts.SystemUptime.Error",
                             list_accounts_system_uptime_);
#if !defined(OS_IOS) && !defined(OS_ANDROID)
    UMA_HISTOGRAM_LONG_TIMES(
        "Gaia.AuthFetcher.ListAccounts.ProcessUptime.Error",
        list_accounts_process_uptime_);
#endif
  }

  if (net_error == net::OK && response_code == net::HTTP_OK)
    consumer_->OnListAccountsSuccess(data);
  else
    consumer_->OnListAccountsFailure(GenerateAuthError(data, net_error));
}

void GaiaAuthFetcher::OnLogOutFetched(const std::string& data,
                                      net::Error net_error,
                                      int response_code) {
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    consumer_->OnLogOutSuccess();
  } else {
    consumer_->OnLogOutFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnGetUserInfoFetched(const std::string& data,
                                           net::Error net_error,
                                           int response_code) {
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    base::StringPairs tokens;
    UserInfoMap matches;
    base::SplitStringIntoKeyValuePairs(data, '=', '\n', &tokens);
    base::StringPairs::iterator i;
    for (i = tokens.begin(); i != tokens.end(); ++i) {
      matches[i->first] = i->second;
    }
    consumer_->OnGetUserInfoSuccess(matches);
  } else {
    consumer_->OnGetUserInfoFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnMergeSessionFetched(const std::string& data,
                                            net::Error net_error,
                                            int response_code) {
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    consumer_->OnMergeSessionSuccess(data);
  } else {
    consumer_->OnMergeSessionFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnUberAuthTokenFetch(const std::string& data,
                                           net::Error net_error,
                                           int response_code) {
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    consumer_->OnUberAuthTokenSuccess(data);
  } else {
    consumer_->OnUberAuthTokenFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnOAuthLoginFetched(const std::string& data,
                                          net::Error net_error,
                                          int response_code) {
  if (net_error == net::OK && response_code == net::HTTP_OK) {
    VLOG(1) << "ClientLogin successful!";
    std::string sid;
    std::string lsid;
    std::string token;
    ParseClientLoginResponse(data, &sid, &lsid, &token);
    consumer_->OnClientLoginSuccess(
        GaiaAuthConsumer::ClientLoginResult(sid, lsid, token, data));
  } else {
    consumer_->OnClientLoginFailure(GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnGetCheckConnectionInfoFetched(const std::string& data,
                                                      net::Error net_error,
                                                      int response_code) {
  if (net_error == net::Error::OK && response_code == net::HTTP_OK) {
    consumer_->OnGetCheckConnectionInfoSuccess(data);
  } else {
    consumer_->OnGetCheckConnectionInfoError(
        GenerateAuthError(data, net_error));
  }
}

void GaiaAuthFetcher::OnOAuthMultiloginFetched(const std::string& data,
                                               net::Error net_error,
                                               int response_code) {
  GoogleServiceAuthError auth_error = GoogleServiceAuthError::AuthErrorNone();
  OAuthMultiloginResult result =
      (net_error == net::Error::OK)
          ? OAuthMultiloginResult(data)
          : OAuthMultiloginResult(GenerateAuthError(data, net_error));
  consumer_->OnOAuthMultiloginFinished(result);
}

void GaiaAuthFetcher::OnURLLoadComplete(
    std::unique_ptr<std::string> response_body) {
  net::Error net_error = static_cast<net::Error>(url_loader_->NetError());
  std::string data = response_body ? std::move(*response_body) : "";

  int response_code = 0;
  network::HttpRawRequestResponseInfo::HeadersVector headers;
  if (url_loader_->ResponseInfo()) {
    if (url_loader_->ResponseInfo()->headers)
      response_code = url_loader_->ResponseInfo()->headers->response_code();
    if (url_loader_->ResponseInfo()->raw_request_response_info) {
      headers = url_loader_->ResponseInfo()
                    ->raw_request_response_info->response_headers;
    }
  }
  OnURLLoadCompleteInternal(net_error, response_code, headers, data);
}

void GaiaAuthFetcher::OnURLLoadCompleteInternal(
    net::Error net_error,
    int response_code,
    const network::HttpRawRequestResponseInfo::HeadersVector& headers,
    std::string data) {
  fetch_pending_ = false;
  net::ResponseCookies cookies;
  GetCookiesFromResponse(headers, &cookies);

  // Some of the GAIA requests perform redirects, which results in the final URL
  // of the fetcher not being the original URL requested.  Therefore use the
  // original URL when determining which OnXXX function to call.
  GURL url = original_url_;
  original_url_ = GURL();
  DispatchFetchedRequest(url, data, cookies, net_error, response_code);
}

void GaiaAuthFetcher::DispatchFetchedRequest(
    const GURL& url,
    const std::string& data,
    const net::ResponseCookies& cookies,
    net::Error net_error,
    int response_code) {
  if (url == oauth2_token_gurl_) {
    OnOAuth2TokenPairFetched(data, net_error, response_code);
  } else if (url == get_user_info_gurl_) {
    OnGetUserInfoFetched(data, net_error, response_code);
  } else if (base::StartsWith(url.spec(), merge_session_gurl_.spec(),
                              base::CompareCase::SENSITIVE)) {
    OnMergeSessionFetched(data, net_error, response_code);
  } else if (url == uberauth_token_gurl_) {
    OnUberAuthTokenFetch(data, net_error, response_code);
  } else if (url == oauth_login_gurl_) {
    OnOAuthLoginFetched(data, net_error, response_code);
  } else if (IsMultiloginUrl(url)) {
    OnOAuthMultiloginFetched(data, net_error, response_code);
  } else if (url == oauth2_revoke_gurl_) {
    OnOAuth2RevokeTokenFetched(data, net_error, response_code);
  } else if (url == list_accounts_gurl_) {
    OnListAccountsFetched(data, net_error, response_code);
  } else if (url == logout_gurl_) {
    OnLogOutFetched(data, net_error, response_code);
  } else if (url == get_check_connection_info_url_) {
    OnGetCheckConnectionInfoFetched(data, net_error, response_code);
  } else {
    NOTREACHED() << "Unknown url: '" << url << "'";
  }
}

// static
bool GaiaAuthFetcher::IsSecondFactorSuccess(
    const std::string& alleged_error) {
  return alleged_error.find(kSecondFactor) !=
      std::string::npos;
}

// static
bool GaiaAuthFetcher::IsWebLoginRequiredSuccess(
    const std::string& alleged_error) {
  return alleged_error.find(kWebLoginRequired) !=
      std::string::npos;
}
