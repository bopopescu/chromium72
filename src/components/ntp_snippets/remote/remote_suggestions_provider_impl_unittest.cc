// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/remote/remote_suggestions_provider_impl.h"

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/default_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/image_fetcher/core/image_decoder.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/mock_image_fetcher.h"
#include "components/image_fetcher/core/request_metadata.h"
#include "components/ntp_snippets/breaking_news/breaking_news_listener.h"
#include "components/ntp_snippets/category.h"
#include "components/ntp_snippets/category_info.h"
#include "components/ntp_snippets/category_rankers/category_ranker.h"
#include "components/ntp_snippets/category_rankers/constant_category_ranker.h"
#include "components/ntp_snippets/category_rankers/mock_category_ranker.h"
#include "components/ntp_snippets/fake_content_suggestions_provider_observer.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/logger.h"
#include "components/ntp_snippets/ntp_snippets_constants.h"
#include "components/ntp_snippets/pref_names.h"
#include "components/ntp_snippets/remote/json_to_categories.h"
#include "components/ntp_snippets/remote/persistent_scheduler.h"
#include "components/ntp_snippets/remote/proto/ntp_snippets.pb.h"
#include "components/ntp_snippets/remote/remote_suggestion.h"
#include "components/ntp_snippets/remote/remote_suggestion_builder.h"
#include "components/ntp_snippets/remote/remote_suggestions_database.h"
#include "components/ntp_snippets/remote/remote_suggestions_fetcher_impl.h"
#include "components/ntp_snippets/remote/remote_suggestions_scheduler.h"
#include "components/ntp_snippets/remote/remote_suggestions_status_service.h"
#include "components/ntp_snippets/remote/remote_suggestions_status_service_impl.h"
#include "components/ntp_snippets/remote/test_utils.h"
#include "components/ntp_snippets/time_serialization.h"
#include "components/ntp_snippets/user_classifier.h"
#include "components/prefs/testing_pref_service.h"
#include "components/variations/variations_params_manager.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gmock_mutant.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_unittest_util.h"

using base::TestMockTimeTaskRunner;
using image_fetcher::ImageFetcher;
using image_fetcher::MockImageFetcher;
using ntp_snippets::test::FetchedCategoryBuilder;
using ntp_snippets::test::RemoteSuggestionBuilder;
using testing::_;
using testing::AnyNumber;
using testing::AtMost;
using testing::Contains;
using testing::CreateFunctor;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Eq;
using testing::Field;
using testing::InSequence;
using testing::Invoke;
using testing::IsEmpty;
using testing::Matcher;
using testing::Mock;
using testing::MockFunction;
using testing::NiceMock;
using testing::Not;
using testing::Property;
using testing::Return;
using testing::SaveArg;
using testing::SizeIs;
using testing::StartsWith;
using testing::StrictMock;
using testing::WithArgs;

namespace ntp_snippets {

namespace {

ACTION_P(MoveFirstArgumentPointeeTo, ptr) {
  // 0-based indexation.
  *ptr = std::move(*arg0);
}

ACTION_P(MoveSecondArgumentPointeeTo, ptr) {
  // 0-based indexation.
  *ptr = std::move(*arg1);
}

const int kMaxExcludedDismissedIds = 100;

const base::Time::Exploded kDefaultCreationTime = {2015, 11, 4, 25, 13, 46, 45};

const char kSuggestionUrl[] = "http://localhost/foobar";
const char kSuggestionTitle[] = "Title";
const char kSuggestionText[] = "Suggestion";
const char kSuggestionPublisherName[] = "Foo News";
const char kImageUrl[] = "http://image/image.png";

const char kSuggestionUrl2[] = "http://foo.com/bar";

const char kTestJsonDefaultCategoryTitle[] = "Some title";

const int kUnknownRemoteCategoryId = 1234;

const int kTimeoutForRefetchWhileDisplayingSeconds = 5;

// Different from default values to confirm that variation param values are
// used.
const int kMaxAdditionalPrefetchedSuggestions = 7;
const base::TimeDelta kMaxAgeForAdditionalPrefetchedSuggestion =
    base::TimeDelta::FromHours(48);

base::Time GetDefaultCreationTime() {
  base::Time out_time;
  EXPECT_TRUE(base::Time::FromUTCExploded(kDefaultCreationTime, &out_time));
  return out_time;
}

base::Time GetDefaultExpirationTime() {
  return base::Time::Now() + base::TimeDelta::FromHours(1);
}

// TODO(vitaliii): Remove this and use RemoteSuggestionBuilder instead.
std::unique_ptr<RemoteSuggestion> CreateTestRemoteSuggestion(
    const std::string& url) {
  SnippetProto snippet_proto;
  snippet_proto.add_ids(url);
  snippet_proto.set_title("title");
  snippet_proto.set_snippet("snippet");
  snippet_proto.set_salient_image_url(url + "p.jpg");
  snippet_proto.set_publish_date(SerializeTime(GetDefaultCreationTime()));
  snippet_proto.set_expiry_date(SerializeTime(GetDefaultExpirationTime()));
  snippet_proto.set_remote_category_id(1);
  auto* source = snippet_proto.mutable_source();
  source->set_url(url);
  source->set_publisher_name("Publisher");
  source->set_amp_url(url + "amp");
  return RemoteSuggestion::CreateFromProto(snippet_proto);
}

void ServeOneByOneImage(
    const std::string& id,
    image_fetcher::ImageDataFetcherCallback* image_data_callback,
    image_fetcher::ImageFetcherCallback* callback) {
  std::move(*image_data_callback)
      .Run("1-by-1-image-data", image_fetcher::RequestMetadata());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(*callback), id, gfx::test::CreateImage(1, 1),
                     image_fetcher::RequestMetadata()));
}

gfx::Image FetchImage(RemoteSuggestionsProviderImpl* provider,
                      const ContentSuggestion::ID& suggestion_id) {
  gfx::Image result;
  base::RunLoop run_loop;
  provider->FetchSuggestionImage(
      suggestion_id, base::BindOnce(
                         [](base::Closure signal, gfx::Image* output,
                            const gfx::Image& loaded) {
                           *output = loaded;
                           signal.Run();
                         },
                         run_loop.QuitClosure(), &result));
  run_loop.Run();
  return result;
}

class FakeImageDecoder : public image_fetcher::ImageDecoder {
 public:
  FakeImageDecoder() {}
  ~FakeImageDecoder() override = default;
  void DecodeImage(
      const std::string& image_data,
      const gfx::Size& desired_image_frame_size,
      const image_fetcher::ImageDecodedCallback& callback) override {
    callback.Run(decoded_image_);
  }

  void SetDecodedImage(const gfx::Image& image) { decoded_image_ = image; }

 private:
  gfx::Image decoded_image_;
};

class MockScheduler : public RemoteSuggestionsScheduler {
 public:
  MOCK_METHOD1(SetProvider, void(RemoteSuggestionsProvider* provider));
  MOCK_METHOD0(OnProviderActivated, void());
  MOCK_METHOD0(OnProviderDeactivated, void());
  MOCK_METHOD0(OnSuggestionsCleared, void());
  MOCK_METHOD0(OnHistoryCleared, void());
  MOCK_METHOD0(AcquireQuotaForInteractiveFetch, bool());
  MOCK_METHOD1(OnInteractiveFetchFinished, void(Status fetch_status));
  MOCK_METHOD0(OnBrowserForegrounded, void());
  MOCK_METHOD0(OnBrowserColdStart, void());
  MOCK_METHOD0(OnSuggestionsSurfaceOpened, void());
  MOCK_METHOD0(OnPersistentSchedulerWakeUp, void());
  MOCK_METHOD0(OnBrowserUpgraded, void());
};

class MockRemoteSuggestionsFetcher : public RemoteSuggestionsFetcher {
 public:
  // GMock does not support movable-only types (SnippetsAvailableCallback is
  // OnceCallback), therefore, the call is redirected to a mock method with a
  // pointer to the callback.
  void FetchSnippets(const RequestParams& params,
                     SnippetsAvailableCallback callback) override {
    FetchSnippets(params, &callback);
  }
  MOCK_METHOD2(FetchSnippets,
               void(const RequestParams& params,
                    SnippetsAvailableCallback* callback));
  MOCK_CONST_METHOD0(GetLastStatusForDebugging, const std::string&());
  MOCK_CONST_METHOD0(GetLastJsonForDebugging, const std::string&());
  MOCK_CONST_METHOD0(WasLastFetchAuthenticatedForDebugging, bool());
  MOCK_CONST_METHOD0(GetFetchUrlForDebugging, const GURL&());
};

class MockPrefetchedPagesTracker : public PrefetchedPagesTracker {
 public:
  MOCK_CONST_METHOD0(IsInitialized, bool());

  // GMock does not support movable-only types (e.g. OnceCallback), therefore,
  // the call is redirected to a mock method with a pointer to the callback.
  void Initialize(base::OnceCallback<void()> callback) override {
    Initialize(&callback);
  }
  MOCK_METHOD1(Initialize, void(base::OnceCallback<void()>* callback));
  MOCK_CONST_METHOD1(PrefetchedOfflinePageExists, bool(const GURL& url));
};

class FakeBreakingNewsListener : public BreakingNewsListener {
 public:
  ~FakeBreakingNewsListener() override = default;

  // BreakingNewsListener implementation.
  void StartListening(
      OnNewRemoteSuggestionCallback on_new_remote_suggestion_callback,
      OnRefreshRequestedCallback on_refresh_requested_callback) override {
    on_new_remote_suggestion_callback_ = on_new_remote_suggestion_callback;
    on_refresh_requested_callback_ = on_refresh_requested_callback;
  }

  void StopListening() override {
    on_new_remote_suggestion_callback_ = OnNewRemoteSuggestionCallback();
    on_refresh_requested_callback_ = OnRefreshRequestedCallback();
  }

  bool IsListening() const override {
    return !on_new_remote_suggestion_callback_.is_null();
  }

  void PushSuggestion(std::unique_ptr<RemoteSuggestion> suggestion) {
    EXPECT_TRUE(IsListening());
    on_new_remote_suggestion_callback_.Run(std::move(suggestion));
  }

 private:
  OnNewRemoteSuggestionCallback on_new_remote_suggestion_callback_;
  OnRefreshRequestedCallback on_refresh_requested_callback_;
};

class MockRemoteSuggestionsStatusService
    : public RemoteSuggestionsStatusService {
 public:
  ~MockRemoteSuggestionsStatusService() override = default;

  MOCK_METHOD1(Init, void(const StatusChangeCallback& callback));
  MOCK_METHOD1(OnSignInStateChanged, void(bool));
  MOCK_METHOD1(OnListVisibilityToggled, void(bool));
};

std::string BoolToString(bool value) {
  return value ? "true" : "false";
}

base::Time GetDummyNow() {
  base::Time out_time;
  EXPECT_TRUE(base::Time::FromUTCString("2017-01-02T00:00:01Z", &out_time));
  return out_time;
}

}  // namespace

class RemoteSuggestionsProviderImplTest : public ::testing::Test {
 public:
  RemoteSuggestionsProviderImplTest()
      : category_ranker_(std::make_unique<ConstantCategoryRanker>()),
        user_classifier_(/*pref_service=*/nullptr,
                         base::DefaultClock::GetInstance()),
        mock_suggestions_fetcher_(nullptr),
        image_fetcher_(nullptr),
        scheduler_(std::make_unique<NiceMock<MockScheduler>>()),
        database_(nullptr),
        timer_mock_task_runner_(
            (new TestMockTimeTaskRunner(GetDummyNow(),
                                        base::TimeTicks::Now()))) {
    RemoteSuggestionsProviderImpl::RegisterProfilePrefs(
        utils_.pref_service()->registry());
    RequestThrottler::RegisterProfilePrefs(utils_.pref_service()->registry());

    EXPECT_TRUE(database_dir_.CreateUniqueTempDir());
  }

  ~RemoteSuggestionsProviderImplTest() override {
    // We need to run until idle after deleting the database, because
    // ProtoDatabaseImpl deletes the actual LevelDB asynchronously on the task
    // runner. Without this, we'd get reports of memory leaks.
    RunUntilIdle();
  }

  // TODO(vitaliii): Rewrite this function to initialize a test class member
  // instead of creating a new provider.
  std::unique_ptr<RemoteSuggestionsProviderImpl> MakeSuggestionsProvider(
      bool use_mock_prefetched_pages_tracker,
      bool use_fake_breaking_news_listener,
      bool use_mock_remote_suggestions_status_service) {
    auto provider = MakeSuggestionsProviderWithoutInitialization(
        use_mock_prefetched_pages_tracker, use_fake_breaking_news_listener,
        use_mock_remote_suggestions_status_service);
    WaitForSuggestionsProviderInitialization(provider.get());
    return provider;
  }

  std::unique_ptr<RemoteSuggestionsProviderImpl>
  MakeSuggestionsProviderWithoutInitialization(
      bool use_mock_prefetched_pages_tracker,
      bool use_fake_breaking_news_listener,
      bool use_mock_remote_suggestions_status_service) {
    auto mock_suggestions_fetcher =
        std::make_unique<StrictMock<MockRemoteSuggestionsFetcher>>();
    mock_suggestions_fetcher_ = mock_suggestions_fetcher.get();

    std::unique_ptr<StrictMock<MockPrefetchedPagesTracker>>
        mock_prefetched_pages_tracker;
    if (use_mock_prefetched_pages_tracker) {
      mock_prefetched_pages_tracker =
          std::make_unique<StrictMock<MockPrefetchedPagesTracker>>();
    }
    mock_prefetched_pages_tracker_ = mock_prefetched_pages_tracker.get();

    std::unique_ptr<FakeBreakingNewsListener> fake_breaking_news_listener;
    if (use_fake_breaking_news_listener) {
      fake_breaking_news_listener =
          std::make_unique<FakeBreakingNewsListener>();
    }
    fake_breaking_news_listener_ = fake_breaking_news_listener.get();

    std::unique_ptr<RemoteSuggestionsStatusService>
        remote_suggestions_status_service;
    if (use_mock_remote_suggestions_status_service) {
      auto mock_remote_suggestions_status_service =
          std::make_unique<StrictMock<MockRemoteSuggestionsStatusService>>();
      EXPECT_CALL(*mock_remote_suggestions_status_service, Init(_))
          .WillOnce(SaveArg<0>(&status_change_callback_));
      remote_suggestions_status_service =
          std::move(mock_remote_suggestions_status_service);
    } else {
      remote_suggestions_status_service =
          std::make_unique<RemoteSuggestionsStatusServiceImpl>(
              /*has_signed_in=*/false, utils_.pref_service(), std::string());
    }
    remote_suggestions_status_service_ =
        remote_suggestions_status_service.get();

    auto image_fetcher = std::make_unique<NiceMock<MockImageFetcher>>();

    image_fetcher_ = image_fetcher.get();
    ON_CALL(*image_fetcher, GetImageDecoder())
        .WillByDefault(Return(&image_decoder_));
    EXPECT_FALSE(observer_);
    observer_ = std::make_unique<FakeContentSuggestionsProviderObserver>();
    auto database =
        std::make_unique<RemoteSuggestionsDatabase>(database_dir_.GetPath());
    database_ = database.get();

    auto fetch_timeout_timer = std::make_unique<base::OneShotTimer>(
        timer_mock_task_runner_->GetMockTickClock());
    fetch_timeout_timer->SetTaskRunner(timer_mock_task_runner_);

    return std::make_unique<RemoteSuggestionsProviderImpl>(
        observer_.get(), utils_.pref_service(), "fr", category_ranker_.get(),
        scheduler_.get(), std::move(mock_suggestions_fetcher),
        std::move(image_fetcher), std::move(database),
        std::move(remote_suggestions_status_service),
        std::move(mock_prefetched_pages_tracker),
        std::move(fake_breaking_news_listener), &debug_logger_,
        std::move(fetch_timeout_timer));
  }

  std::unique_ptr<RemoteSuggestionsProviderImpl>
  MakeSuggestionsProviderWithoutInitializationWithStrictScheduler() {
    scheduler_ = std::make_unique<StrictMock<MockScheduler>>();
    return MakeSuggestionsProviderWithoutInitialization(
        /*use_mock_prefetched_pages_tracker=*/false,
        /*use_fake_breaking_news_listener=*/false,
        /*use_mock_remote_suggestions_status_service=*/false);
  }

  void WaitForSuggestionsProviderInitialization(
      RemoteSuggestionsProviderImpl* provider) {
    EXPECT_EQ(RemoteSuggestionsProviderImpl::State::NOT_INITED,
              provider->state_);

    // TODO(treib): Find a better way to wait for initialization to finish.
    RunUntilIdle();
  }

  void ResetSuggestionsProvider(
      std::unique_ptr<RemoteSuggestionsProviderImpl>* provider,
      bool use_mock_prefetched_pages_tracker,
      bool use_fake_breaking_news_listener,
      bool use_mock_remote_suggestions_status_service) {
    provider->reset();
    // We need to run until idle after deleting the RemoteSuggestionsDatabase.
    RunUntilIdle();
    observer_.reset();
    *provider = MakeSuggestionsProvider(
        use_mock_prefetched_pages_tracker, use_fake_breaking_news_listener,
        use_mock_remote_suggestions_status_service);
  }

  void ResetSuggestionsProviderWithoutInitialization(
      std::unique_ptr<RemoteSuggestionsProviderImpl>* provider,
      bool use_mock_prefetched_pages_tracker,
      bool use_fake_breaking_news_listener,
      bool use_mock_remote_suggestions_status_service) {
    provider->reset();
    // We need to run until idle after deleting the RemoteSuggestionsDatabase.
    RunUntilIdle();
    observer_.reset();
    *provider = MakeSuggestionsProviderWithoutInitialization(
        use_mock_prefetched_pages_tracker, use_fake_breaking_news_listener,
        use_mock_remote_suggestions_status_service);
  }

  void RunUntilIdle() {
    timer_mock_task_runner_->RunUntilIdle();
    scoped_task_environment_.RunUntilIdle();
  }

  void SetCategoryRanker(std::unique_ptr<CategoryRanker> category_ranker) {
    category_ranker_ = std::move(category_ranker);
  }

  ContentSuggestion::ID MakeArticleID(const std::string& id_within_category) {
    return ContentSuggestion::ID(articles_category(), id_within_category);
  }

  Category articles_category() {
    return Category::FromKnownCategory(KnownCategories::ARTICLES);
  }

  ContentSuggestion::ID MakeOtherID(const std::string& id_within_category) {
    return ContentSuggestion::ID(other_category(), id_within_category);
  }

  // TODO(tschumann): Get rid of the convenience other_category() and
  // unknown_category() helpers -- tests can just define their own.
  Category other_category() { return Category::FromRemoteCategory(2); }

  Category unknown_category() {
    return Category::FromRemoteCategory(kUnknownRemoteCategoryId);
  }

 protected:
  FakeContentSuggestionsProviderObserver& observer() { return *observer_; }
  StrictMock<MockRemoteSuggestionsFetcher>* mock_suggestions_fetcher() {
    return mock_suggestions_fetcher_;
  }
  StrictMock<MockPrefetchedPagesTracker>* mock_prefetched_pages_tracker() {
    return mock_prefetched_pages_tracker_;
  }
  // TODO(tschumann): Make this a strict-mock. We want to avoid unneccesary
  // network requests.
  NiceMock<MockImageFetcher>* image_fetcher() { return image_fetcher_; }
  FakeImageDecoder* image_decoder() { return &image_decoder_; }
  PrefService* pref_service() { return utils_.pref_service(); }
  RemoteSuggestionsDatabase* database() { return database_; }
  MockScheduler* scheduler() { return scheduler_.get(); }
  FakeBreakingNewsListener* fake_breaking_news_listener() {
    return fake_breaking_news_listener_;
  }

  void FetchTheseSuggestions(
      RemoteSuggestionsProviderImpl* provider,
      bool interactive_request,
      Status status,
      base::Optional<std::vector<FetchedCategory>> fetched_categories) {
    RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
    EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
        .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
        .RetiresOnSaturation();
    provider->FetchSuggestions(
        interactive_request, RemoteSuggestionsProvider::FetchStatusCallback());
    std::move(snippets_callback).Run(status, std::move(fetched_categories));
  }

  void FetchMoreTheseSuggestions(
      RemoteSuggestionsProviderImpl* provider,
      const Category& category,
      const std::set<std::string>& known_suggestion_ids,
      FetchDoneCallback fetch_done_callback,
      Status status,
      base::Optional<std::vector<FetchedCategory>> fetched_categories) {
    RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
    EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
        .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
        .RetiresOnSaturation();
    EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
        .WillOnce(Return(true))
        .RetiresOnSaturation();
    provider->Fetch(category, known_suggestion_ids,
                    std::move(fetch_done_callback));
    std::move(snippets_callback).Run(status, std::move(fetched_categories));
  }

  RemoteSuggestionsFetcher::SnippetsAvailableCallback
  FetchSuggestionsAndGetResponseCallback(
      RemoteSuggestionsProviderImpl* provider,
      bool interactive_request) {
    RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
    EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
        .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
        .RetiresOnSaturation();
    provider->FetchSuggestions(
        interactive_request, RemoteSuggestionsProvider::FetchStatusCallback());
    return snippets_callback;
  }

  RemoteSuggestionsFetcher::SnippetsAvailableCallback
  RefetchWhileDisplayingAndGetResponseCallback(
      RemoteSuggestionsProviderImpl* provider) {
    RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
    EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
        .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
        .RetiresOnSaturation();
    provider->RefetchWhileDisplaying(
        RemoteSuggestionsProvider::FetchStatusCallback());
    return snippets_callback;
  }

  RemoteSuggestionsFetcher::SnippetsAvailableCallback
  ReloadSuggestionsAndGetResponseCallback(
      RemoteSuggestionsProviderImpl* provider) {
    EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
        .WillOnce(Return(true))
        .RetiresOnSaturation();
    RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
    EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
        .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
        .RetiresOnSaturation();
    provider->ReloadSuggestions();
    return snippets_callback;
  }

  void PushArticleSuggestionToTheFront(
      std::unique_ptr<RemoteSuggestion> suggestion) {
    ASSERT_TRUE(fake_breaking_news_listener_);
    fake_breaking_news_listener_->PushSuggestion(std::move(suggestion));
  }

  void ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus old_status,
                                     RemoteSuggestionsStatus new_status) {
    EXPECT_FALSE(status_change_callback_.is_null());
    status_change_callback_.Run(old_status, new_status);
  }

  void SetOrderNewRemoteCategoriesBasedOnArticlesCategoryParam(bool value) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        kArticleSuggestionsFeature.name,
        {{"order_new_remote_categories_based_on_articles_category",
          value ? "true" : "false"}},
        {kArticleSuggestionsFeature.name});
  }

  void EnableKeepingPrefetchedContentSuggestions(
      int max_additional_prefetched_suggestions,
      const base::TimeDelta& max_age_for_additional_prefetched_suggestion) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        kKeepPrefetchedContentSuggestions.name,
        {
            {"max_additional_prefetched_suggestions",
             base::IntToString(max_additional_prefetched_suggestions)},
            {"max_age_for_additional_prefetched_suggestion_minutes",
             base::IntToString(
                 max_age_for_additional_prefetched_suggestion.InMinutes())},
        },
        {kKeepPrefetchedContentSuggestions.name});
  }

  void SetTriggeringNotificationsAndSubscriptionParams(
      bool fetched_notifications_enabled,
      bool pushed_notifications_enabled,
      bool subscribe_signed_in,
      bool subscribe_signed_out) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        /*trial_name=*/kNotificationsFeature.name,
        {
            {"enable_fetched_suggestions_notifications",
             BoolToString(fetched_notifications_enabled)},
            {"enable_pushed_suggestions_notifications",
             BoolToString(pushed_notifications_enabled)},
            {"enable_signed_in_users_subscription_for_pushed_suggestions",
             BoolToString(subscribe_signed_in)},
            {"enable_signed_out_users_subscription_for_pushed_suggestions",
             BoolToString(subscribe_signed_out)},
        },
        {kNotificationsFeature.name, kBreakingNewsPushFeature.name});
  }

  void SetFetchedNotificationsParams(bool enable, bool force) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        /*trial_name=*/kNotificationsFeature.name,
        {
            {"enable_fetched_suggestions_notifications", BoolToString(enable)},
            {"force_fetched_suggestions_notifications", BoolToString(force)},
        },
        {kNotificationsFeature.name});
  }

  void SetFetchMoreSuggestionsCount(int count) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        /*trial_name=*/kArticleSuggestionsFeature.name,
        {{"fetch_more_suggestions_count", base::IntToString(count)}},
        {kArticleSuggestionsFeature.name});
  }

  void FastForwardBy(const base::TimeDelta& delta) {
    timer_mock_task_runner_->FastForwardBy(delta);
  }

 private:
  variations::testing::VariationParamsManager params_manager_;
  test::RemoteSuggestionsTestUtils utils_;
  std::unique_ptr<CategoryRanker> category_ranker_;
  UserClassifier user_classifier_;
  std::unique_ptr<FakeContentSuggestionsProviderObserver> observer_;
  StrictMock<MockRemoteSuggestionsFetcher>* mock_suggestions_fetcher_;
  StrictMock<MockPrefetchedPagesTracker>* mock_prefetched_pages_tracker_;
  NiceMock<MockImageFetcher>* image_fetcher_;
  FakeImageDecoder image_decoder_;
  std::unique_ptr<MockScheduler> scheduler_;
  FakeBreakingNewsListener* fake_breaking_news_listener_;
  RemoteSuggestionsStatusService* remote_suggestions_status_service_;
  base::test::ScopedTaskEnvironment scoped_task_environment_;

  RemoteSuggestionsStatusService::StatusChangeCallback status_change_callback_;

  base::ScopedTempDir database_dir_;
  RemoteSuggestionsDatabase* database_;

  Logger debug_logger_;
  scoped_refptr<TestMockTimeTaskRunner> timer_mock_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(RemoteSuggestionsProviderImplTest);
};

TEST_F(RemoteSuggestionsProviderImplTest, Full) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId(kSuggestionUrl)
                                       .SetTitle(kSuggestionTitle)
                                       .SetSnippet(kSuggestionText)
                                       .SetImageUrl(kImageUrl)
                                       .SetPublishDate(GetDefaultCreationTime())
                                       .SetPublisher(kSuggestionPublisherName))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));

  const ContentSuggestion& suggestion =
      observer().SuggestionsForCategory(articles_category()).front();

  EXPECT_EQ(MakeArticleID(kSuggestionUrl), suggestion.id());
  EXPECT_EQ(kSuggestionTitle, base::UTF16ToUTF8(suggestion.title()));
  EXPECT_EQ(kSuggestionText, base::UTF16ToUTF8(suggestion.snippet_text()));
  EXPECT_EQ(kImageUrl, suggestion.salient_image_url());
  EXPECT_EQ(GetDefaultCreationTime(), suggestion.publish_date());
  EXPECT_EQ(kSuggestionPublisherName,
            base::UTF16ToUTF8(suggestion.publisher_name()));
}

TEST_F(RemoteSuggestionsProviderImplTest, CategoryTitle) {
  const base::string16 test_default_title =
      base::UTF8ToUTF16(kTestJsonDefaultCategoryTitle);

  // Don't send an initial response -- we want to test what happens without any
  // server status.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // The articles category should be there by default, and have a title.
  CategoryInfo info_before = provider->GetCategoryInfo(articles_category());
  ASSERT_THAT(info_before.title(), Not(IsEmpty()));
  ASSERT_THAT(info_before.title(), Not(Eq(test_default_title)));
  EXPECT_THAT(info_before.additional_action(),
              Eq(ContentSuggestionsAdditionalAction::FETCH));
  EXPECT_THAT(info_before.show_if_empty(), Eq(true));

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .SetTitle(base::UTF16ToUTF8(test_default_title))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder())
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));

  // The response contained a title, |kTestJsonDefaultCategoryTitle|.
  // Make sure we updated the title in the CategoryInfo.
  CategoryInfo info_with_title = provider->GetCategoryInfo(articles_category());
  EXPECT_THAT(info_before.title(), Not(Eq(info_with_title.title())));
  EXPECT_THAT(test_default_title, Eq(info_with_title.title()));
  EXPECT_THAT(info_before.additional_action(),
              Eq(ContentSuggestionsAdditionalAction::FETCH));
  EXPECT_THAT(info_before.show_if_empty(), Eq(true));
}

TEST_F(RemoteSuggestionsProviderImplTest, MultipleCategories) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId(base::StringPrintf("%s/%d", kSuggestionUrl, 0))
                  .SetTitle(kSuggestionTitle)
                  .SetSnippet(kSuggestionText)
                  .SetPublishDate(GetDefaultCreationTime())
                  .SetPublisher(kSuggestionPublisherName))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(2))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId(base::StringPrintf("%s/%d", kSuggestionUrl, 1))
                  .SetTitle(kSuggestionTitle)
                  .SetSnippet(kSuggestionText)
                  .SetPublishDate(GetDefaultCreationTime())
                  .SetPublisher(kSuggestionPublisherName))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().statuses(),
              Eq(std::map<Category, CategoryStatus, Category::CompareByID>{
                  {articles_category(), CategoryStatus::AVAILABLE},
                  {other_category(), CategoryStatus::AVAILABLE},
              }));

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
  EXPECT_THAT(provider->GetSuggestionsForTesting(other_category()), SizeIs(1));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  ASSERT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(1));

  {
    const ContentSuggestion& suggestion =
        observer().SuggestionsForCategory(articles_category()).front();
    EXPECT_EQ(MakeArticleID(std::string(kSuggestionUrl) + "/0"),
              suggestion.id());
    EXPECT_EQ(kSuggestionTitle, base::UTF16ToUTF8(suggestion.title()));
    EXPECT_EQ(kSuggestionText, base::UTF16ToUTF8(suggestion.snippet_text()));
    EXPECT_EQ(GetDefaultCreationTime(), suggestion.publish_date());
    EXPECT_EQ(kSuggestionPublisherName,
              base::UTF16ToUTF8(suggestion.publisher_name()));
  }

  {
    const ContentSuggestion& suggestion =
        observer().SuggestionsForCategory(other_category()).front();
    EXPECT_EQ(MakeOtherID(std::string(kSuggestionUrl) + "/1"), suggestion.id());
    EXPECT_EQ(kSuggestionTitle, base::UTF16ToUTF8(suggestion.title()));
    EXPECT_EQ(kSuggestionText, base::UTF16ToUTF8(suggestion.snippet_text()));
    EXPECT_EQ(GetDefaultCreationTime(), suggestion.publish_date());
    EXPECT_EQ(kSuggestionPublisherName,
              base::UTF16ToUTF8(suggestion.publisher_name()));
  }
}

TEST_F(RemoteSuggestionsProviderImplTest, ArticleCategoryInfo) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  CategoryInfo article_info = provider->GetCategoryInfo(articles_category());
  EXPECT_THAT(article_info.additional_action(),
              Eq(ContentSuggestionsAdditionalAction::FETCH));
  EXPECT_THAT(article_info.show_if_empty(), Eq(true));
}

TEST_F(RemoteSuggestionsProviderImplTest, ExperimentalCategoryInfo) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("1"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(kUnknownRemoteCategoryId))
          .SetAdditionalAction(ContentSuggestionsAdditionalAction::NONE)
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("2"))
          .Build());
  // Load data with multiple categories so that a new experimental category gets
  // registered.
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  CategoryInfo info = provider->GetCategoryInfo(unknown_category());
  EXPECT_THAT(info.additional_action(),
              Eq(ContentSuggestionsAdditionalAction::NONE));
  EXPECT_THAT(info.show_if_empty(), Eq(false));
}

TEST_F(RemoteSuggestionsProviderImplTest, AddRemoteCategoriesToCategoryRanker) {
  auto mock_ranker = std::make_unique<MockCategoryRanker>();
  MockCategoryRanker* raw_mock_ranker = mock_ranker.get();
  SetCategoryRanker(std::move(mock_ranker));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(11))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("11"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(13))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("13"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(12))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("12"))
          .Build());
  {
    // The order of categories is determined by the order in which they are
    // added. Thus, the latter is tested here.
    InSequence s;
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(11)));
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(13)));
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(12)));
  }
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       AddRemoteCategoriesToCategoryRankerRelativeToArticles) {
  SetOrderNewRemoteCategoriesBasedOnArticlesCategoryParam(true);
  auto mock_ranker = std::make_unique<MockCategoryRanker>();
  MockCategoryRanker* raw_mock_ranker = mock_ranker.get();
  SetCategoryRanker(std::move(mock_ranker));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(14))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("14"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(13))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("13"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("1"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(12))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("12"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(11))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("11"))
          .Build());
  {
    InSequence s;
    EXPECT_CALL(*raw_mock_ranker,
                InsertCategoryBeforeIfNecessary(
                    Category::FromRemoteCategory(14), articles_category()));
    EXPECT_CALL(*raw_mock_ranker,
                InsertCategoryBeforeIfNecessary(
                    Category::FromRemoteCategory(13), articles_category()));
    EXPECT_CALL(*raw_mock_ranker,
                InsertCategoryAfterIfNecessary(Category::FromRemoteCategory(11),
                                               articles_category()));
    EXPECT_CALL(*raw_mock_ranker,
                InsertCategoryAfterIfNecessary(Category::FromRemoteCategory(12),
                                               articles_category()));
  }
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
}

TEST_F(
    RemoteSuggestionsProviderImplTest,
    AddRemoteCategoriesToCategoryRankerRelativeToArticlesWithArticlesAbsent) {
  SetOrderNewRemoteCategoriesBasedOnArticlesCategoryParam(true);
  auto mock_ranker = std::make_unique<MockCategoryRanker>();
  MockCategoryRanker* raw_mock_ranker = mock_ranker.get();
  SetCategoryRanker(std::move(mock_ranker));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(11))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("11"))
          .Build());

  EXPECT_CALL(*raw_mock_ranker, InsertCategoryBeforeIfNecessary(_, _)).Times(0);
  EXPECT_CALL(*raw_mock_ranker,
              AppendCategoryIfNecessary(Category::FromRemoteCategory(11)));
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
}

TEST_F(RemoteSuggestionsProviderImplTest, PersistCategoryInfos) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("1"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(kUnknownRemoteCategoryId))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("2"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_EQ(observer().StatusForCategory(articles_category()),
            CategoryStatus::AVAILABLE);
  ASSERT_EQ(observer().StatusForCategory(
                Category::FromRemoteCategory(kUnknownRemoteCategoryId)),
            CategoryStatus::AVAILABLE);

  CategoryInfo info_articles_before =
      provider->GetCategoryInfo(articles_category());
  CategoryInfo info_unknown_before = provider->GetCategoryInfo(
      Category::FromRemoteCategory(kUnknownRemoteCategoryId));

  // Recreate the provider to simulate a Chrome restart.
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // The categories should have been restored.
  ASSERT_NE(observer().StatusForCategory(articles_category()),
            CategoryStatus::NOT_PROVIDED);
  ASSERT_NE(observer().StatusForCategory(
                Category::FromRemoteCategory(kUnknownRemoteCategoryId)),
            CategoryStatus::NOT_PROVIDED);

  EXPECT_EQ(observer().StatusForCategory(articles_category()),
            CategoryStatus::AVAILABLE);
  EXPECT_EQ(observer().StatusForCategory(
                Category::FromRemoteCategory(kUnknownRemoteCategoryId)),
            CategoryStatus::AVAILABLE);

  CategoryInfo info_articles_after =
      provider->GetCategoryInfo(articles_category());
  CategoryInfo info_unknown_after = provider->GetCategoryInfo(
      Category::FromRemoteCategory(kUnknownRemoteCategoryId));

  EXPECT_EQ(info_articles_before.title(), info_articles_after.title());
  EXPECT_EQ(info_unknown_before.title(), info_unknown_after.title());
}

TEST_F(RemoteSuggestionsProviderImplTest, PersistRemoteCategoryOrder) {
  // We create a provider with a normal ranker to store the order.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(11))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("11"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(13))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("13"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(12))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("12"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // We manually recreate the provider to simulate Chrome restart and enforce a
  // mock ranker.
  auto mock_ranker = std::make_unique<MockCategoryRanker>();
  MockCategoryRanker* raw_mock_ranker = mock_ranker.get();
  SetCategoryRanker(std::move(mock_ranker));
  // Ensure that the order is not fetched.
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _)).Times(0);
  {
    // The order of categories is determined by the order in which they are
    // added. Thus, the latter is tested here.
    InSequence s;
    // Article category always exists and, therefore, it is stored in prefs too.
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(articles_category()));

    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(11)));
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(13)));
    EXPECT_CALL(*raw_mock_ranker,
                AppendCategoryIfNecessary(Category::FromRemoteCategory(12)));
  }
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
}

TEST_F(RemoteSuggestionsProviderImplTest, PersistSuggestions) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("1").SetRemoteCategoryId(1))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(2))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("2").SetRemoteCategoryId(2))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  ASSERT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(1));

  // Recreate the provider to simulate a Chrome restart.
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // The suggestions in both categories should have been restored.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  EXPECT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest, ClearSuggestionsOnInit) {
  // Add suggestions.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("1").SetRemoteCategoryId(1))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(2))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("2").SetRemoteCategoryId(2))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  ASSERT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(1));

  // Reset the provider and clear the suggestions before it is inited.
  ResetSuggestionsProviderWithoutInitialization(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  provider->ClearCachedSuggestions();

  // The suggestions in both categories should have been cleared after the init.
  WaitForSuggestionsProviderInitialization(provider.get());
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(0));
  EXPECT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(0));
}

TEST_F(RemoteSuggestionsProviderImplTest, DontNotifyIfNotAvailable) {
  // Get some suggestions into the database.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(1))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("1"))
          .Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(Category::FromRemoteCategory(2))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("2"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  ASSERT_THAT(observer().SuggestionsForCategory(other_category()), SizeIs(1));

  provider.reset();

  // Set the pref that disables remote suggestions.
  pref_service()->SetBoolean(prefs::kEnableSnippets, false);

  // Recreate the provider to simulate a Chrome start.
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  ASSERT_THAT(RemoteSuggestionsProviderImpl::State::DISABLED,
              Eq(provider->state_));

  // Now the observer should not have received any suggestions.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              IsEmpty());
  EXPECT_THAT(observer().SuggestionsForCategory(other_category()), IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, Clear) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("1"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));

  provider->ClearCachedSuggestions();
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, ReplaceSuggestions) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::string first("http://first");
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(first))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::id, first))));

  std::string second("http://second");
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(second))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // The suggestions loaded last replace all that was loaded previously.
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::id, second))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldResolveFetchedSuggestionThumbnail) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("id"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::id, "id"))));

  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  auto serve_one_by_one_image_callback =
      base::BindRepeating(&ServeOneByOneImage);
  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .WillOnce(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(serve_one_by_one_image_callback))));

  gfx::Image image = FetchImage(provider.get(), MakeArticleID("id"));
  ASSERT_FALSE(image.IsEmpty());
  EXPECT_EQ(1, image.Width());
}

TEST_F(RemoteSuggestionsProviderImplTest, ShouldFetchMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("first"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::id, "first"))));

  auto expect_only_second_suggestion_received =
      base::Bind([](Status status, std::vector<ContentSuggestion> suggestions) {
        EXPECT_THAT(suggestions, SizeIs(1));
        EXPECT_THAT(suggestions[0].id().id_within_category(), Eq("second"));
      });
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("second"))
          .Build());
  FetchMoreTheseSuggestions(
      provider.get(), articles_category(),
      /*known_suggestion_ids=*/std::set<std::string>(),
      /*fetch_done_callback=*/expect_only_second_suggestion_received,
      Status::Success(), std::move(fetched_categories));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldResolveFetchedMoreSuggestionThumbnail) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId("id"))
          .Build());

  auto assert_only_first_suggestion_received =
      base::Bind([](Status status, std::vector<ContentSuggestion> suggestions) {
        ASSERT_THAT(suggestions, SizeIs(1));
        ASSERT_THAT(suggestions[0].id().id_within_category(), Eq("id"));
      });
  FetchMoreTheseSuggestions(
      provider.get(), articles_category(),
      /*known_suggestion_ids=*/std::set<std::string>(),
      /*fetch_done_callback=*/assert_only_first_suggestion_received,
      Status::Success(), std::move(fetched_categories));

  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  auto serve_one_by_one_image_callback =
      base::BindRepeating(&ServeOneByOneImage);
  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .WillOnce(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(serve_one_by_one_image_callback))));

  gfx::Image image = FetchImage(provider.get(), MakeArticleID("id"));
  ASSERT_FALSE(image.IsEmpty());
  EXPECT_EQ(1, image.Width());
}

// Imagine that we have surfaces A and B. The user fetches more in A, this
// should not add any suggestions to B.
TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotChangeSuggestionsInOtherSurfacesWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Fetch a suggestion.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://old.com/"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::id,
                                   MakeArticleID("http://old.com/"))));

  // Now fetch more, but first prepare a response.
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://fetched-more.com/"))
          .Build());

  // The surface issuing the fetch more gets response via callback.
  auto assert_receiving_one_new_suggestion =
      base::Bind([](Status status, std::vector<ContentSuggestion> suggestions) {
        ASSERT_THAT(suggestions, SizeIs(1));
        ASSERT_THAT(suggestions[0].id().id_within_category(),
                    Eq("http://fetched-more.com/"));
      });
  FetchMoreTheseSuggestions(
      provider.get(), articles_category(),
      /*known_suggestion_ids=*/{"http://old.com/"},
      /*fetch_done_callback=*/assert_receiving_one_new_suggestion,
      Status::Success(), std::move(fetched_categories));

  // Other surfaces should remain the same.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::id,
                                   MakeArticleID("http://old.com/"))));
}

// Imagine that we have surfaces A and B. The user fetches more in A. This
// should not affect the next fetch more in B, i.e. assuming the same server
// response the same suggestions must be fetched in B if the user fetches more
// there as well.
TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotAffectFetchMoreInOtherSurfacesWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Fetch more on the surface A.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(FetchedCategory(
      articles_category(),
      BuildRemoteCategoryInfo(base::UTF8ToUTF16("title"),
                              /*allow_fetching_more_results=*/true)));
  fetched_categories[0].suggestions.push_back(
      CreateTestRemoteSuggestion("http://fetched-more.com/"));

  auto assert_receiving_one_new_suggestion =
      base::Bind([](Status status, std::vector<ContentSuggestion> suggestions) {
        ASSERT_THAT(suggestions, SizeIs(1));
        ASSERT_THAT(suggestions[0].id().id_within_category(),
                    Eq("http://fetched-more.com/"));
      });
  RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
      .RetiresOnSaturation();
  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  provider->Fetch(articles_category(),
                  /*known_suggestion_ids=*/std::set<std::string>(),
                  assert_receiving_one_new_suggestion);
  std::move(snippets_callback)
      .Run(Status::Success(), std::move(fetched_categories));

  // Now fetch more on the surface B. The response is the same as before.
  fetched_categories.clear();
  fetched_categories.push_back(FetchedCategory(
      articles_category(),
      BuildRemoteCategoryInfo(base::UTF8ToUTF16("title"),
                              /*allow_fetching_more_results=*/true)));
  fetched_categories[0].suggestions.push_back(
      CreateTestRemoteSuggestion("http://fetched-more.com/"));

  // B should receive the same suggestion as was fetched more on A.
  auto expect_receiving_same_suggestion =
      base::Bind([](Status status, std::vector<ContentSuggestion> suggestions) {
        ASSERT_THAT(suggestions, SizeIs(1));
        EXPECT_THAT(suggestions[0].id().id_within_category(),
                    Eq("http://fetched-more.com/"));
      });
  // The provider should not ask the fetcher to exclude the suggestion fetched
  // more on A.
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids,
                                  Not(Contains("http://fetched-more.com/"))),
                            _))
      .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
      .RetiresOnSaturation();
  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  provider->Fetch(articles_category(),
                  /*known_suggestion_ids=*/std::set<std::string>(),
                  expect_receiving_same_suggestion);
  std::move(snippets_callback)
      .Run(Status::Success(), std::move(fetched_categories));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ClearHistoryShouldDeleteArchivedSuggestions) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  // First get suggestions into the archived state which happens through
  // subsequent fetches. Then we verify the entries are gone from the 'archived'
  // state by trying to load their images (and we shouldn't even know the URLs
  // anymore).
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://id-1"))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://id-2"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://new-id-1"))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://new-id-2"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // Make sure images of both batches are available. This is to sanity check our
  // assumptions for the test are right.
  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .Times(2)
      .WillRepeatedly(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(base::BindRepeating(&ServeOneByOneImage)))));
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  gfx::Image image = FetchImage(provider.get(), MakeArticleID("http://id-1"));
  ASSERT_FALSE(image.IsEmpty());
  ASSERT_EQ(1, image.Width());
  image = FetchImage(provider.get(), MakeArticleID("http://new-id-1"));
  ASSERT_FALSE(image.IsEmpty());
  ASSERT_EQ(1, image.Width());

  provider->ClearHistory(base::Time::UnixEpoch(), base::Time::Max(),
                         base::Callback<bool(const GURL& url)>());

  // Make sure images of both batches are gone.
  // Verify we cannot resolve the image of the new suggestions.
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  EXPECT_TRUE(
      FetchImage(provider.get(), MakeArticleID("http://id-1")).IsEmpty());
  EXPECT_TRUE(
      FetchImage(provider.get(), MakeArticleID("http://new-id-1")).IsEmpty());
}

namespace {

// Workaround for gMock's lack of support for movable types.
void SuggestionsLoaded(
    MockFunction<void(Status, const std::vector<ContentSuggestion>&)>* loaded,
    Status status,
    std::vector<ContentSuggestion> suggestions) {
  loaded->Call(status, suggestions);
}

}  // namespace

TEST_F(RemoteSuggestionsProviderImplTest, ReturnFetchRequestEmptyBeforeInit) {
  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _)).Times(0);
  MockFunction<void(Status, const std::vector<ContentSuggestion>&)> loaded;
  EXPECT_CALL(loaded, Call(Field(&Status::code, StatusCode::TEMPORARY_ERROR),
                           IsEmpty()));
  provider->Fetch(articles_category(), std::set<std::string>(),
                  base::BindOnce(&SuggestionsLoaded, &loaded));
  RunUntilIdle();
}

TEST_F(RemoteSuggestionsProviderImplTest, ReturnRefetchRequestEmptyBeforeInit) {
  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _)).Times(0);
  MockFunction<void(Status)> loaded;
  EXPECT_CALL(loaded, Call(Field(&Status::code, StatusCode::TEMPORARY_ERROR)));
  provider->RefetchInTheBackground(base::BindOnce(
      &MockFunction<void(Status)>::Call, base::Unretained(&loaded)));
  RunUntilIdle();
}

TEST_F(RemoteSuggestionsProviderImplTest, IgnoreRefetchRequestEmptyBeforeInit) {
  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _)).Times(0);
  provider->RefetchInTheBackground(
      RemoteSuggestionsProvider::FetchStatusCallback());
  RunUntilIdle();
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldForwardTemporaryErrorFromFetcher) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
  MockFunction<void(Status, const std::vector<ContentSuggestion>&)> loaded;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback));
  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  provider->Fetch(articles_category(),
                  /*known_ids=*/std::set<std::string>(),
                  base::BindOnce(&SuggestionsLoaded, &loaded));

  EXPECT_CALL(loaded, Call(Field(&Status::code, StatusCode::TEMPORARY_ERROR),
                           IsEmpty()));
  ASSERT_FALSE(snippets_callback.is_null());
  std::move(snippets_callback)
      .Run(Status(StatusCode::TEMPORARY_ERROR, "Received invalid JSON"),
           base::nullopt);
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotAddNewSuggestionsAfterFetchError) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  FetchTheseSuggestions(
      provider.get(), /*interactive_request=*/false,
      Status(StatusCode::TEMPORARY_ERROR, "Received invalid JSON"),
      base::nullopt);
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotClearOldSuggestionsAfterFetchError) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(FetchedCategory(
      articles_category(),
      BuildRemoteCategoryInfo(base::UTF8ToUTF16("title"),
                              /*allow_fetching_more_results=*/true)));
  fetched_categories[0].suggestions.push_back(
      CreateTestRemoteSuggestion(base::StringPrintf("http://abc.com/")));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/false,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(
      provider->GetSuggestionsForTesting(articles_category()),
      ElementsAre(Pointee(Property(&RemoteSuggestion::id, "http://abc.com/"))));

  FetchTheseSuggestions(
      provider.get(), /*interactive_request=*/false,
      Status(StatusCode::TEMPORARY_ERROR, "Received invalid JSON"),
      base::nullopt);
  // This should not have changed the existing suggestions.
  EXPECT_THAT(
      provider->GetSuggestionsForTesting(articles_category()),
      ElementsAre(Pointee(Property(&RemoteSuggestion::id, "http://abc.com/"))));
}

TEST_F(RemoteSuggestionsProviderImplTest, Dismiss) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://site.com"));
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
  // Load the image to store it in the database.
  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .WillOnce(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(base::BindRepeating(&ServeOneByOneImage)))));
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  gfx::Image image =
      FetchImage(provider.get(), MakeArticleID("http://site.com"));
  EXPECT_FALSE(image.IsEmpty());
  EXPECT_EQ(1, image.Width());

  // Dismissing a non-existent suggestion shouldn't do anything.
  provider->DismissSuggestion(MakeArticleID("http://othersite.com"));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));

  // Dismiss the suggestion.
  provider->DismissSuggestion(MakeArticleID("http://site.com"));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());

  // Verify we can still load the image of the discarded suggestion (other NTPs
  // might still reference it). This should come from the database -- no network
  // fetch necessary.
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  image = FetchImage(provider.get(), MakeArticleID("http://site.com"));
  EXPECT_FALSE(image.IsEmpty());
  EXPECT_EQ(1, image.Width());

  // Make sure that fetching the same suggestion again does not re-add it.
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());

  // The suggestion should stay dismissed even after re-creating the provider.
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());

  // The suggestion can be added again after clearing dismissed suggestions.
  provider->ClearDismissedSuggestionsForDebugging(articles_category());
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest, GetDismissed) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://site.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  provider->DismissSuggestion(MakeArticleID("http://site.com"));

  provider->GetDismissedSuggestionsForDebugging(
      articles_category(),
      base::BindOnce(
          [](RemoteSuggestionsProviderImpl* provider,
             RemoteSuggestionsProviderImplTest* test,
             std::vector<ContentSuggestion> dismissed_suggestions) {
            EXPECT_EQ(1u, dismissed_suggestions.size());
            for (auto& suggestion : dismissed_suggestions) {
              EXPECT_EQ(test->MakeArticleID("http://site.com"),
                        suggestion.id());
            }
          },
          provider.get(), this));
  RunUntilIdle();

  // There should be no dismissed suggestion after clearing the list.
  provider->ClearDismissedSuggestionsForDebugging(articles_category());
  provider->GetDismissedSuggestionsForDebugging(
      articles_category(),
      base::BindOnce(
          [](RemoteSuggestionsProviderImpl* provider,
             RemoteSuggestionsProviderImplTest* test,
             std::vector<ContentSuggestion> dismissed_suggestions) {
            EXPECT_EQ(0u, dismissed_suggestions.size());
          },
          provider.get(), this));
  RunUntilIdle();
}

TEST_F(RemoteSuggestionsProviderImplTest, RemoveExpiredDismissedContent) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://first/")
                                       .SetExpiryDate(base::Time::Now()))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // Load the image to store it in the database.
  // TODO(tschumann): Introduce some abstraction to nicely work with image
  // fetching expectations.
  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .WillOnce(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(base::BindRepeating(&ServeOneByOneImage)))));
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));
  gfx::Image image = FetchImage(provider.get(), MakeArticleID("http://first/"));
  EXPECT_FALSE(image.IsEmpty());
  EXPECT_EQ(1, image.Width());

  // Dismiss the suggestion
  provider->DismissSuggestion(
      ContentSuggestion::ID(articles_category(), "http://first/"));

  // Load a different suggestion - this will clear the expired dismissed ones.
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://second/"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(provider->GetDismissedSuggestionsForTesting(articles_category()),
              IsEmpty());

  // Verify the image got removed, too.
  EXPECT_TRUE(
      FetchImage(provider.get(), MakeArticleID("http://first/")).IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, ExpiredContentNotRemoved) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetExpiryDate(base::Time::Now()))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest, TestSingleSource) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://source1.com")
                                       .SetUrl("http://source1.com")
                                       .SetPublisher("Source 1")
                                       .SetAmpUrl("http://source1.amp.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
  const RemoteSuggestion& suggestion =
      *provider->GetSuggestionsForTesting(articles_category()).front();
  EXPECT_EQ(suggestion.id(), "http://source1.com");
  EXPECT_EQ(suggestion.url(), GURL("http://source1.com"));
  EXPECT_EQ(suggestion.publisher_name(), std::string("Source 1"));
  EXPECT_EQ(suggestion.amp_url(), GURL("http://source1.amp.com"));
}

TEST_F(RemoteSuggestionsProviderImplTest, TestSingleSourceWithMissingData) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetPublisher("").SetAmpUrl(""))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, LogNumArticlesHistogram) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  base::HistogramTester tester;

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status(StatusCode::TEMPORARY_ERROR, "message"),
                        base::nullopt);
  // Error responses don't update the list of suggestions and shouldn't
  // influence these metrics.
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              IsEmpty());
  // Fetch error shouldn't contribute to NumArticlesFetched.
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              IsEmpty());

  // TODO(tschumann): The expectations in these tests have high dependencies on
  // the sequence of unrelated events. This test should be split up into
  // multiple tests.

  // Empty categories list.
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::vector<FetchedCategory>());
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              IsEmpty());

  // Empty articles category.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder().SetCategory(articles_category()).Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/2)));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));

  // Suggestion list should be populated with size 1.
  const FetchedCategoryBuilder category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://site.com/"));
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/2),
                          base::Bucket(/*min=*/1, /*count=*/1)));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1),
                          base::Bucket(/*min=*/1, /*count=*/1)));

  // Duplicate suggestion shouldn't increase the list size.
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/2),
                          base::Bucket(/*min=*/1, /*count=*/2)));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1),
                          base::Bucket(/*min=*/1, /*count=*/2)));
  EXPECT_THAT(
      tester.GetAllSamples("NewTabPage.Snippets.NumArticlesZeroDueToDiscarded"),
      IsEmpty());

  // Dismissing a suggestion should decrease the list size. This will only be
  // logged after the next fetch.
  provider->DismissSuggestion(MakeArticleID("http://site.com/"));
  fetched_categories.clear();
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticles"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/3),
                          base::Bucket(/*min=*/1, /*count=*/2)));
  // Dismissed suggestions shouldn't influence NumArticlesFetched.
  EXPECT_THAT(tester.GetAllSamples("NewTabPage.Snippets.NumArticlesFetched"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1),
                          base::Bucket(/*min=*/1, /*count=*/3)));
  EXPECT_THAT(
      tester.GetAllSamples("NewTabPage.Snippets.NumArticlesZeroDueToDiscarded"),
      ElementsAre(base::Bucket(/*min=*/1, /*count=*/1)));
}

TEST_F(RemoteSuggestionsProviderImplTest, DismissShouldRespectAllKnownUrls) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  const std::vector<std::string> source_urls = {
      "http://mashable.com/2016/05/11/stolen",
      "http://www.aol.com/article/2016/05/stolen-doggie"};
  const std::vector<std::string> publishers = {"Mashable", "AOL"};
  const std::vector<std::string> amp_urls = {
      "http://mashable-amphtml.googleusercontent.com/1",
      "http://t2.gstatic.com/images?q=tbn:3"};

  // Add the suggestion from the mashable domain.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId(source_urls[0])
                                       .AddId(source_urls[1])
                                       .SetUrl(source_urls[0])
                                       .SetAmpUrl(amp_urls[0])
                                       .SetPublisher(publishers[0]))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));
  // Dismiss the suggestion via the mashable source corpus ID.
  provider->DismissSuggestion(MakeArticleID(source_urls[0]));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());

  // The same article from the AOL domain should now be detected as dismissed.
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId(source_urls[0])
                                       .AddId(source_urls[1])
                                       .SetUrl(source_urls[1])
                                       .SetAmpUrl(amp_urls[1])
                                       .SetPublisher(publishers[1]))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, ImageReturnedWithTheSameId) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId(kSuggestionUrl))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  gfx::Image image;
  MockFunction<void(const gfx::Image&)> image_fetched;
  {
    InSequence s;
    EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
        .WillOnce(WithArgs<0, 2, 3>(
            Invoke(CreateFunctor(base::BindRepeating(&ServeOneByOneImage)))));
    EXPECT_CALL(image_fetched, Call(_)).WillOnce(SaveArg<0>(&image));
  }

  provider->FetchSuggestionImage(
      MakeArticleID(kSuggestionUrl),
      base::BindOnce(&MockFunction<void(const gfx::Image&)>::Call,
                     base::Unretained(&image_fetched)));
  RunUntilIdle();
  // Check that the image by ServeOneByOneImage is really served.
  EXPECT_EQ(1, image.Width());
}

TEST_F(RemoteSuggestionsProviderImplTest, EmptyImageReturnedForNonExistentId) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Create a non-empty image so that we can test the image gets updated.
  gfx::Image image = gfx::test::CreateImage(1, 1);
  MockFunction<void(const gfx::Image&)> image_fetched;
  EXPECT_CALL(image_fetched, Call(_)).WillOnce(SaveArg<0>(&image));

  provider->FetchSuggestionImage(
      MakeArticleID("nonexistent"),
      base::BindOnce(&MockFunction<void(const gfx::Image&)>::Call,
                     base::Unretained(&image_fetched)));

  RunUntilIdle();
  EXPECT_TRUE(image.IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       FetchingUnknownImageIdShouldNotHitDatabase) {
  // Testing that the provider is not accessing the database is tricky.
  // Therefore, we simply put in some data making sure that if the provider asks
  // the database, it will get a wrong answer.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  ContentSuggestion::ID unknown_id = MakeArticleID(kSuggestionUrl2);
  database()->SaveImage(unknown_id.id_within_category(), "some image blob");
  // Set up the image decoder to always return the 1x1 test image.
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));

  // Create a non-empty image so that we can test the image gets updated.
  gfx::Image image = gfx::test::CreateImage(2, 2);
  MockFunction<void(const gfx::Image&)> image_fetched;
  EXPECT_CALL(image_fetched, Call(_)).WillOnce(SaveArg<0>(&image));

  provider->FetchSuggestionImage(
      MakeArticleID(kSuggestionUrl2),
      base::BindOnce(&MockFunction<void(const gfx::Image&)>::Call,
                     base::Unretained(&image_fetched)));

  RunUntilIdle();
  EXPECT_TRUE(image.IsEmpty()) << "got image with width: " << image.Width();
}

TEST_F(RemoteSuggestionsProviderImplTest, ClearHistoryRemovesAllSuggestions) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://first/"))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://second/"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(2));

  provider->DismissSuggestion(MakeArticleID("http://first/"));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              Not(IsEmpty()));
  ASSERT_THAT(provider->GetDismissedSuggestionsForTesting(articles_category()),
              SizeIs(1));

  base::Time begin = base::Time::FromTimeT(123),
             end = base::Time::FromTimeT(456);
  base::Callback<bool(const GURL& url)> filter;
  provider->ClearHistory(begin, end, filter);

  // Verify that the observer received the update with the empty data as well.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              IsEmpty());
  EXPECT_THAT(provider->GetDismissedSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldKeepArticlesCategoryAvailableAfterClearHistory) {
  // If the provider marks that category as NOT_PROVIDED, then it won't be shown
  // at all in the UI and the user cannot load new data :-/.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  ASSERT_THAT(observer().StatusForCategory(articles_category()),
              Eq(CategoryStatus::AVAILABLE));
  provider->ClearHistory(base::Time::UnixEpoch(), base::Time::Max(),
                         base::Callback<bool(const GURL& url)>());

  EXPECT_THAT(observer().StatusForCategory(articles_category()),
              Eq(CategoryStatus::AVAILABLE));
}

TEST_F(RemoteSuggestionsProviderImplTest, ShouldClearOrphanedImagesOnRestart) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId(kSuggestionUrl))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_CALL(*image_fetcher(), FetchImageAndData_(_, _, _, _, _))
      .WillOnce(WithArgs<0, 2, 3>(
          Invoke(CreateFunctor(base::BindRepeating(&ServeOneByOneImage)))));
  image_decoder()->SetDecodedImage(gfx::test::CreateImage(1, 1));

  gfx::Image image = FetchImage(provider.get(), MakeArticleID(kSuggestionUrl));
  EXPECT_EQ(1, image.Width());
  EXPECT_FALSE(image.IsEmpty());

  // Send new suggestion which don't include the suggestion referencing the
  // image.
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(
              "http://something.com/pletely/unrelated"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // The image should still be available until a restart happens.
  EXPECT_FALSE(
      FetchImage(provider.get(), MakeArticleID(kSuggestionUrl)).IsEmpty());
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  // After the restart, the image should be garbage collected.
  EXPECT_TRUE(
      FetchImage(provider.get(), MakeArticleID(kSuggestionUrl)).IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldHandleMoreThanMaxSuggestionsInResponse) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  for (int i = 0;
       i < provider->GetMaxNormalFetchSuggestionCountForTesting() + 1; ++i) {
    category_builder.AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(
        base::StringPrintf("http://localhost/suggestion-id-%d", i)));
  }
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // TODO(tschumann): We should probably trim out any additional results and
  // only serve the MaxSuggestionCount items.
  EXPECT_THAT(
      provider->GetSuggestionsForTesting(articles_category()),
      SizeIs(provider->GetMaxNormalFetchSuggestionCountForTesting() + 1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       StoreLastSuccessfullBackgroundFetchTime) {
  // On initialization of the RemoteSuggestionsProviderImpl a background fetch
  // is triggered since the suggestions DB is empty. Therefore the provider must
  // not be initialized until the test clock is set.
  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  base::SimpleTestClock simple_test_clock;
  provider->SetClockForTesting(&simple_test_clock);

  // Test that the preference is correctly initialized with the default value 0.
  EXPECT_EQ(
      0, pref_service()->GetInt64(prefs::kLastSuccessfulBackgroundFetchTime));

  WaitForSuggestionsProviderInitialization(provider.get());
  EXPECT_EQ(
      SerializeTime(simple_test_clock.Now()),
      pref_service()->GetInt64(prefs::kLastSuccessfulBackgroundFetchTime));

  // Advance the time and check whether the time was updated correctly after the
  // background fetch.
  simple_test_clock.Advance(base::TimeDelta::FromHours(1));

  RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
      .RetiresOnSaturation();
  provider->RefetchInTheBackground(
      RemoteSuggestionsProvider::FetchStatusCallback());
  RunUntilIdle();
  std::move(snippets_callback).Run(Status::Success(), base::nullopt);
  // TODO(jkrcal): Move together with the pref storage into the scheduler.
  EXPECT_EQ(
      SerializeTime(simple_test_clock.Now()),
      pref_service()->GetInt64(prefs::kLastSuccessfulBackgroundFetchTime));
  // TODO(markusheintz): Add a test that simulates a browser restart once the
  // scheduler refactoring is done (crbug.com/672434).
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerWhenReady) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();

  // Should be called when becoming ready.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerOnError) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();

  // Should be called on error.
  EXPECT_CALL(*scheduler(), OnProviderDeactivated());
  provider->EnterState(RemoteSuggestionsProviderImpl::State::ERROR_OCCURRED);
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerWhenDisabled) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();

  // Should be called when becoming disabled. First deactivate and only after
  // that clear the suggestions so that they are not fetched again.
  {
    InSequence s;
    EXPECT_CALL(*scheduler(), OnProviderDeactivated());
    ASSERT_THAT(provider->ready(), Eq(false));
    EXPECT_CALL(*scheduler(), OnSuggestionsCleared());
  }
  provider->EnterState(RemoteSuggestionsProviderImpl::State::DISABLED);
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerWhenHistoryCleared) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();
  // Initiate the provider so that it is already READY.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());

  // The scheduler should be notified of clearing the history.
  EXPECT_CALL(*scheduler(), OnHistoryCleared());
  provider->ClearHistory(GetDefaultCreationTime(), GetDefaultExpirationTime(),
                         base::Callback<bool(const GURL& url)>());
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerWhenSignedIn) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();
  // Initiate the provider so that it is already READY.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());

  // The scheduler should be notified of clearing the history.
  EXPECT_CALL(*scheduler(), OnSuggestionsCleared());
  provider->OnStatusChanged(RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN,
                            RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
}

TEST_F(RemoteSuggestionsProviderImplTest, CallsSchedulerWhenSignedOut) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();
  // Initiate the provider so that it is already READY.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());

  // The scheduler should be notified of clearing the history.
  EXPECT_CALL(*scheduler(), OnSuggestionsCleared());
  provider->OnStatusChanged(RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT,
                            RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN);
}

TEST_F(RemoteSuggestionsProviderImplTest,
       RestartsFetchWhenSignedInWhileFetching) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();
  // Initiate the provider so that it is already READY.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());

  // Initiate the fetch.
  RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(MoveSecondArgumentPointeeTo(&snippets_callback))
      .RetiresOnSaturation();
  provider->FetchSuggestions(/*interactive_request=*/false,
                             RemoteSuggestionsProvider::FetchStatusCallback());

  // The scheduler should be notified of clearing the suggestions.
  EXPECT_CALL(*scheduler(), OnSuggestionsCleared());
  provider->OnStatusChanged(RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT,
                            RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN);

  // Once we signal the first fetch to be finished (calling snippets_callback
  // below), a new fetch should get triggered.
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _)).Times(1);
  std::move(snippets_callback)
      .Run(Status::Success(), std::vector<FetchedCategory>());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       IgnoresResultsWhenHistoryClearedWhileFetching) {
  auto provider =
      MakeSuggestionsProviderWithoutInitializationWithStrictScheduler();
  // Initiate the provider so that it is already READY.
  EXPECT_CALL(*scheduler(), OnProviderActivated());
  WaitForSuggestionsProviderInitialization(provider.get());

  // Initiate the fetch.
  RemoteSuggestionsFetcher::SnippetsAvailableCallback snippets_callback =
      FetchSuggestionsAndGetResponseCallback(provider.get(),
                                             /*interactive_request=*/false);

  // The scheduler should be notified of clearing the history.
  EXPECT_CALL(*scheduler(), OnHistoryCleared());
  provider->ClearHistory(GetDefaultCreationTime(), GetDefaultExpirationTime(),
                         base::RepeatingCallback<bool(const GURL& url)>());

  // Once the fetch finishes, the returned suggestions are ignored.
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  category_builder.AddSuggestionViaBuilder(
      RemoteSuggestionBuilder().AddId(base::StringPrintf("http://abc.com")));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(category_builder.Build());
  std::move(snippets_callback)
      .Run(Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(0));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldExcludeKnownSuggestionsWithoutTruncatingWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::set<std::string> known_ids;
  for (int i = 0; i < 200; ++i) {
    known_ids.insert(base::IntToString(i));
  }

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids, known_ids), _));
  provider->Fetch(
      articles_category(), known_ids,
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldExcludeDismissedSuggestionsWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().AddId("http://abc.com/"))
          .Build());
  ASSERT_TRUE(fetched_categories[0].suggestions[0]->is_complete());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  provider->DismissSuggestion(MakeArticleID("http://abc.com/"));

  std::set<std::string> expected_excluded_ids({"http://abc.com/"});
  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  EXPECT_CALL(
      *mock_suggestions_fetcher(),
      FetchSnippets(Field(&RequestParams::excluded_ids, expected_excluded_ids),
                    _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldTruncateExcludedDismissedSuggestionsWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  const int kSuggestionsCount = kMaxExcludedDismissedIds + 1;
  for (int i = 0; i < kSuggestionsCount; ++i) {
    category_builder.AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(
        base::StringPrintf("http://abc.com/%d/", i)));
  }
  fetched_categories.push_back(category_builder.Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  // Dismiss them.
  for (int i = 0; i < kSuggestionsCount; ++i) {
    provider->DismissSuggestion(
        MakeArticleID(base::StringPrintf("http://abc.com/%d/", i)));
  }

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids,
                                  SizeIs(kMaxExcludedDismissedIds)),
                            _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldPreferLatestExcludedDismissedSuggestionsWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  const int kSuggestionsCount = kMaxExcludedDismissedIds + 1;
  for (int i = 0; i < kSuggestionsCount; ++i) {
    category_builder.AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(
        base::StringPrintf("http://abc.com/%d/", i)));
  }
  fetched_categories.push_back(category_builder.Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Dismiss them in reverse order.
  std::string first_dismissed_suggestion_id;
  for (int i = kSuggestionsCount - 1; i >= 0; --i) {
    const std::string id = base::StringPrintf("http://abc.com/%d/", i);
    provider->DismissSuggestion(MakeArticleID(id));
    if (first_dismissed_suggestion_id.empty()) {
      first_dismissed_suggestion_id = id;
    }
  }

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  // The oldest dismissed suggestion should be absent, because there are
  // |kMaxExcludedDismissedIds| newer dismissed suggestions.
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids,
                                  Not(Contains(first_dismissed_suggestion_id))),
                            _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldExcludeDismissedFetchedMoreSuggestions) {
  // This tests verifies that dismissing an article seen in the fetch-more state
  // (i.e., an article that has been fetched via fetch-more) will be excluded in
  // future fetches.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  const int kSuggestionsCount = 5;
  for (int i = 0; i < kSuggestionsCount; ++i) {
    category_builder.AddSuggestionViaBuilder(RemoteSuggestionBuilder().AddId(
        base::StringPrintf("http://abc.com/%d", i)));
  }
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(category_builder.Build());

  FetchMoreTheseSuggestions(
      provider.get(), articles_category(),
      /*known_suggestion_ids=*/std::set<std::string>(),
      /*fetch_done_callback=*/
      base::BindOnce(
          [](Status status, std::vector<ContentSuggestion> suggestions) {
            ASSERT_THAT(suggestions, SizeIs(5));
          }),
      Status::Success(), std::move(fetched_categories));

  // Dismiss them.
  for (int i = 0; i < kSuggestionsCount; ++i) {
    provider->DismissSuggestion(
        MakeArticleID(base::StringPrintf("http://abc.com/%d", i)));
  }

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  EXPECT_CALL(
      *mock_suggestions_fetcher(),
      FetchSnippets(Field(&RequestParams::excluded_ids,
                          ElementsAre("http://abc.com/0", "http://abc.com/1",
                                      "http://abc.com/2", "http://abc.com/3",
                                      "http://abc.com/4")),
                    _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest, ClearDismissedAfterFetchMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  category_builder.AddSuggestionViaBuilder(
      RemoteSuggestionBuilder().AddId(base::StringPrintf("http://abc.com")));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(category_builder.Build());

  FetchMoreTheseSuggestions(
      provider.get(), articles_category(),
      /*known_suggestion_ids=*/std::set<std::string>(),
      /*fetch_done_callback=*/
      base::BindOnce(
          [](Status status, std::vector<ContentSuggestion> suggestions) {}),
      Status::Success(), std::move(fetched_categories));

  provider->DismissSuggestion(MakeArticleID("http://abc.com"));

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillRepeatedly(Return(true));

  // Make sure the article got marked as dismissed.
  InSequence s;
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids,
                                  ElementsAre("http://abc.com")),
                            _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));

  // Clear dismissals.
  provider->ClearDismissedSuggestionsForDebugging(articles_category());

  // Fetch and verify the article is not marked as dismissed anymore.
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids, IsEmpty()), _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldExcludeDismissedSuggestionsFromAllCategoriesWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Add article suggestions.
  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder first_category_builder;
  first_category_builder.SetCategory(articles_category());
  const int kSuggestionsPerCategory = 2;
  for (int i = 0; i < kSuggestionsPerCategory; ++i) {
    first_category_builder.AddSuggestionViaBuilder(
        RemoteSuggestionBuilder().AddId(
            base::StringPrintf("http://abc.com/%d/", i)));
  }
  fetched_categories.push_back(first_category_builder.Build());
  // Add other category suggestions.
  FetchedCategoryBuilder second_category_builder;
  second_category_builder.SetCategory(other_category());
  for (int i = 0; i < kSuggestionsPerCategory; ++i) {
    second_category_builder.AddSuggestionViaBuilder(
        RemoteSuggestionBuilder().AddId(
            base::StringPrintf("http://other.com/%d/", i)));
  }
  fetched_categories.push_back(second_category_builder.Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Dismiss all suggestions.
  std::set<std::string> expected_excluded_ids;
  for (int i = 0; i < kSuggestionsPerCategory; ++i) {
    const std::string article_id = base::StringPrintf("http://abc.com/%d/", i);
    provider->DismissSuggestion(MakeArticleID(article_id));
    expected_excluded_ids.insert(article_id);
    const std::string other_id = base::StringPrintf("http://other.com/%d/", i);
    provider->DismissSuggestion(MakeOtherID(other_id));
    expected_excluded_ids.insert(other_id);
  }

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  // Dismissed suggestions from all categories must be excluded (but not only
  // target category).
  EXPECT_CALL(
      *mock_suggestions_fetcher(),
      FetchSnippets(Field(&RequestParams::excluded_ids, expected_excluded_ids),
                    _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldPreferTargetCategoryExcludedDismissedSuggestionsWhenFetchingMore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Add article suggestions.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(FetchedCategory(
      articles_category(),
      BuildRemoteCategoryInfo(base::UTF8ToUTF16("title"),
                              /*allow_fetching_more_results=*/true)));

  for (int i = 0; i < kMaxExcludedDismissedIds; ++i) {
    fetched_categories[0].suggestions.push_back(CreateTestRemoteSuggestion(
        base::StringPrintf("http://abc.com/%d/", i)));
  }
  // Add other category suggestion.
  fetched_categories.push_back(FetchedCategory(
      other_category(),
      BuildRemoteCategoryInfo(base::UTF8ToUTF16("title"),
                              /*allow_fetching_more_results=*/true)));
  fetched_categories[1].suggestions.push_back(
      CreateTestRemoteSuggestion("http://other.com/"));

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Dismiss article suggestions first.
  for (int i = 0; i < kMaxExcludedDismissedIds; ++i) {
    provider->DismissSuggestion(
        MakeArticleID(base::StringPrintf("http://abc.com/%d/", i)));
  }

  // Then dismiss other category suggestion.
  provider->DismissSuggestion(MakeOtherID("http://other.com/"));

  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  // The other category dismissed suggestion should be absent, because the fetch
  // is for articles and there are |kMaxExcludedDismissedIds| dismissed
  // suggestions there.
  EXPECT_CALL(*mock_suggestions_fetcher(),
              FetchSnippets(Field(&RequestParams::excluded_ids,
                                  Not(Contains("http://other.com/"))),
                            _));
  provider->Fetch(
      articles_category(), std::set<std::string>(),
      base::BindOnce([](Status status_code,
                        std::vector<ContentSuggestion> suggestions) {}));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldFetchNormallyWithoutPrefetchedPagesTracker) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder())
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldKeepPrefetchedSuggestionsAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prefetched.com")
                                       .SetUrl("http://prefetched.com")
                                       .SetAmpUrl("http://amp.prefetched.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com")))
      .WillOnce(Return(true));
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://other.com")
                                       .SetUrl("http://other.com")
                                       .SetAmpUrl("http://amp.other.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      UnorderedElementsAre(
          Property(&ContentSuggestion::id,
                   MakeArticleID("http://prefetched.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://other.com"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldIgnoreNotPrefetchedSuggestionsAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://not_prefetched.com")
                  .SetUrl("http://not_prefetched.com")
                  .SetAmpUrl("http://amp.not_prefetched.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_tracker, PrefetchedOfflinePageExists(
                                 GURL("http://amp.not_prefetched.com")))
      .WillOnce(Return(false));
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://other.com")
                                       .SetUrl("http://other.com")
                                       .SetAmpUrl("http://amp.other.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              UnorderedElementsAre(Property(
                  &ContentSuggestion::id, MakeArticleID("http://other.com"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldLimitKeptPrefetchedSuggestionsAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();
  WaitForSuggestionsProviderInitialization(provider.get());

  const int prefetched_suggestions_count =
      2 * kMaxAdditionalPrefetchedSuggestions + 1;
  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder;
  category_builder.SetCategory(articles_category());
  for (int i = 0; i < prefetched_suggestions_count; ++i) {
    const std::string url = base::StringPrintf("http://prefetched.com/%d", i);
    category_builder.AddSuggestionViaBuilder(
        RemoteSuggestionBuilder().AddId(url).SetUrl(url).SetAmpUrl(
            base::StringPrintf("http://amp.prefetched.com/%d", i)));
  }
  fetched_categories.push_back(category_builder.Build());

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(prefetched_suggestions_count));

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  for (int i = 0; i < prefetched_suggestions_count; ++i) {
    EXPECT_CALL(*mock_tracker,
                PrefetchedOfflinePageExists(GURL(
                    base::StringPrintf("http://amp.prefetched.com/%d", i))))
        .WillOnce(Return(true));
  }
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://not_prefetched.com")
                  .SetUrl("http://not_prefetched.com")
                  .SetAmpUrl("http://amp.not_prefetched.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(kMaxAdditionalPrefetchedSuggestions + 1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldMixInPrefetchedSuggestionsByScoreAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prefetched.com/1")
                                       .SetUrl("http://prefetched.com/1")
                                       .SetAmpUrl("http://amp.prefetched.com/1")
                                       .SetScore(1))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prefetched.com/3")
                                       .SetUrl("http://prefetched.com/3")
                                       .SetAmpUrl("http://amp.prefetched.com/3")
                                       .SetScore(3))
          .Build());
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(2));

  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://new.com/2")
                                       .SetUrl("http://new.com/2")
                                       .SetAmpUrl("http://amp.new.com/2")
                                       .SetScore(2))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://new.com/4")
                                       .SetUrl("http://new.com/4")
                                       .SetAmpUrl("http://amp.new.com/4")
                                       .SetScore(4))
          .Build());

  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com/3")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::id, MakeArticleID("http://new.com/4")),
          Property(&ContentSuggestion::id,
                   MakeArticleID("http://prefetched.com/3")),
          Property(&ContentSuggestion::id, MakeArticleID("http://new.com/2")),
          Property(&ContentSuggestion::id,
                   MakeArticleID("http://prefetched.com/1"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       KeepMostRecentlyFetchedPrefetchedSuggestionsFirstAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();

  std::vector<FetchedCategory> fetched_categories;
  const int prefetched_suggestions_count =
      2 * kMaxAdditionalPrefetchedSuggestions + 1;
  for (int i = 0; i < prefetched_suggestions_count; ++i) {
    const std::string url = base::StringPrintf("http://prefetched.com/%d", i);
    fetched_categories.push_back(
        FetchedCategoryBuilder()
            .SetCategory(articles_category())
            .AddSuggestionViaBuilder(
                RemoteSuggestionBuilder().AddId(url).SetUrl(url).SetAmpUrl(
                    base::StringPrintf("http://amp.prefetched.com/%d", i)))
            .Build());
    EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
    if (i != 0) {
      EXPECT_CALL(*mock_tracker,
                  PrefetchedOfflinePageExists(GURL(base::StringPrintf(
                      "http://amp.prefetched.com/%d", i - 1))))
          .WillRepeatedly(Return(true));
    }

    FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                          Status::Success(), std::move(fetched_categories));
  }

  const std::vector<ContentSuggestion>& actual_suggestions =
      observer().SuggestionsForCategory(articles_category());

  ASSERT_THAT(actual_suggestions,
              SizeIs(kMaxAdditionalPrefetchedSuggestions + 1));

  int matched = 0;
  for (int i = prefetched_suggestions_count - 1; i >= 0; --i) {
    EXPECT_THAT(actual_suggestions,
                Contains(Property(&ContentSuggestion::id,
                                  MakeArticleID(base::StringPrintf(
                                      "http://prefetched.com/%d", i)))));
    ++matched;
    if (matched == kMaxAdditionalPrefetchedSuggestions + 1) {
      break;
    }
  }
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotKeepStalePrefetchedSuggestionsAfterFetchWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();

  base::SimpleTestClock provider_clock;
  provider->SetClockForTesting(&provider_clock);

  provider_clock.SetNow(GetDefaultCreationTime() +
                        base::TimeDelta::FromHours(10));

  WaitForSuggestionsProviderInitialization(provider.get());
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://prefetched.com")
                  .SetUrl("http://prefetched.com")
                  .SetAmpUrl("http://amp.prefetched.com")
                  .SetFetchDate(provider_clock.Now())
                  .SetPublishDate(GetDefaultCreationTime()))
          .Build());
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  provider_clock.Advance(kMaxAgeForAdditionalPrefetchedSuggestion -
                         base::TimeDelta::FromSeconds(1));

  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://other.com")
                  .SetUrl("http://other.com")
                  .SetAmpUrl("http://amp.other.com")
                  .SetFetchDate(provider_clock.Now())
                  .SetPublishDate(GetDefaultCreationTime()))
          .Build());
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com")))
      .WillOnce(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(2));

  provider_clock.Advance(base::TimeDelta::FromSeconds(2));

  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://other.com")
                  .SetUrl("http://other.com")
                  .SetAmpUrl("http://amp.other.com")
                  .SetFetchDate(provider_clock.Now())
                  .SetPublishDate(GetDefaultCreationTime()))
          .Build());
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com")))
      .WillOnce(Return(true));
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::id,
                                   MakeArticleID("http://other.com"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldWaitForPrefetchedPagesTrackerInitialization) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();

  base::OnceCallback<void()> initialization_completed_callback;
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_tracker, Initialize(_))
      .WillOnce(MoveFirstArgumentPointeeTo(&initialization_completed_callback));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prefetched.com")
                                       .SetUrl("http://prefetched.com")
                                       .SetAmpUrl("http://amp.prefetched.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(0));

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  std::move(initialization_completed_callback).Run();
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldRestoreSuggestionsFromDatabaseInSameOrderAsFetched) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://1.com")
                                       .SetUrl("http://1.com")
                                       .SetScore(1))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://3.com")
                                       .SetUrl("http://3.com")
                                       .SetScore(3))
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://2.com")
                                       .SetUrl("http://2.com")
                                       .SetScore(2))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::id, MakeArticleID("http://1.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://3.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://2.com"))));

  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::id, MakeArticleID("http://1.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://3.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://2.com"))));
}

// TODO(vitaliii): Remove this test (as well as the score fallback) in M64.
TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldSortSuggestionsWithoutRanksByScore) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Write suggestions without ranks (i.e. with default values) directly to
  // database to simulate behaviour of M61.
  std::vector<std::unique_ptr<RemoteSuggestion>> suggestions;
  suggestions.push_back(RemoteSuggestionBuilder()
                            .AddId("http://1.com")
                            .SetUrl("http://1.com")
                            .SetScore(1)
                            .SetRank(std::numeric_limits<int>::max())
                            .Build());
  suggestions.push_back(RemoteSuggestionBuilder()
                            .AddId("http://3.com")
                            .SetUrl("http://3.com")
                            .SetScore(3)
                            .SetRank(std::numeric_limits<int>::max())
                            .Build());
  suggestions.push_back(RemoteSuggestionBuilder()
                            .AddId("http://2.com")
                            .SetUrl("http://2.com")
                            .SetScore(2)
                            .SetRank(std::numeric_limits<int>::max())
                            .Build());

  database()->SaveSnippets(suggestions);

  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::id, MakeArticleID("http://3.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://2.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://1.com"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       PrependingShouldNotAffectOtherSuggestions) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  // Set up the provider with some article suggestions.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder =
      FetchedCategoryBuilder().SetCategory(articles_category());
  for (int i = 0; i < 10; ++i) {
    const std::string url = "http://other.com/" + base::IntToString(i);
    category_builder.AddSuggestionViaBuilder(
        RemoteSuggestionBuilder().AddId(url).SetUrl(url));
  }
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Prepend an article suggestion.
  const std::string prepended_url = "http://prepended.com/";
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId(prepended_url)
          .SetUrl(prepended_url)
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));

  // Check that the prepended suggestion is in the front, and all the others are
  // still there in the same order.
  std::vector<Matcher<const ContentSuggestion&>> expected;
  expected.push_back(
      Property(&ContentSuggestion::id, MakeArticleID(prepended_url)));
  for (int i = 0; i < 10; ++i) {
    expected.push_back(
        Property(&ContentSuggestion::id,
                 MakeArticleID("http://other.com/" + base::IntToString(i))));
  }

  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAreArray(expected));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotPrependIncompleteSuggestion) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Prepend an article suggestion.
  const RemoteSuggestionBuilder suggestion_builder =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetTitle(std::string());

  PushArticleSuggestionToTheFront(suggestion_builder.Build());
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, ShouldNotPrependDismissedSuggestion) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Prepend an article suggestion.
  const RemoteSuggestionBuilder suggestion_builder =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com");

  PushArticleSuggestionToTheFront(suggestion_builder.Build());
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              SizeIs(1));

  // Dismiss it.
  provider->DismissSuggestion(MakeArticleID("http://prepended.com"));
  ASSERT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());

  // Prepend it again and verify that it is ignored.
  PushArticleSuggestionToTheFront(suggestion_builder.Build());
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest, ShouldNotPrependExistingSuggestion) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  base::Time first_time;
  ASSERT_TRUE(base::Time::FromUTCString("2017-01-02T00:00:01Z", &first_time));
  base::Time second_time;
  ASSERT_TRUE(base::Time::FromUTCString("2017-01-02T00:00:02Z", &second_time));

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prepended.com")
                                       .SetUrl("http://prepended.com")
                                       .SetFetchDate(first_time))
          .Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(
                  Property(&RemoteSuggestion::fetch_date, first_time))));

  // Prepend the same article suggestion.
  const RemoteSuggestionBuilder suggestion_builder =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetFetchDate(second_time);

  PushArticleSuggestionToTheFront(suggestion_builder.Build());

  // The prepended suggestion should be ignored, because it was fetched
  // previously and is shown on NTP already.
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(
                  Property(&RemoteSuggestion::fetch_date, first_time))));
}

TEST_F(RemoteSuggestionsProviderImplTest, ShouldNotPrependArchivedSuggestion) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prepended.com")
                                       .SetUrl("http://prepended.com"))
          .Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::url,
                                           GURL("http://prepended.com")))));

  // Do another fetch to achive the existing article.
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://newly_fetched.com")
                                       .SetUrl("http://newly_fetched.com"))
          .Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::url,
                                           GURL("http://newly_fetched.com")))));

  // Prepend the previous article suggestion.
  const RemoteSuggestionBuilder suggestion_builder =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com");
  PushArticleSuggestionToTheFront(suggestion_builder.Build());

  // The prepended suggestion should be ignored again, because it was fetched
  // previously and may be shown on older NTPs.
  EXPECT_THAT(provider->GetSuggestionsForTesting(articles_category()),
              ElementsAre(Pointee(Property(&RemoteSuggestion::url,
                                           GURL("http://newly_fetched.com")))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldRestorePrependedSuggestionOnTopAfterRestart) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  // Set up the provider with some article suggestions.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  FetchedCategoryBuilder category_builder =
      FetchedCategoryBuilder().SetCategory(articles_category());
  for (int i = 0; i < 10; ++i) {
    const std::string url = "http://other.com/" + base::IntToString(i);
    category_builder.AddSuggestionViaBuilder(
        RemoteSuggestionBuilder().AddId(url).SetUrl(url));
  }
  fetched_categories.push_back(category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Prepend an article suggestion.
  const std::string prepended_url = "http://prepended.com";
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId(prepended_url)
          .SetUrl(prepended_url)
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));

  // Reset the provider to imitate browser restart.
  ResetSuggestionsProvider(
      &provider,
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Check that prepended suggestion is in the front of the restored list and
  // all other suggestions are present in the same order.
  std::vector<Matcher<const ContentSuggestion&>> expected;
  expected.push_back(
      Property(&ContentSuggestion::id, MakeArticleID(prepended_url)));
  for (int i = 0; i < 10; ++i) {
    expected.push_back(
        Property(&ContentSuggestion::id,
                 MakeArticleID("http://other.com/" + base::IntToString(i))));
  }

  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAreArray(expected));
}

TEST_F(
    RemoteSuggestionsProviderImplTest,
    PrependingShouldNotTriggerFetchedSuggestionNotificationForTheSecondTime) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/true,
      /*pushed_notifications_enabled=*/true,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  // Set up the provider with an article suggestion triggering a notification.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://fetched.com/")
                  .SetUrl("http://fetched.com/")
                  .SetShouldNotify(true)
                  .SetNotificationDeadline(GetDefaultExpirationTime()))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::notification_extra,
                                   Not(nullptr))));

  // Prepend an article suggestion.
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetShouldNotify(true)
          .SetNotificationDeadline(GetDefaultExpirationTime())
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));

  // Previously fetched suggestion should not trigger a notification.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::notification_extra, Not(nullptr)),
          Property(&ContentSuggestion::notification_extra, nullptr)));
}

TEST_F(
    RemoteSuggestionsProviderImplTest,
    PrependingShouldNotTriggerPrependedSuggestionNotificationForTheSecondTime) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/true,
      /*pushed_notifications_enabled=*/true,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Prepend an article suggestion.
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetShouldNotify(true)
          .SetNotificationDeadline(GetDefaultExpirationTime())
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::notification_extra,
                                   Not(nullptr))));

  // Prepend another article suggestion.
  std::unique_ptr<RemoteSuggestion> another_prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId("http://another_prepended.com")
          .SetUrl("http://another_prepended.com")
          .SetShouldNotify(true)
          .SetNotificationDeadline(GetDefaultExpirationTime())
          .Build();

  PushArticleSuggestionToTheFront(std::move(another_prepended_suggestion));

  // Previously prepended suggestion should not trigger a notification.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::notification_extra, Not(nullptr)),
          Property(&ContentSuggestion::notification_extra, nullptr)));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       PrependingShouldNotTriggerNotificationWhenDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/true,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Prepend an article suggestion triggering a notification.
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetShouldNotify(true)
          .SetNotificationDeadline(GetDefaultExpirationTime())
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));

  // The prepended suggestion should not trigger a notification because such
  // notifications are disabled.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(Property(&ContentSuggestion::notification_extra, nullptr)));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       FetchingShouldNotTriggerNotificationWhenDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/true,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Fetch a suggestion triggering a notification.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://fetched.com/")
                  .SetUrl("http://fetched.com/")
                  .SetShouldNotify(true)
                  .SetNotificationDeadline(GetDefaultExpirationTime()))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // The fetched suggestion should not trigger a notification because such
  // notifications are disabled.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(Property(&ContentSuggestion::notification_extra, nullptr)));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       PrependingShouldTriggerNotificationEvenIfFetchedNotificationsDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/true,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Prepend an article suggestion triggering a notification.
  std::unique_ptr<RemoteSuggestion> prepended_suggestion =
      RemoteSuggestionBuilder()
          .AddId("http://prepended.com")
          .SetUrl("http://prepended.com")
          .SetShouldNotify(true)
          .SetNotificationDeadline(GetDefaultExpirationTime())
          .Build();

  PushArticleSuggestionToTheFront(std::move(prepended_suggestion));

  // The prepended suggestion should trigger a notification even though fetched
  // notifications are disabled.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::notification_extra,
                                   Not(nullptr))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       FetchingShouldTriggerNotificationEvenIfPrependedNotificationsDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/true,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Fetch a suggestion triggering a notification.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .AddId("http://fetched.com/")
                  .SetUrl("http://fetched.com/")
                  .SetShouldNotify(true)
                  .SetNotificationDeadline(GetDefaultExpirationTime()))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // The fetched suggestion should trigger a notification even though prepended
  // notifications are disabled.
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              ElementsAre(Property(&ContentSuggestion::notification_extra,
                                   Not(nullptr))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotStartListeningForBreakingNewsIfSuggestionsDisabledAtStartup) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();
  EXPECT_FALSE(fake_listener->IsListening());

  WaitForSuggestionsProviderInitialization(provider.get());
  EXPECT_FALSE(fake_listener->IsListening());

  // Notify the provider about status change (simulating startup). The provider
  // should not start listening, because the suggestions are disabled.
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
                                RemoteSuggestionsStatus::EXPLICITLY_DISABLED);
  EXPECT_FALSE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotStartListeningForBreakingNewsIfSignedOutAndDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/false);

  auto provider = MakeSuggestionsProviderWithoutInitialization(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();
  EXPECT_FALSE(fake_listener->IsListening());

  WaitForSuggestionsProviderInitialization(provider.get());
  EXPECT_FALSE(fake_listener->IsListening());

  // Notify the provider about status change (simulating startup). The provider
  // should not start listening, because the user is signed out and such
  // subscription is disabled via feature params.
  ChangeRemoteSuggestionsStatus(
      RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
  EXPECT_FALSE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldStartListeningForBreakingNewsIfSuggestionsEnabledAtStartup) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/false,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();
  ASSERT_FALSE(fake_listener->IsListening());

  // Notify the provider about status change (simulating startup). The provider
  // should start listening, because the suggestions are enabled.
  ChangeRemoteSuggestionsStatus(
      RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
  EXPECT_TRUE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldStartListeningForBreakingNewsIfSuggestionsEnabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/false);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();

  // Notify the provider about status change (simulating startup).
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
                                RemoteSuggestionsStatus::EXPLICITLY_DISABLED);
  ASSERT_FALSE(fake_listener->IsListening());

  // Simulate the user enabling suggestions by notifying the status change. The
  // provider should start listening.
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
                                RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN);
  EXPECT_TRUE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldStopListeningForBreakingNewsIfSuggestionsDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();

  // Notify the provider about status change (simulating startup).
  ChangeRemoteSuggestionsStatus(
      RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
  ASSERT_TRUE(fake_listener->IsListening());

  // Simulate the user disabling suggestions by notifying the status change. The
  // provider should stop listening.
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT,
                                RemoteSuggestionsStatus::EXPLICITLY_DISABLED);
  EXPECT_FALSE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldStopListeningForBreakingNewsAfterSignOutIfDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/true,
      /*subscribe_signed_out=*/false);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();

  // Notify the provider about status change (simulating startup).
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
                                RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN);
  ASSERT_TRUE(fake_listener->IsListening());

  // Simulate the user signing out by notifying the status change. The provider
  // should stop listening, because signed out subscription is disabled via
  // feature params.
  ChangeRemoteSuggestionsStatus(
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN,
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
  EXPECT_FALSE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldStopListeningForBreakingNewsAfterSignInIfDisabled) {
  SetTriggeringNotificationsAndSubscriptionParams(
      /*fetched_notifications_enabled=*/false,
      /*pushed_notifications_enabled=*/false,
      /*subscribe_signed_in=*/false,
      /*subscribe_signed_out=*/true);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/true,
      /*use_mock_remote_suggestions_status_service=*/true);

  FakeBreakingNewsListener* fake_listener = fake_breaking_news_listener();

  // Notify the provider about status change (simulating startup).
  ChangeRemoteSuggestionsStatus(
      RemoteSuggestionsStatus::EXPLICITLY_DISABLED,
      RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT);
  ASSERT_TRUE(fake_listener->IsListening());

  // Simulate the user signing in by notifying the status change. The provider
  // should stop listening, because signed in subscription is disabled via
  // feature params.
  ChangeRemoteSuggestionsStatus(RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT,
                                RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN);
  EXPECT_FALSE(fake_listener->IsListening());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldForceFetchedSuggestionsNotificationsWhenEnabled) {
  SetFetchedNotificationsParams(
      /*enabled=*/true, /*force=*/true);

  // Initialize the provider with two article suggestions - one with a
  // notification and one - without.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .SetUrl("http://article_with_notification.com")
                  .SetShouldNotify(true)
                  .SetNotificationDeadline(GetDefaultExpirationTime()))
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .SetUrl("http://article_without_notification.com")
                  .SetShouldNotify(false))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // For the observer, both suggestions must have notifications, because they
  // are forced via a feature param.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(
          Property(&ContentSuggestion::notification_extra, Not(nullptr)),
          Property(&ContentSuggestion::notification_extra, Not(nullptr))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotForceFetchedSuggestionsNotificationsWhenExplicitlyDisabled) {
  SetFetchedNotificationsParams(
      /*enabled=*/false, /*force=*/true);

  // Initialize the provider with an article suggestions without a notification.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder()
                  .SetUrl("http://article_without_notification.com")
                  .SetShouldNotify(false))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // For the observer, the suggestion still must not have a notification (even
  // though they are forced via a feature param), because the fetched
  // notifications are explicitly disabled via another feature param.
  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      ElementsAre(Property(&ContentSuggestion::notification_extra, nullptr)));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldDeleteNotFetchedCategoryWhenDeletionEnabled) {
  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(other_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://not_articles.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(other_category()));

  // Fetch only one category - articles.
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  // The other category must be gone, because it was not included in the last
  // fetch and the deletion is enabled via feature params.
  EXPECT_EQ(CategoryStatus::NOT_PROVIDED,
            observer().StatusForCategory(other_category()));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldKeepFetchedCategoryWhenDeletionEnabled) {
  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  const FetchedCategoryBuilder other_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(other_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://not_articles.com"));
  fetched_categories.push_back(other_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(other_category()));

  // Fetch the same two categories again.
  fetched_categories.push_back(articles_category_builder.Build());
  fetched_categories.push_back(other_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  // The other category must remain, because it was included in the last fetch.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(other_category()));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldKeepArticleCategoryEvenWhenNotFetchedAndDeletionEnabled) {
  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"))
          .Build());
  const FetchedCategoryBuilder other_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(other_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://not_articles.com"));
  fetched_categories.push_back(other_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  // Fetch only one other category.
  fetched_categories.push_back(other_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  // Articles category still must be provided (it is an exception) even though
  // it was not included in the last fetch and the deletion is enabled via
  // feature params.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       EmptySectionResponseShouldClearSection) {
  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Set up state with present suggestions.
  // Unfortunately, we cannot create the fetched_categories inline, as some part
  // requires a copy of FetchedCategory which is not supported :-/.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .SetUrl("http://articles.com")
                                       .SetAmpUrl(""))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  // Next fetch returns an empty article section.
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder().SetCategory(articles_category()).Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  // Articles category still must be provided, but empty.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              IsEmpty());
}

TEST_F(RemoteSuggestionsProviderImplTest,
       FetchErrorShouldLeaveSuggestionsUnchangedEmptySection) {
  // Tests that we don't interpret the response value in error cases.
  // Note, that the contract of the callback guarantees that we always send
  // a null value in error cases. However, such a contract is brittle and the
  // code is not too clear on the receiving side.
  // TODO(tschumann): Establish and enforce a clear and robust error handling
  // contract.

  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Set up state with present suggestions.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .SetUrl("http://articles.com")
                                       .SetAmpUrl(""))
          .Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  // Next fetch returns an error (with an empty section).
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder().SetCategory(articles_category()).Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status(StatusCode::TEMPORARY_ERROR, "some error"),
                        std::move(fetched_categories));

  // Articles category should stay unchanged.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category())[0].url(),
              GURL("http://articles.com"));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       FetchErrorShouldLeaveSuggestionsUnchangedNullResponse) {
  // Initialize the provider with two categories.
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  // Set up state with present suggestions.
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .SetUrl("http://articles.com")
                                       .SetAmpUrl(""))
          .Build());

  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  // Next fetch returns an error (with an empty section).
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status(StatusCode::TEMPORARY_ERROR, "some error"),
                        base::nullopt);

  // Articles category should stay unchanged.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category())[0].url(),
              GURL("http://articles.com"));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotSetExclusiveCategoryWhenFetchingSuggestions) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  RequestParams params;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(SaveArg<0>(&params));
  provider->FetchSuggestions(
      /*interactive_request=*/true,
      RemoteSuggestionsProvider::FetchStatusCallback());

  EXPECT_FALSE(params.exclusive_category.has_value());
  EXPECT_EQ(params.count_to_fetch, 10);
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldSetExclusiveCategoryAndCountToFetchWhenFetchingMoreSuggestions) {
  SetFetchMoreSuggestionsCount(35);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  RequestParams params;
  EXPECT_CALL(*mock_suggestions_fetcher(), FetchSnippets(_, _))
      .WillOnce(SaveArg<0>(&params));
  EXPECT_CALL(*scheduler(), AcquireQuotaForInteractiveFetch())
      .WillOnce(Return(true))
      .RetiresOnSaturation();
  provider->Fetch(
      articles_category(), /*known_suggestion_ids=*/std::set<std::string>(),
      /*fetch_done_callback=*/
      base::BindOnce(
          [](Status status_code,
             std::vector<ContentSuggestion> suggestions) -> void {}));

  ASSERT_TRUE(params.exclusive_category.has_value());
  EXPECT_EQ(*params.exclusive_category, articles_category());
  EXPECT_EQ(params.count_to_fetch, 35);
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldToggleStatusIfRefetchWhileDisplayingSucceeds) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  auto response_callback =
      RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  // The timeout does not fire earlier than it should.
  FastForwardBy(
      base::TimeDelta::FromSeconds(kTimeoutForRefetchWhileDisplayingSeconds) -
      base::TimeDelta::FromMilliseconds(1));

  // Before the results come, the status is AVAILABLE_LOADING.
  ASSERT_EQ(CategoryStatus::AVAILABLE_LOADING,
            observer().StatusForCategory(articles_category()));

  fetched_categories.push_back(articles_category_builder.Build());
  std::move(response_callback)
      .Run(Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();
  // After the results come, the status is flipped back to AVAILABLE.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldToggleStatusIfRefetchWhileDisplayingFails) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  auto response_callback =
      RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  // Before the results come, the status is flipped to AVAILABLE_LOADING.
  ASSERT_EQ(CategoryStatus::AVAILABLE_LOADING,
            observer().StatusForCategory(articles_category()));

  // After the results come, the status is flipped back to AVAILABLE.
  std::move(response_callback)
      .Run(Status(StatusCode::TEMPORARY_ERROR, "some error"), base::nullopt);
  // The category is available with the previous suggestion.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldToggleStatusIfRefetchWhileDisplayingTimeouts) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  // No need to finish the fetch, we ignore the response callback.
  RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  FastForwardBy(
      base::TimeDelta::FromSeconds(kTimeoutForRefetchWhileDisplayingSeconds) -
      base::TimeDelta::FromMilliseconds(1));

  // Before the timeout, the status is flipped to AVAILABLE_LOADING.
  ASSERT_EQ(CategoryStatus::AVAILABLE_LOADING,
            observer().StatusForCategory(articles_category()));

  FastForwardBy(base::TimeDelta::FromMilliseconds(2));

  // After the timeout, the status is flipped back to AVAILABLE, with the
  // previous suggestion.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldHandleCategoryDisabledBeforeTimeout) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  // No need to finish the fetch, we ignore the response callback.
  RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  FastForwardBy(
      base::TimeDelta::FromSeconds(kTimeoutForRefetchWhileDisplayingSeconds) -
      base::TimeDelta::FromMilliseconds(1));

  // Before the timeout, the status is flipped to AVAILABLE_LOADING.
  ASSERT_EQ(CategoryStatus::AVAILABLE_LOADING,
            observer().StatusForCategory(articles_category()));

  // Disable the provider; this will put the category into the
  // CATEGORY_EXPLICITLY_DISABLED status.
  provider->EnterState(RemoteSuggestionsProviderImpl::State::DISABLED);
  ASSERT_EQ(CategoryStatus::CATEGORY_EXPLICITLY_DISABLED,
            observer().StatusForCategory(articles_category()));

  // Trigger the timeout. The provider should gracefully handle(i.e. not crash
  // because of) the category being disabled in the interim.
  FastForwardBy(base::TimeDelta::FromMilliseconds(2));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldNotUpdateTimeoutIfRefetchWhileDisplayingCalledAgain) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  std::vector<FetchedCategory> fetched_categories;
  const FetchedCategoryBuilder articles_category_builder =
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(
              RemoteSuggestionBuilder().SetUrl("http://articles.com"));
  fetched_categories.push_back(articles_category_builder.Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));
  fetched_categories.clear();

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  // No need to finish the fetch, we ignore the response callback.
  RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  FastForwardBy(
      base::TimeDelta::FromSeconds(kTimeoutForRefetchWhileDisplayingSeconds) -
      base::TimeDelta::FromMilliseconds(1));

  // Another fetch does nothing to the deadline.
  RefetchWhileDisplayingAndGetResponseCallback(provider.get());

  FastForwardBy(base::TimeDelta::FromMilliseconds(2));

  // After the timeout, the status is flipped back to AVAILABLE, with the
  // previous suggestion.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldKeepPrefetchedSuggestionsAfterRefetchWhileDisplayingWhenEnabled) {
  EnableKeepingPrefetchedContentSuggestions(
      kMaxAdditionalPrefetchedSuggestions,
      kMaxAgeForAdditionalPrefetchedSuggestion);

  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/true,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);
  StrictMock<MockPrefetchedPagesTracker>* mock_tracker =
      mock_prefetched_pages_tracker();
  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  std::vector<FetchedCategory> fetched_categories;
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://prefetched.com")
                                       .SetUrl("http://prefetched.com")
                                       .SetAmpUrl("http://amp.prefetched.com"))
          .Build());
  FetchTheseSuggestions(provider.get(), /*interactive_request=*/true,
                        Status::Success(), std::move(fetched_categories));

  ASSERT_THAT(observer().SuggestionsForCategory(articles_category()),
              SizeIs(1));

  EXPECT_CALL(*mock_tracker, IsInitialized()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_tracker,
              PrefetchedOfflinePageExists(GURL("http://amp.prefetched.com")))
      .WillOnce(Return(true));
  fetched_categories.clear();
  fetched_categories.push_back(
      FetchedCategoryBuilder()
          .SetCategory(articles_category())
          .AddSuggestionViaBuilder(RemoteSuggestionBuilder()
                                       .AddId("http://other.com")
                                       .SetUrl("http://other.com")
                                       .SetAmpUrl("http://amp.other.com"))
          .Build());
  RefetchWhileDisplayingAndGetResponseCallback(provider.get())
      .Run(Status::Success(), std::move(fetched_categories));

  EXPECT_THAT(
      observer().SuggestionsForCategory(articles_category()),
      UnorderedElementsAre(
          Property(&ContentSuggestion::id,
                   MakeArticleID("http://prefetched.com")),
          Property(&ContentSuggestion::id, MakeArticleID("http://other.com"))));
}

TEST_F(RemoteSuggestionsProviderImplTest,
       ShouldToggleStatusIfReloadSuggestionsFails) {
  auto provider = MakeSuggestionsProvider(
      /*use_mock_prefetched_pages_tracker=*/false,
      /*use_fake_breaking_news_listener=*/false,
      /*use_mock_remote_suggestions_status_service=*/false);

  ASSERT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));

  auto response_callback =
      ReloadSuggestionsAndGetResponseCallback(provider.get());

  // Before the results come, the status is flipped to AVAILABLE_LOADING.
  ASSERT_EQ(CategoryStatus::AVAILABLE_LOADING,
            observer().StatusForCategory(articles_category()));

  // After the results come, the status is flipped back to AVAILABLE.
  std::move(response_callback)
      .Run(Status(StatusCode::TEMPORARY_ERROR, "some error"), base::nullopt);
  // The category is available, with no suggestions.
  EXPECT_EQ(CategoryStatus::AVAILABLE,
            observer().StatusForCategory(articles_category()));
  EXPECT_THAT(observer().SuggestionsForCategory(articles_category()),
              IsEmpty());
}

}  // namespace ntp_snippets
