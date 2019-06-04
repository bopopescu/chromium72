// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/audio/loopback_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/unguessable_token.h"
#include "media/base/audio_parameters.h"
#include "media/base/audio_timestamp_helper.h"
#include "media/base/channel_layout.h"
#include "services/audio/loopback_coordinator.h"
#include "services/audio/loopback_group_member.h"
#include "services/audio/test/fake_consumer.h"
#include "services/audio/test/fake_loopback_group_member.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::StrictMock;

namespace audio {
namespace {

// Volume settings for the FakeLoopbackGroupMember (source) and LoopbackStream.
constexpr double kSnoopVolume = 0.25;
constexpr double kLoopbackVolume = 0.5;

// Piano key frequencies.
constexpr double kMiddleAFreq = 440;
constexpr double kMiddleCFreq = 261.626;

// Audio buffer duration.
constexpr base::TimeDelta kBufferDuration =
    base::TimeDelta::FromMilliseconds(10);

// Local audio output delay.
constexpr base::TimeDelta kDelayUntilOutput =
    base::TimeDelta::FromMilliseconds(20);

// The amount of audio signal to record each time PumpAudioAndTakeNewRecording()
// is called.
constexpr base::TimeDelta kTestRecordingDuration =
    base::TimeDelta::FromMilliseconds(250);

const media::AudioParameters& GetLoopbackStreamParams() {
  // 48 kHz, 2-channel audio, with 10 ms buffers.
  static const media::AudioParameters params(
      media::AudioParameters::AUDIO_PCM_LOW_LATENCY,
      media::CHANNEL_LAYOUT_STEREO, 48000, 480);
  return params;
}

class MockClientAndObserver : public media::mojom::AudioInputStreamClient,
                              public media::mojom::AudioInputStreamObserver {
 public:
  MockClientAndObserver() : client_binding_(this), observer_binding_(this) {}

  ~MockClientAndObserver() override = default;

  void Bind(media::mojom::AudioInputStreamClientRequest client_request,
            media::mojom::AudioInputStreamObserverRequest observer_request) {
    client_binding_.Bind(std::move(client_request));
    observer_binding_.Bind(std::move(observer_request));
  }

  void CloseClientBinding() { client_binding_.Close(); }
  void CloseObserverBinding() { observer_binding_.Close(); }

  MOCK_METHOD0(OnError, void());
  MOCK_METHOD0(DidStartRecording, void());
  void OnMutedStateChanged(bool) override { NOTREACHED(); }

 private:
  mojo::Binding<media::mojom::AudioInputStreamClient> client_binding_;
  mojo::Binding<media::mojom::AudioInputStreamObserver> observer_binding_;
};

// Subclass of FakeConsumer that adapts the SyncWriter interface to allow the
// tests to record and analyze the audio data from the LoopbackStream.
class FakeSyncWriter : public FakeConsumer, public InputController::SyncWriter {
 public:
  FakeSyncWriter(int channels, int sample_rate)
      : FakeConsumer(channels, sample_rate) {}

  ~FakeSyncWriter() override = default;

  void Clear() {
    FakeConsumer::Clear();
    last_capture_time_ = base::TimeTicks();
  }

  // media::AudioInputController::SyncWriter implementation.
  void Write(const media::AudioBus* data,
             double volume,
             bool key_pressed,
             base::TimeTicks capture_time) final {
    FakeConsumer::Consume(*data);

    // Capture times should be monotonically increasing.
    if (!last_capture_time_.is_null()) {
      CHECK_LT(last_capture_time_, capture_time);
    }
    last_capture_time_ = capture_time;
  }

  void Close() final {}

  base::TimeTicks last_capture_time_;
};

class LoopbackStreamTest : public testing::Test {
 public:
  LoopbackStreamTest()
      : task_runner_(base::MakeRefCounted<base::TestMockTimeTaskRunner>(
            base::Time(),
            // The starting TimeTicks value is "huge" to ensure time
            // calculations are being tested for overflow cases.
            base::TimeTicks() +
                base::TimeDelta::FromMicroseconds(INT64_C(1) << 62),
            base::TestMockTimeTaskRunner::Type::kBoundToThread)),
        group_id_(base::UnguessableToken::Create()) {}

  ~LoopbackStreamTest() override = default;

  void TearDown() override {
    stream_ = nullptr;

    for (const auto& source : sources_) {
      coordinator_.UnregisterMember(group_id_, source.get());
    }
    sources_.clear();

    task_runner_->FastForwardUntilNoTasksRemain();
  }

  MockClientAndObserver* client() { return &client_; }
  LoopbackStream* stream() { return stream_.get(); }
  FakeSyncWriter* consumer() { return consumer_; }

  void RunMojoTasks() { task_runner_->RunUntilIdle(); }

  FakeLoopbackGroupMember* AddSource(int channels, int sample_rate) {
    sources_.emplace_back(std::make_unique<FakeLoopbackGroupMember>(
        media::AudioParameters(media::AudioParameters::AUDIO_PCM_LOW_LATENCY,
                               media::GuessChannelLayout(channels), sample_rate,
                               (sample_rate * kBufferDuration) /
                                   base::TimeDelta::FromSeconds(1))));
    coordinator_.RegisterMember(group_id_, sources_.back().get());
    return sources_.back().get();
  }

  void RemoveSource(FakeLoopbackGroupMember* source) {
    const auto it = std::find_if(sources_.begin(), sources_.end(),
                                 base::MatchesUniquePtr(source));
    if (it != sources_.end()) {
      coordinator_.UnregisterMember(group_id_, source);
      sources_.erase(it);
    }
  }

  void CreateLoopbackStream() {
    CHECK(!stream_);

    media::mojom::AudioInputStreamClientPtr client_ptr;
    media::mojom::AudioInputStreamObserverPtr observer_ptr;
    client_.Bind(mojo::MakeRequest(&client_ptr),
                 mojo::MakeRequest(&observer_ptr));

    stream_ = std::make_unique<LoopbackStream>(
        base::BindOnce([](media::mojom::ReadOnlyAudioDataPipePtr pipe) {
          EXPECT_TRUE(pipe->shared_memory.IsValid());
          EXPECT_TRUE(pipe->socket.is_valid());
        }),
        base::BindOnce([](LoopbackStreamTest* self,
                          LoopbackStream* stream) { self->stream_ = nullptr; },
                       this),
        task_runner_, mojo::MakeRequest(&input_stream_ptr_),
        std::move(client_ptr), std::move(observer_ptr),
        GetLoopbackStreamParams(),
        // The following argument is the |shared_memory_count|, which does not
        // matter because the SyncWriter will be overridden with FakeSyncWriter
        // below.
        1, &coordinator_, group_id_);

    // Override the clock used by the LoopbackStream so that everything is
    // single-threaded and synchronized with the driving code in these tests.
    stream_->set_clock_for_testing(task_runner_->GetMockTickClock());

    // Redirect the output of the LoopbackStream to a FakeSyncWriter.
    // LoopbackStream takes ownership of the FakeSyncWriter.
    auto consumer = std::make_unique<FakeSyncWriter>(
        GetLoopbackStreamParams().channels(),
        GetLoopbackStreamParams().sample_rate());
    CHECK(!consumer_);
    consumer_ = consumer.get();
    stream_->set_sync_writer_for_testing(std::move(consumer));

    // Set the volume for the LoopbackStream.
    input_stream_ptr_->SetVolume(kLoopbackVolume);

    // Allow all pending mojo tasks for all of the above to run and propagate
    // state.
    RunMojoTasks();

    ASSERT_TRUE(input_stream_ptr_);
  }

  void StartLoopbackRecording() {
    ASSERT_EQ(0, consumer_->GetRecordedFrameCount());
    input_stream_ptr_->Record();
    RunMojoTasks();
  }

  void SetLoopbackVolume(double volume) {
    input_stream_ptr_->SetVolume(volume);
    RunMojoTasks();
  }

  void PumpAudioAndTakeNewRecording() {
    consumer_->Clear();

    const int min_frames_to_record = media::AudioTimestampHelper::TimeToFrames(
        kTestRecordingDuration, GetLoopbackStreamParams().sample_rate());
    do {
      // Render audio meant for local output at some point in the near
      // future.
      const base::TimeTicks output_timestamp =
          task_runner_->NowTicks() + kDelayUntilOutput;
      for (const auto& source : sources_) {
        source->RenderMoreAudio(output_timestamp);
      }

      // Move the task runner forward, which will cause the FlowNetwork's
      // delayed tasks to run, which will generate output for the consumer.
      task_runner_->FastForwardBy(kBufferDuration);
    } while (consumer_->GetRecordedFrameCount() < min_frames_to_record);
  }

  void CloseInputStreamPtr() {
    input_stream_ptr_.reset();
    RunMojoTasks();
  }

 private:
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_;
  LoopbackCoordinator coordinator_;
  const base::UnguessableToken group_id_;
  std::vector<std::unique_ptr<FakeLoopbackGroupMember>> sources_;
  NiceMock<MockClientAndObserver> client_;
  std::unique_ptr<LoopbackStream> stream_;
  FakeSyncWriter* consumer_ = nullptr;  // Owned by |stream_|.

  media::mojom::AudioInputStreamPtr input_stream_ptr_;

  DISALLOW_COPY_AND_ASSIGN(LoopbackStreamTest);
};

TEST_F(LoopbackStreamTest, ShutsDownStreamWhenInterfacePtrIsClosed) {
  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();
  EXPECT_CALL(*client(), OnError());
  CloseInputStreamPtr();
  EXPECT_FALSE(stream());
  Mock::VerifyAndClearExpectations(client());
}

TEST_F(LoopbackStreamTest, ShutsDownStreamWhenClientBindingIsClosed) {
  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();
  // Note: Expect no call to client::OnError() because it is the client binding
  // that is being closed and causing the error.
  EXPECT_CALL(*client(), OnError()).Times(0);
  client()->CloseClientBinding();
  RunMojoTasks();
  EXPECT_FALSE(stream());
  Mock::VerifyAndClearExpectations(client());
}

TEST_F(LoopbackStreamTest, ShutsDownStreamWhenObserverBindingIsClosed) {
  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();
  EXPECT_CALL(*client(), OnError());
  client()->CloseObserverBinding();
  RunMojoTasks();
  EXPECT_FALSE(stream());
  Mock::VerifyAndClearExpectations(client());
}

TEST_F(LoopbackStreamTest, ProducesSilenceWhenNoMembersArePresent) {
  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();
  for (int ch = 0; ch < GetLoopbackStreamParams().channels(); ++ch) {
    SCOPED_TRACE(testing::Message() << "ch=" << ch);
    EXPECT_TRUE(consumer()->IsSilent(ch));
  }
}

// Syntatic sugar to confirm a tone exists and its amplitude matches
// expectations.
#define EXPECT_TONE(ch, frequency, expected_amplitude)                     \
  {                                                                        \
    SCOPED_TRACE(testing::Message() << "ch=" << ch);                       \
    const double amplitude = consumer()->ComputeAmplitudeAt(               \
        ch, frequency, consumer()->GetRecordedFrameCount());               \
    VLOG(1) << "For ch=" << ch << ", amplitude at frequency=" << frequency \
            << " is " << amplitude;                                        \
    EXPECT_NEAR(expected_amplitude, amplitude, 0.01);                      \
  }

TEST_F(LoopbackStreamTest, ProducesAudioFromASingleSource) {
  FakeLoopbackGroupMember* const source =
      AddSource(1, 48000);  // Monaural, 48 kHz.
  source->SetChannelTone(0, kMiddleAFreq);
  source->SetVolume(kSnoopVolume);

  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();

  // Expect to have recorded middle-A in all of the loopback stream's channels.
  for (int ch = 0; ch < GetLoopbackStreamParams().channels(); ++ch) {
    EXPECT_TONE(ch, kMiddleAFreq, kSnoopVolume * kLoopbackVolume);
  }
}

TEST_F(LoopbackStreamTest, ProducesAudioFromTwoSources) {
  // Start the first source (of a middle-A note) before creating the loopback
  // stream.
  const int channels = GetLoopbackStreamParams().channels();
  FakeLoopbackGroupMember* const source1 = AddSource(channels, 48000);
  source1->SetChannelTone(0, kMiddleAFreq);
  source1->SetVolume(kSnoopVolume);

  CreateLoopbackStream();
  EXPECT_CALL(*client(), DidStartRecording());
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();

  // Start the second source (of a middle-C note) while the loopback stream is
  // running. The second source has a different sample rate than the first.
  FakeLoopbackGroupMember* const source2 = AddSource(channels, 44100);
  source2->SetChannelTone(1, kMiddleCFreq);
  source2->SetVolume(kSnoopVolume);

  PumpAudioAndTakeNewRecording();

  // Expect to have recorded both middle-A and middle-C in all of the loopback
  // stream's channels.
  EXPECT_TONE(0, kMiddleAFreq, kSnoopVolume * kLoopbackVolume);
  EXPECT_TONE(1, kMiddleCFreq, kSnoopVolume * kLoopbackVolume);

  // Switch the channels containig the tone in both sources, and expect to see
  // the tones have switched channels in the loopback output.
  source1->SetChannelTone(0, 0.0);
  source1->SetChannelTone(1, kMiddleAFreq);
  source2->SetChannelTone(0, kMiddleCFreq);
  source2->SetChannelTone(1, 0.0);
  PumpAudioAndTakeNewRecording();
  EXPECT_TONE(1, kMiddleAFreq, kSnoopVolume * kLoopbackVolume);
  EXPECT_TONE(0, kMiddleCFreq, kSnoopVolume * kLoopbackVolume);
}

TEST_F(LoopbackStreamTest, AudioChangesVolume) {
  FakeLoopbackGroupMember* const source =
      AddSource(1, 48000);  // Monaural, 48 kHz.
  source->SetChannelTone(0, kMiddleAFreq);
  source->SetVolume(kSnoopVolume);

  CreateLoopbackStream();
  StartLoopbackRecording();
  PumpAudioAndTakeNewRecording();

  // Record and check the amplitude at the default volume settings.
  double expected_amplitude = kSnoopVolume * kLoopbackVolume;
  for (int ch = 0; ch < GetLoopbackStreamParams().channels(); ++ch) {
    EXPECT_TONE(ch, kMiddleAFreq, expected_amplitude);
  }

  // Double the volume of the source and expect the output to have also doubled.
  source->SetVolume(kSnoopVolume * 2);
  PumpAudioAndTakeNewRecording();
  expected_amplitude *= 2;
  for (int ch = 0; ch < GetLoopbackStreamParams().channels(); ++ch) {
    EXPECT_TONE(ch, kMiddleAFreq, expected_amplitude);
  }

  // Drop the LoopbackStream volume by 1/3 and expect the output to also have
  // dropped by 1/3.
  SetLoopbackVolume(kLoopbackVolume / 3);
  PumpAudioAndTakeNewRecording();
  expected_amplitude /= 3;
  for (int ch = 0; ch < GetLoopbackStreamParams().channels(); ++ch) {
    EXPECT_TONE(ch, kMiddleAFreq, expected_amplitude);
  }
}

}  // namespace
}  // namespace audio
