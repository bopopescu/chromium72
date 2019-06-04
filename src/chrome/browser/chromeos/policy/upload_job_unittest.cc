// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/upload_job.h"

#include <stddef.h>

#include <set>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/containers/queue.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/policy/upload_job_impl.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/gaia/fake_oauth2_token_service.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/test/test_shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

namespace {

const char kUploadPath[] = "/upload";
const char kRobotAccountId[] = "robot@gmail.com";
const char kCustomField1[] = "customfield1";
const char kCustomField2[] = "customfield2";
const char kTestPayload1[] = "**||--||PAYLOAD1||--||**";
const char kTestPayload2[] = "**||--||PAYLOAD2||--||**";
const char kTokenExpired[] = "EXPIRED_TOKEN";
const char kTokenInvalid[] = "INVALID_TOKEN";
const char kTokenValid[] = "VALID_TOKEN";

class RepeatingMimeBoundaryGenerator
    : public UploadJobImpl::MimeBoundaryGenerator {
 public:
  explicit RepeatingMimeBoundaryGenerator(char character)
      : character_(character) {}
  ~RepeatingMimeBoundaryGenerator() override {}

  // MimeBoundaryGenerator:
  std::string GenerateBoundary() const override {
    const int kMimeBoundarySize = 32;
    return std::string(kMimeBoundarySize, character_);
  }

 private:
  const char character_;

  DISALLOW_COPY_AND_ASSIGN(RepeatingMimeBoundaryGenerator);
};

class MockOAuth2TokenService : public FakeOAuth2TokenService {
 public:
  MockOAuth2TokenService();
  ~MockOAuth2TokenService() override;

  // OAuth2TokenService:
  void FetchOAuth2Token(
      RequestImpl* request,
      const std::string& account_id,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      const std::string& client_id,
      const std::string& client_secret,
      const ScopeSet& scopes) override;

  // OAuth2TokenService:
  void InvalidateAccessTokenImpl(const std::string& account_id,
                                 const std::string& client_id,
                                 const ScopeSet& scopes,
                                 const std::string& access_token) override;

  void AddTokenToQueue(const std::string& token);
  bool IsTokenValid(const std::string& token) const;
  void SetTokenValid(const std::string& token);
  void SetTokenInvalid(const std::string& token);

 private:
  base::queue<std::string> token_replies_;
  std::set<std::string> valid_tokens_;

  DISALLOW_COPY_AND_ASSIGN(MockOAuth2TokenService);
};

MockOAuth2TokenService::MockOAuth2TokenService() {
}

MockOAuth2TokenService::~MockOAuth2TokenService() {
}

void MockOAuth2TokenService::FetchOAuth2Token(
    OAuth2TokenService::RequestImpl* request,
    const std::string& account_id,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& client_id,
    const std::string& client_secret,
    const FakeOAuth2TokenService::ScopeSet& scopes) {
  GoogleServiceAuthError response_error =
      GoogleServiceAuthError::AuthErrorNone();
  OAuth2AccessTokenConsumer::TokenResponse token_response;
  if (token_replies_.empty()) {
    response_error =
        GoogleServiceAuthError::FromServiceError("Service unavailable.");
  } else {
    token_response.access_token = token_replies_.front();
    token_response.expiration_time = base::Time::Now();
    token_replies_.pop();
  }
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&OAuth2TokenService::RequestImpl::InformConsumer,
                     request->AsWeakPtr(), response_error, token_response));
}

void MockOAuth2TokenService::AddTokenToQueue(const std::string& token) {
  token_replies_.push(token);
}

bool MockOAuth2TokenService::IsTokenValid(const std::string& token) const {
  return valid_tokens_.find(token) != valid_tokens_.end();
}

void MockOAuth2TokenService::SetTokenValid(const std::string& token) {
  valid_tokens_.insert(token);
}

void MockOAuth2TokenService::SetTokenInvalid(const std::string& token) {
  valid_tokens_.erase(token);
}

void MockOAuth2TokenService::InvalidateAccessTokenImpl(
    const std::string& account_id,
    const std::string& client_id,
    const ScopeSet& scopes,
    const std::string& access_token) {
  SetTokenInvalid(access_token);
}

}  // namespace

class UploadJobTestBase : public testing::Test, public UploadJob::Delegate {
 public:
  UploadJobTestBase()
      : test_browser_thread_bundle_(
            content::TestBrowserThreadBundle::IO_MAINLOOP) {}

  // policy::UploadJob::Delegate:
  void OnSuccess() override {
    if (!expected_error_)
      run_loop_.Quit();
    else
      FAIL();
  }

  // policy::UploadJob::Delegate:
  void OnFailure(UploadJob::ErrorCode error_code) override {
    if (expected_error_ && *expected_error_.get() == error_code)
      run_loop_.Quit();
    else
      FAIL();
  }

  const GURL GetServerURL() const { return test_server_.GetURL(kUploadPath); }

  void SetExpectedError(std::unique_ptr<UploadJob::ErrorCode> expected_error) {
    expected_error_ = std::move(expected_error);
  }

  // testing::Test:
  void SetUp() override {
    url_loader_factory_ =
        base::MakeRefCounted<network::TestSharedURLLoaderFactory>();
    oauth2_service_.AddAccount("robot@gmail.com");
    ASSERT_TRUE(test_server_.Start());
    // Set retry delay to prevent timeouts
    policy::UploadJobImpl::SetRetryDelayForTesting(0);
  }

  // testing::Test:
  void TearDown() override {
    ASSERT_TRUE(test_server_.ShutdownAndWaitUntilComplete());
  }

 protected:
  std::unique_ptr<UploadJob> PrepareUploadJob(
      std::unique_ptr<UploadJobImpl::MimeBoundaryGenerator>
          mime_boundary_generator) {
    std::unique_ptr<UploadJob> upload_job(new UploadJobImpl(
        GetServerURL(), kRobotAccountId, &oauth2_service_, url_loader_factory_,
        this, std::move(mime_boundary_generator), TRAFFIC_ANNOTATION_FOR_TESTS,
        base::ThreadTaskRunnerHandle::Get()));

    std::map<std::string, std::string> header_entries;
    header_entries.insert(std::make_pair(kCustomField1, "CUSTOM1"));
    std::unique_ptr<std::string> data(new std::string(kTestPayload1));
    upload_job->AddDataSegment("Name1", "file1.ext", header_entries,
                               std::move(data));

    header_entries.insert(std::make_pair(kCustomField2, "CUSTOM2"));
    std::unique_ptr<std::string> data2(new std::string(kTestPayload2));
    upload_job->AddDataSegment("Name2", "", header_entries, std::move(data2));
    return upload_job;
  }

  content::TestBrowserThreadBundle test_browser_thread_bundle_;
  base::RunLoop run_loop_;
  net::EmbeddedTestServer test_server_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  MockOAuth2TokenService oauth2_service_;

  std::unique_ptr<UploadJob::ErrorCode> expected_error_;
};

class UploadFlowTest : public UploadJobTestBase {
 public:
  UploadFlowTest() {}

  // UploadJobTestBase:
  void SetUp() override {
    test_server_.RegisterRequestHandler(
        base::Bind(&UploadFlowTest::HandlePostRequest, base::Unretained(this)));
    UploadJobTestBase::SetUp();
    upload_attempt_count_ = 0;
  }

  // Sets the response code which will be returned when no other problems occur.
  // Default is |net::HTTP_OK|
  void SetResponseDefaultStatusCode(net::HttpStatusCode code) {
    default_status_code_ = code;
  }

  std::unique_ptr<net::test_server::HttpResponse> HandlePostRequest(
      const net::test_server::HttpRequest& request) {
    upload_attempt_count_++;
    EXPECT_TRUE(request.headers.find("Authorization") != request.headers.end());
    const std::string authorization_header =
        request.headers.at("Authorization");
    std::unique_ptr<net::test_server::BasicHttpResponse> response(
        new net::test_server::BasicHttpResponse);
    const size_t pos = authorization_header.find(" ");
    if (pos == std::string::npos) {
      response->set_code(net::HTTP_UNAUTHORIZED);
      return std::move(response);
    }

    const std::string token = authorization_header.substr(pos + 1);
    response->set_code(oauth2_service_.IsTokenValid(token)
                           ? default_status_code_
                           : net::HTTP_UNAUTHORIZED);
    return std::move(response);
  }

 protected:
  int upload_attempt_count_;
  net::HttpStatusCode default_status_code_ = net::HTTP_OK;
};

TEST_F(UploadFlowTest, SuccessfulUpload) {
  oauth2_service_.SetTokenValid(kTokenValid);
  oauth2_service_.AddTokenToQueue(kTokenValid);
  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  ASSERT_EQ(1, upload_attempt_count_);
}

TEST_F(UploadFlowTest, TokenExpired) {
  oauth2_service_.SetTokenValid(kTokenValid);
  oauth2_service_.AddTokenToQueue(kTokenExpired);
  oauth2_service_.AddTokenToQueue(kTokenValid);
  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  ASSERT_EQ(2, upload_attempt_count_);
}

TEST_F(UploadFlowTest, TokenInvalid) {
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  SetExpectedError(std::unique_ptr<UploadJob::ErrorCode>(
      new UploadJob::ErrorCode(UploadJob::AUTHENTICATION_ERROR)));

  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  ASSERT_EQ(4, upload_attempt_count_);
}

TEST_F(UploadFlowTest, TokenMultipleTries) {
  oauth2_service_.SetTokenValid(kTokenValid);
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  oauth2_service_.AddTokenToQueue(kTokenInvalid);
  oauth2_service_.AddTokenToQueue(kTokenValid);

  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  ASSERT_EQ(3, upload_attempt_count_);
}

TEST_F(UploadFlowTest, TokenFetchFailure) {
  SetExpectedError(std::unique_ptr<UploadJob::ErrorCode>(
      new UploadJob::ErrorCode(UploadJob::AUTHENTICATION_ERROR)));

  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  // Without a token we don't try to upload
  ASSERT_EQ(0, upload_attempt_count_);
}

TEST_F(UploadFlowTest, InternalServerError) {
  SetResponseDefaultStatusCode(net::HTTP_INTERNAL_SERVER_ERROR);
  oauth2_service_.SetTokenValid(kTokenValid);
  oauth2_service_.AddTokenToQueue(kTokenValid);

  SetExpectedError(std::unique_ptr<UploadJob::ErrorCode>(
      new UploadJob::ErrorCode(UploadJob::SERVER_ERROR)));

  std::unique_ptr<UploadJob> upload_job = PrepareUploadJob(
      base::WrapUnique(new UploadJobImpl::RandomMimeBoundaryGenerator));
  upload_job->Start();
  run_loop_.Run();
  // kMaxAttempts
  ASSERT_EQ(4, upload_attempt_count_);
}

class UploadRequestTest : public UploadJobTestBase {
 public:
  UploadRequestTest() {}

  // UploadJobTestBase:
  void SetUp() override {
    test_server_.RegisterRequestHandler(base::Bind(
        &UploadRequestTest::HandlePostRequest, base::Unretained(this)));
    UploadJobTestBase::SetUp();
  }

  std::unique_ptr<net::test_server::HttpResponse> HandlePostRequest(
      const net::test_server::HttpRequest& request) {
    std::unique_ptr<net::test_server::BasicHttpResponse> response(
        new net::test_server::BasicHttpResponse);
    response->set_code(net::HTTP_OK);
    EXPECT_EQ(expected_content_, request.content);
    return std::move(response);
  }

  void SetExpectedRequestContent(const std::string& expected_content) {
    expected_content_ = expected_content;
  }

 protected:
  std::string expected_content_;
};

TEST_F(UploadRequestTest, TestRequestStructure) {
  oauth2_service_.SetTokenValid(kTokenValid);
  oauth2_service_.AddTokenToQueue(kTokenValid);
  std::unique_ptr<UploadJob> upload_job =
      PrepareUploadJob(std::make_unique<RepeatingMimeBoundaryGenerator>('A'));
  SetExpectedRequestContent(
      "--AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\r\n"
      "Content-Disposition: form-data; "
      "name=\"Name1\"; filename=\"file1.ext\"\r\n"
      "customfield1: CUSTOM1\r\n"
      "\r\n"
      "**||--||PAYLOAD1||--||**\r\n"
      "--AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\r\n"
      "Content-Disposition: form-data; name=\"Name2\"\r\n"
      "customfield1: CUSTOM1\r\n"
      "customfield2: CUSTOM2\r\n"
      "\r\n"
      "**||--||PAYLOAD2||--||**\r\n--"
      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA--\r\n");

  upload_job->Start();
  run_loop_.Run();
}

}  // namespace policy
