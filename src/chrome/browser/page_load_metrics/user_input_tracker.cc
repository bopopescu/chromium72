// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/user_input_tracker.h"

#include <algorithm>

#include "third_party/blink/public/platform/web_input_event.h"

namespace page_load_metrics {

namespace {

// Blink's UserGestureIndicator allows events to be associated with gestures
// that are up to 1 second old, based on guidance in the HTML spec:
// https://html.spec.whatwg.org/multipage/interaction.html#triggered-by-user-activation.
const int kMaxEventAgeSeconds = 1;

// Allow for up to 2x the oldest time. This allows consumers to continue to
// find events for timestamps up to 1 second in the past.
const int kOldestAllowedEventAgeSeconds = kMaxEventAgeSeconds * 2;

// In order to limit to at most kMaxTrackedEvents, we rate limit the recorded
// events,
// allowing one per rate limit period.
constexpr int kRateLimitClampMillis = (kOldestAllowedEventAgeSeconds * 1000) /
                                      UserInputTracker::kMaxTrackedEvents;

bool IsInterestingInputEvent(const blink::WebInputEvent& event) {
  // Ignore synthesized auto repeat events.
  if (event.GetModifiers() & blink::WebInputEvent::kIsAutoRepeat)
    return false;

  switch (event.GetType()) {
    case blink::WebInputEvent::kMouseDown:
    case blink::WebInputEvent::kMouseUp:
    case blink::WebInputEvent::kRawKeyDown:
    case blink::WebInputEvent::kKeyDown:
    case blink::WebInputEvent::kChar:
    case blink::WebInputEvent::kTouchStart:
    case blink::WebInputEvent::kTouchEnd:
      return true;
    default:
      return false;
  }
}

}  // namespace

constexpr size_t UserInputTracker::kMaxTrackedEvents;

UserInputTracker::UserInputTracker() {
  sorted_event_times_.reserve(kMaxTrackedEvents);
}

UserInputTracker::~UserInputTracker() {}

// static
base::TimeTicks UserInputTracker::RoundToRateLimitedOffset(
    base::TimeTicks time) {
  base::TimeDelta time_as_delta = time - base::TimeTicks();
  base::TimeDelta rate_limit_remainder =
      time_as_delta % base::TimeDelta::FromMilliseconds(kRateLimitClampMillis);
  return time - rate_limit_remainder;
}

void UserInputTracker::OnInputEvent(const blink::WebInputEvent& event) {
  RemoveInputEventsUpToInclusive(base::TimeTicks::Now() -
                                 GetOldEventThreshold());

  if (!IsInterestingInputEvent(event))
    return;

  // TODO(bmcquade): ideally we'd limit tracking to events generated by a user
  // action, as opposed to those generated from JavaScript. The JS API isTrusted
  // can be used to distinguish these cases. isTrusted isn't yet a property of
  // WebInputEvent. We should consider adding it.

  const base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks time = RoundToRateLimitedOffset(event.TimeStamp());
  if (time <=
      std::max(most_recent_consumed_time_, now - GetOldEventThreshold()))
    return;

  if (time > now) {
    // We should never receive a UserInputEvent with a timestamp in the future
    // if we're on a platform with a high-res clock, where the monotonic clock
    // is system-wide monotonic. Unfortunately, this DCHECK seems to fire in
    // some linux unit tests, so it is disabled for the time being. See
    // crbug.com/678093 for more details.
    // DCHECK(!base::TimeTicks::IsHighResolution());
    return;
  }

  // lower_bound finds the first element >= |time|.
  auto it = std::lower_bound(sorted_event_times_.begin(),
                             sorted_event_times_.end(), time);
  if (it != sorted_event_times_.end() && *it == time) {
    // Don't insert duplicate values.
    return;
  }

  sorted_event_times_.insert(it, time);
  DCHECK_LE(sorted_event_times_.size(), kMaxTrackedEvents);
  DCHECK(
      std::is_sorted(sorted_event_times_.begin(), sorted_event_times_.end()));
}

bool UserInputTracker::FindAndConsumeInputEventsBefore(base::TimeTicks time) {
  base::TimeTicks recent_input_event_time =
      FindMostRecentUserInputEventBefore(time);

  if (recent_input_event_time.is_null())
    return false;

  RemoveInputEventsUpToInclusive(recent_input_event_time);
  return true;
}

base::TimeTicks UserInputTracker::FindMostRecentUserInputEventBefore(
    base::TimeTicks time) {
  RemoveInputEventsUpToInclusive(base::TimeTicks::Now() -
                                 GetOldEventThreshold());

  if (sorted_event_times_.empty())
    return base::TimeTicks();

  // lower_bound finds the first element >= |time|.
  auto it = std::lower_bound(sorted_event_times_.begin(),
                             sorted_event_times_.end(), time);

  // If all times are after the requested time, then we don't have a time to
  // return.
  if (it == sorted_event_times_.begin())
    return base::TimeTicks();

  // |it| points to the first event >= the specified time, so decrement once to
  // find the greatest event before the specified time.
  --it;
  base::TimeTicks candidate = *it;
  DCHECK_LT(candidate, time);

  // If the most recent event is too old, then don't return it.
  if (candidate < time - base::TimeDelta::FromSeconds(kMaxEventAgeSeconds))
    return base::TimeTicks();

  return candidate;
}

void UserInputTracker::RemoveInputEventsUpToInclusive(base::TimeTicks cutoff) {
  cutoff = std::max(RoundToRateLimitedOffset(cutoff),
                    base::TimeTicks::Now() - GetOldEventThreshold());
  most_recent_consumed_time_ = std::max(most_recent_consumed_time_, cutoff);
  sorted_event_times_.erase(
      sorted_event_times_.begin(),
      std::upper_bound(sorted_event_times_.begin(), sorted_event_times_.end(),
                       cutoff));
}

// static
base::TimeDelta UserInputTracker::GetOldEventThreshold() {
  return base::TimeDelta::FromSeconds(kOldestAllowedEventAgeSeconds);
}

}  // namespace page_load_metrics
