// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/activation_state_computing_navigation_throttle.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter_test_utils.h"
#include "components/subresource_filter/core/common/scoped_timers.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "components/subresource_filter/mojom/subresource_filter.mojom.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace subresource_filter {

namespace {

// Histogram name on thread timers. Please, use |ExpectThreadTimers| for
// expectation calls corrections.
constexpr char kActivationCPU[] =
    "SubresourceFilter.DocumentLoad.Activation.CPUDuration";

int ExpectThreadTimers(int expected) {
  return ScopedThreadTimers::IsSupported() ? expected : 0;
}

}  // namespace

namespace proto = url_pattern_index::proto;

// The tests are parameterized by a bool which enables speculative main frame
// activation computation in DRYRUN mode. In practice, this will correspond to
// the kAdTagging feature.
class ActivationStateComputingNavigationThrottleTest
    : public content::RenderViewHostTestHarness,
      public content::WebContentsObserver,
      public ::testing::WithParamInterface<bool> {
 public:
  ActivationStateComputingNavigationThrottleTest()
      : simple_task_runner_(base::MakeRefCounted<base::TestSimpleTaskRunner>()),
        dryrun_speculation_(GetParam()) {}
  ~ActivationStateComputingNavigationThrottleTest() override {}

  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://example.first"));
    InitializeRuleset();
    Observe(RenderViewHostTestHarness::web_contents());
  }

  void TearDown() override {
    ruleset_handle_.reset();
    dealer_handle_.reset();

    // The various ruleset classes post tasks to delete their blocking task
    // runners. Make sure that happens now to avoid test-only leaks.
    base::RunLoop().RunUntilIdle();
    simple_task_runner()->RunUntilIdle();
    content::RenderViewHostTestHarness::TearDown();
  }

  void InitializeRuleset() {
    std::vector<proto::UrlRule> rules;
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelisted.com", proto::ACTIVATION_TYPE_DOCUMENT,
        {"allow-child-to-be-whitelisted.com",
         "whitelisted-generic-with-disabled-child.com"}));

    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelisted-generic.com", proto::ACTIVATION_TYPE_GENERICBLOCK,
        {"allow-child-to-be-whitelisted.com"}));

    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelisted-generic-with-disabled-child.com",
        proto::ACTIVATION_TYPE_GENERICBLOCK,
        {"allow-child-to-be-whitelisted.com"}));

    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelisted-always.com", proto::ACTIVATION_TYPE_DOCUMENT));

    ASSERT_NO_FATAL_FAILURE(test_ruleset_creator_.CreateRulesetWithRules(
        rules, &test_ruleset_pair_));

    // Make the blocking task runner run on the current task runner for the
    // tests, to ensure that the NavigationSimulator properly runs all necessary
    // tasks while waiting for throttle checks to finish.
    InitializeRulesetHandles(base::SequencedTaskRunnerHandle::Get());
  }

  void NavigateAndCommitMainFrameWithPageActivationState(
      const GURL& document_url,
      const mojom::ActivationLevel& level) {
    mojom::ActivationState state;
    state.activation_level = level;
    CreateTestNavigationForMainFrame(document_url);
    SimulateStartAndExpectToProceed();

    NotifyPageActivation(state);
    SimulateCommitAndExpectToProceed();
  }

  void CreateTestNavigationForMainFrame(const GURL& first_url) {
    navigation_simulator_ =
        content::NavigationSimulator::CreateRendererInitiated(first_url,
                                                              main_rfh());
  }

  void CreateSubframeAndInitTestNavigation(
      const GURL& first_url,
      content::RenderFrameHost* parent,
      const mojom::ActivationState& parent_activation_state) {
    ASSERT_TRUE(parent);
    parent_activation_state_ = parent_activation_state;
    content::RenderFrameHost* navigating_subframe =
        content::RenderFrameHostTester::For(parent)->AppendChild("subframe");
    navigation_simulator_ =
        content::NavigationSimulator::CreateRendererInitiated(
            first_url, navigating_subframe);
  }

  void SimulateStartAndExpectToProceed() {
    ASSERT_TRUE(navigation_simulator_);
    navigation_simulator_->Start();
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator_->GetLastThrottleCheckResult());
  }

  void SimulateRedirectAndExpectToProceed(const GURL& new_url) {
    navigation_simulator_->Redirect(new_url);
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator_->GetLastThrottleCheckResult());
  }

  void SimulateCommitAndExpectToProceed() {
    navigation_simulator_->Commit();
    EXPECT_EQ(content::NavigationThrottle::PROCEED,
              navigation_simulator_->GetLastThrottleCheckResult());
  }

  void InitializeRulesetHandles(
      scoped_refptr<base::SequencedTaskRunner> ruleset_task_runner) {
    dealer_handle_ = std::make_unique<VerifiedRulesetDealer::Handle>(
        std::move(ruleset_task_runner));
    dealer_handle_->TryOpenAndSetRulesetFile(test_ruleset_pair_.indexed.path,
                                             /*expected_checksum=*/0,
                                             base::DoNothing());
    ruleset_handle_ =
        std::make_unique<VerifiedRuleset::Handle>(dealer_handle_.get());
  }

  void NotifyPageActivation(mojom::ActivationState state) {
    test_throttle_->NotifyPageActivationWithRuleset(ruleset_handle_.get(),
                                                    state);
  }

  mojom::ActivationState last_activation_state() {
    EXPECT_TRUE(last_activation_state_.has_value());
    return last_activation_state_.value_or(mojom::ActivationState());
  }

  content::RenderFrameHost* last_committed_frame_host() {
    return last_committed_frame_host_;
  }

  scoped_refptr<base::TestSimpleTaskRunner> simple_task_runner() {
    return simple_task_runner_;
  }

  void set_parent_activation_state(const mojom::ActivationState& state) {
    parent_activation_state_ = state;
  }

 protected:
  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    std::unique_ptr<ActivationStateComputingNavigationThrottle> throttle =
        navigation_handle->IsInMainFrame()
            ? ActivationStateComputingNavigationThrottle::CreateForMainFrame(
                  navigation_handle)
            : ActivationStateComputingNavigationThrottle::CreateForSubframe(
                  navigation_handle, ruleset_handle_.get(),
                  parent_activation_state_.value());
    if (navigation_handle->IsInMainFrame() && dryrun_speculation_) {
      mojom::ActivationState dryrun_state;
      dryrun_state.activation_level = mojom::ActivationLevel::kDryRun;
      throttle->NotifyPageActivationWithRuleset(ruleset_handle_.get(),
                                                dryrun_state);
    }
    test_throttle_ = throttle.get();
    navigation_handle->RegisterThrottleForTesting(std::move(throttle));
  }

  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!test_throttle_)
      return;
    ASSERT_EQ(navigation_handle, test_throttle_->navigation_handle());
    if (test_throttle_->filter())
      test_throttle_->WillSendActivationToRenderer();

    if (auto filter = test_throttle_->ReleaseFilter()) {
      EXPECT_NE(mojom::ActivationLevel::kDisabled,
                filter->activation_state().activation_level);
      last_activation_state_ = filter->activation_state();
    } else {
      last_activation_state_ = mojom::ActivationState();
    }
  }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!test_throttle_)
      return;
    last_committed_frame_host_ = navigation_handle->GetRenderFrameHost();
    test_throttle_ = nullptr;
  }

  bool dryrun_speculation() const { return dryrun_speculation_; }

 private:
  testing::TestRulesetCreator test_ruleset_creator_;
  testing::TestRulesetPair test_ruleset_pair_;

  std::unique_ptr<VerifiedRulesetDealer::Handle> dealer_handle_;
  std::unique_ptr<VerifiedRuleset::Handle> ruleset_handle_;

  std::unique_ptr<content::NavigationSimulator> navigation_simulator_;

  scoped_refptr<base::TestSimpleTaskRunner> simple_task_runner_;

  // Owned by the current navigation.
  ActivationStateComputingNavigationThrottle* test_throttle_;
  base::Optional<mojom::ActivationState> last_activation_state_;
  base::Optional<mojom::ActivationState> parent_activation_state_;

  // Needed for potential cross process navigations which swap hosts.
  content::RenderFrameHost* last_committed_frame_host_ = nullptr;

  bool dryrun_speculation_;

  DISALLOW_COPY_AND_ASSIGN(ActivationStateComputingNavigationThrottleTest);
};

typedef ActivationStateComputingNavigationThrottleTest
    ActivationStateComputingThrottleMainFrameTest;
typedef ActivationStateComputingNavigationThrottleTest
    ActivationStateComputingThrottleSubFrameTest;

TEST_P(ActivationStateComputingThrottleMainFrameTest, Activate) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://example.test/"), mojom::ActivationLevel::kEnabled);
  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_FALSE(state.filtering_disabled_for_document);
}

TEST_P(ActivationStateComputingThrottleMainFrameTest,
       NoPageActivationNotification_NoActivation) {
  CreateTestNavigationForMainFrame(GURL("http://example.test/"));
  SimulateStartAndExpectToProceed();
  SimulateRedirectAndExpectToProceed(GURL("http://example.test/?v=1"));

  // Never send NotifyPageActivation.
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kDisabled, state.activation_level);
}

TEST_P(ActivationStateComputingThrottleMainFrameTest,
       WhitelistDoesNotApply_CausesActivation) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://allow-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://whitelisted.com/"), mojom::ActivationLevel::kEnabled);

  mojom::ActivationState state = last_activation_state();
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
}

TEST_P(ActivationStateComputingThrottleMainFrameTest,
       Whitelisted_DisablesFiltering) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://whitelisted-always.com/"), mojom::ActivationLevel::kEnabled);

  mojom::ActivationState state = last_activation_state();
  EXPECT_TRUE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, Activate) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://example.test/"), mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(GURL("http://example.child/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateRedirectAndExpectToProceed(GURL("http://example.child/?v=1"));
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest,
       WhitelistDoesNotApply_CausesActivation) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://disallows-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(GURL("http://whitelisted.com/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest,
       Whitelisted_DisableDocumentFiltering) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://allow-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(GURL("http://whitelisted.com/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_TRUE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest,
       Whitelisted_DisablesGenericRules) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://allow-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(GURL("http://whitelisted-generic.com/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_TRUE(state.generic_blocking_rules_disabled);
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, DryRunIsPropagated) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://example.test/"), mojom::ActivationLevel::kDryRun);
  EXPECT_EQ(mojom::ActivationLevel::kDryRun,
            last_activation_state().activation_level);

  CreateSubframeAndInitTestNavigation(GURL("http://example.child/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateRedirectAndExpectToProceed(GURL("http://example.child/?v=1"));
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kDryRun, state.activation_level);
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest,
       DryRunWithLoggingIsPropagated) {
  mojom::ActivationState page_state;
  page_state.activation_level = mojom::ActivationLevel::kDryRun;
  page_state.enable_logging = true;

  CreateTestNavigationForMainFrame(GURL("http://example.test/"));
  SimulateStartAndExpectToProceed();

  NotifyPageActivation(page_state);
  SimulateCommitAndExpectToProceed();

  EXPECT_EQ(mojom::ActivationLevel::kDryRun,
            last_activation_state().activation_level);

  CreateSubframeAndInitTestNavigation(GURL("http://example.child/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateRedirectAndExpectToProceed(GURL("http://example.child/?v=1"));
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kDryRun, state.activation_level);
  EXPECT_TRUE(state.enable_logging);
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, DisabledStatePropagated) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://allow-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(GURL("http://whitelisted.com"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  CreateSubframeAndInitTestNavigation(GURL("http://example.test/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_TRUE(state.filtering_disabled_for_document);
  EXPECT_FALSE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, DisabledStatePropagated2) {
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://allow-child-to-be-whitelisted.com/"),
      mojom::ActivationLevel::kEnabled);

  CreateSubframeAndInitTestNavigation(
      GURL("http://whitelisted-generic-with-disabled-child.com/"),
      last_committed_frame_host(), last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  mojom::ActivationState state = last_activation_state();
  EXPECT_FALSE(state.filtering_disabled_for_document);
  EXPECT_TRUE(state.generic_blocking_rules_disabled);

  CreateSubframeAndInitTestNavigation(GURL("http://whitelisted.com/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_TRUE(state.filtering_disabled_for_document);
  EXPECT_TRUE(state.generic_blocking_rules_disabled);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, DelayMetrics) {
  base::HistogramTester histogram_tester;
  NavigateAndCommitMainFrameWithPageActivationState(
      GURL("http://example.test/"), mojom::ActivationLevel::kEnabled);
  mojom::ActivationState state = last_activation_state();
  EXPECT_EQ(mojom::ActivationLevel::kEnabled, state.activation_level);
  EXPECT_FALSE(state.filtering_disabled_for_document);

  const char kActivationDelay[] =
      "SubresourceFilter.DocumentLoad.ActivationComputingDelay";
  const char kActivationDelayMainFrame[] =
      "SubresourceFilter.DocumentLoad.ActivationComputingDelay.MainFrame";
  histogram_tester.ExpectTotalCount(kActivationDelay, 1);
  histogram_tester.ExpectTotalCount(kActivationDelayMainFrame, 1);

  // Subframe activation should not log main frame metrics.
  CreateSubframeAndInitTestNavigation(GURL("http://example.test/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();
  histogram_tester.ExpectTotalCount(kActivationDelay, 2);
  histogram_tester.ExpectTotalCount(kActivationDelayMainFrame, 1);

  // No page activation should imply no delay.
  CreateTestNavigationForMainFrame(GURL("http://example.test2/"));
  SimulateStartAndExpectToProceed();
  SimulateCommitAndExpectToProceed();

  state = last_activation_state();
  EXPECT_EQ(dryrun_speculation() ? mojom::ActivationLevel::kDryRun
                                 : mojom::ActivationLevel::kDisabled,
            state.activation_level);
  int extra_activation = dryrun_speculation() ? 1 : 0;
  histogram_tester.ExpectTotalCount(kActivationDelay, 2 + extra_activation);
  histogram_tester.ExpectTotalCount(kActivationDelayMainFrame,
                                    1 + extra_activation);
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, Speculation) {
  // Use the activation performance metric as a proxy for how many times
  // activation computation occurred.
  base::HistogramTester main_histogram_tester;

  // Main frames don't do speculative lookups, a navigation commit should only
  // trigger a single ruleset lookup.
  CreateTestNavigationForMainFrame(GURL("http://example.test/"));
  SimulateStartAndExpectToProceed();
  base::RunLoop().RunUntilIdle();
  int main_frame_checks = dryrun_speculation() ? 1 : 0;
  main_histogram_tester.ExpectTotalCount(kActivationCPU,
                                         ExpectThreadTimers(main_frame_checks));

  SimulateRedirectAndExpectToProceed(GURL("http://example.test2/"));
  base::RunLoop().RunUntilIdle();
  main_frame_checks += dryrun_speculation() ? 1 : 0;
  main_histogram_tester.ExpectTotalCount(kActivationCPU,
                                         ExpectThreadTimers(main_frame_checks));

  mojom::ActivationState state;
  state.activation_level = mojom::ActivationLevel::kEnabled;
  NotifyPageActivation(state);
  SimulateCommitAndExpectToProceed();
  main_frame_checks += dryrun_speculation() ? 0 : 1;
  main_histogram_tester.ExpectTotalCount(kActivationCPU,
                                         ExpectThreadTimers(main_frame_checks));

  base::HistogramTester sub_histogram_tester;
  CreateSubframeAndInitTestNavigation(GURL("http://example.test/"),
                                      last_committed_frame_host(),
                                      last_activation_state());
  // For subframes, do a ruleset lookup at the start and every redirect.
  SimulateStartAndExpectToProceed();
  base::RunLoop().RunUntilIdle();
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, ExpectThreadTimers(1));

  SimulateRedirectAndExpectToProceed(GURL("http://example.test2/"));
  base::RunLoop().RunUntilIdle();
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, ExpectThreadTimers(2));

  // No ruleset lookup required at commit because we've already checked the
  // latest URL.
  SimulateCommitAndExpectToProceed();
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, ExpectThreadTimers(2));
}

TEST_P(ActivationStateComputingThrottleSubFrameTest, SpeculationWithDelay) {
  InitializeRulesetHandles(simple_task_runner());

  // Use the activation performance metric as a proxy for how many times
  // activation computation occurred.
  base::HistogramTester main_histogram_tester;

  // Main frames will do speculative lookup only in some cases.
  auto simulator = content::NavigationSimulator::CreateRendererInitiated(
      GURL("http://example.test/"), main_rfh());
  simulator->SetAutoAdvance(false);

  simulator->Start();
  EXPECT_FALSE(simulator->IsDeferred());
  main_histogram_tester.ExpectTotalCount(kActivationCPU, 0);

  simulator->Redirect(GURL("http://example.test2/"));
  EXPECT_FALSE(simulator->IsDeferred());
  main_histogram_tester.ExpectTotalCount(kActivationCPU, 0);

  mojom::ActivationState state;
  state.activation_level = mojom::ActivationLevel::kEnabled;
  NotifyPageActivation(state);

  simulator->ReadyToCommit();
  EXPECT_TRUE(simulator->IsDeferred());
  EXPECT_LT(0u, simple_task_runner()->NumPendingTasks());
  simple_task_runner()->RunPendingTasks();
  // If speculation was enabled for this test, will do a lookup at start and
  // redirect.
  main_histogram_tester.ExpectTotalCount(
      kActivationCPU, ExpectThreadTimers(dryrun_speculation() ? 2 : 1));
  simulator->Wait();
  EXPECT_FALSE(simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            simulator->GetLastThrottleCheckResult());
  simulator->Commit();

  base::HistogramTester sub_histogram_tester;
  auto subframe_simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://example.test"),
          content::RenderFrameHostTester::For(main_rfh())
              ->AppendChild("subframe"));
  subframe_simulator->SetAutoAdvance(false);
  set_parent_activation_state(last_activation_state());

  // Simulate slow ruleset checks for the subframe, these should not delay the
  // navigation until commit time.
  subframe_simulator->Start();
  EXPECT_FALSE(subframe_simulator->IsDeferred());
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, 0);

  // Calling redirect should ensure that the throttle does not receive the
  // results of the check, but the task to actually perform the check will still
  // happen.
  subframe_simulator->Redirect(GURL("http://example.test2/"));
  EXPECT_FALSE(subframe_simulator->IsDeferred());
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, 0);

  // Finish the checks dispatched in the start and redirect phase when the
  // navigation is ready to commit.
  subframe_simulator->ReadyToCommit();
  EXPECT_TRUE(subframe_simulator->IsDeferred());
  EXPECT_LT(0u, simple_task_runner()->NumPendingTasks());
  simple_task_runner()->RunPendingTasks();
  subframe_simulator->Wait();
  EXPECT_FALSE(subframe_simulator->IsDeferred());
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            simulator->GetLastThrottleCheckResult());
  sub_histogram_tester.ExpectTotalCount(kActivationCPU, ExpectThreadTimers(2));
}

INSTANTIATE_TEST_CASE_P(,
                        ActivationStateComputingNavigationThrottleTest,
                        ::testing::Values(true, false));
INSTANTIATE_TEST_CASE_P(,
                        ActivationStateComputingThrottleSubFrameTest,
                        ::testing::Values(true, false));

}  // namespace subresource_filter
