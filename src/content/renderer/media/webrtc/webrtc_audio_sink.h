// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_WEBRTC_WEBRTC_AUDIO_SINK_H_
#define CONTENT_RENDERER_MEDIA_WEBRTC_WEBRTC_AUDIO_SINK_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "content/common/content_export.h"
#include "content/public/renderer/media_stream_audio_sink.h"
#include "content/renderer/media/stream/media_stream_audio_level_calculator.h"
#include "content/renderer/media/stream/media_stream_audio_processor.h"
#include "media/base/audio_parameters.h"
#include "media/base/audio_push_fifo.h"
#include "third_party/webrtc/api/mediastreaminterface.h"
#include "third_party/webrtc/pc/mediastreamtrack.h"

namespace content {

// Provides an implementation of the MediaStreamAudioSink which re-chunks audio
// data into the 10ms chunks required by WebRTC and then delivers the audio to
// one or more objects implementing the webrtc::AudioTrackSinkInterface.
//
// The inner class, Adapter, implements the webrtc::AudioTrackInterface and
// manages one or more "WebRTC sinks" (i.e., instances of
// webrtc::AudioTrackSinkInterface) which are added/removed on the WebRTC
// signaling thread.
class CONTENT_EXPORT WebRtcAudioSink : public MediaStreamAudioSink {
 public:
  WebRtcAudioSink(
      const std::string& label,
      scoped_refptr<webrtc::AudioSourceInterface> track_source,
      scoped_refptr<base::SingleThreadTaskRunner> signaling_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);

  ~WebRtcAudioSink() override;

  webrtc::AudioTrackInterface* webrtc_audio_track() const {
    return adapter_.get();
  }

  // Set the object that provides shared access to the current audio signal
  // level. This is passed via the Adapter to libjingle. This method may only
  // be called once, before the audio data flow starts, and before any calls to
  // Adapter::GetSignalLevel() might be made.
  void SetLevel(scoped_refptr<MediaStreamAudioLevelCalculator::Level> level);

  // Set the processor that applies signal processing on the data from the
  // source. This is passed via the Adapter to libjingle. This method may only
  // be called once, before the audio data flow starts, and before any calls to
  // GetAudioProcessor() might be made.
  void SetAudioProcessor(
      scoped_refptr<webrtc::AudioProcessorInterface> processor);

  // MediaStreamSink override.
  void OnEnabledChanged(bool enabled) override;

 private:
  // Private implementation of the webrtc::AudioTrackInterface whose control
  // methods are all called on the WebRTC signaling thread. This class is
  // ref-counted, per the requirements of webrtc::AudioTrackInterface.
  class Adapter : public webrtc::MediaStreamTrack<webrtc::AudioTrackInterface> {
   public:
    Adapter(const std::string& label,
            scoped_refptr<webrtc::AudioSourceInterface> source,
            scoped_refptr<base::SingleThreadTaskRunner> signaling_task_runner,
            scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);

    base::SingleThreadTaskRunner* signaling_task_runner() const {
      return signaling_task_runner_.get();
    }

    // These setters are called before the audio data flow starts, and before
    // any methods called on the signaling thread reference these objects.
    void set_processor(
        scoped_refptr<webrtc::AudioProcessorInterface> processor) {
      audio_processor_ = std::move(processor);
    }
    void set_level(
        scoped_refptr<MediaStreamAudioLevelCalculator::Level> level) {
      level_ = std::move(level);
    }

    // Delivers a 10ms chunk of audio to all WebRTC sinks managed by this
    // Adapter. This is called on the audio thread.
    void DeliverPCMToWebRtcSinks(const int16_t* audio_data,
                                 int sample_rate,
                                 size_t number_of_channels,
                                 size_t number_of_frames);

    // webrtc::MediaStreamTrack implementation.
    std::string kind() const override;
    bool set_enabled(bool enable) override;

    // webrtc::AudioTrackInterface implementation.
    void AddSink(webrtc::AudioTrackSinkInterface* sink) override;
    void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override;
    bool GetSignalLevel(int* level) override;
    rtc::scoped_refptr<webrtc::AudioProcessorInterface> GetAudioProcessor()
        override;
    webrtc::AudioSourceInterface* GetSource() const override;

   protected:
    ~Adapter() override;

   private:
    const scoped_refptr<webrtc::AudioSourceInterface> source_;

    // Task runner for operations that must be done on libjingle's signaling
    // thread.
    const scoped_refptr<base::SingleThreadTaskRunner> signaling_task_runner_;

    // Task runner used for the final de-referencing of |audio_processor_| at
    // destruction time.
    //
    // TODO(miu): Remove this once MediaStreamAudioProcessor is fixed.
    const scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

    // The audio processsor that applies audio post-processing on the source
    // audio. This is null if there is no audio processing taking place
    // upstream. This must be set before calls to GetAudioProcessor() are made.
    scoped_refptr<webrtc::AudioProcessorInterface> audio_processor_;

    // Thread-safe accessor to current audio signal level. This may be null, if
    // not applicable to the current use case. This must be set before calls to
    // GetSignalLevel() are made.
    scoped_refptr<MediaStreamAudioLevelCalculator::Level> level_;

    // Lock that protects concurrent access to the |sinks_| list.
    base::Lock lock_;

    // A vector of pointers to unowned WebRTC-internal objects which each
    // receive the audio data.
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;

    DISALLOW_COPY_AND_ASSIGN(Adapter);
  };

  // MediaStreamAudioSink implementation.
  void OnData(const media::AudioBus& audio_bus,
              base::TimeTicks estimated_capture_time) override;
  void OnSetFormat(const media::AudioParameters& params) override;

  // Called by AudioPushFifo zero or more times during the call to OnData().
  // Delivers audio data with the required 10ms buffer size to |adapter_|.
  void DeliverRebufferedAudio(const media::AudioBus& audio_bus,
                              int frame_delay);

  // Owner of the WebRTC sinks. May outlive this WebRtcAudioSink (if references
  // are held by libjingle).
  const scoped_refptr<Adapter> adapter_;

  // The current format of the audio passing through this sink.
  media::AudioParameters params_;

  // Light-weight fifo used for re-chunking audio into the 10ms chunks required
  // by the WebRTC sinks.
  media::AudioPushFifo fifo_;

  // Buffer used for converting into the required signed 16-bit integer
  // interleaved samples.
  std::unique_ptr<int16_t[]> interleaved_data_;

  // In debug builds, check that WebRtcAudioSink's public methods are all being
  // called on the main render thread.
  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(WebRtcAudioSink);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_WEBRTC_WEBRTC_AUDIO_SINK_H_
