// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/screen_security/screen_security_notification_controller.h"

#include "ash/public/cpp/notification_utils.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_delegate.h"

using message_center::MessageCenter;
using message_center::Notification;

namespace ash {

// It is possible that we are capturing and sharing screen at the same time, so
// we cannot share the notification IDs for capturing and sharing.
const char kScreenCaptureNotificationId[] = "chrome://screen/capture";
const char kScreenShareNotificationId[] = "chrome://screen/share";
const char kNotifierScreenCapture[] = "ash.screen-capture";
const char kNotifierScreenShare[] = "ash.screen-share";

ScreenSecurityNotificationController::ScreenSecurityNotificationController() {
  Shell::Get()->AddShellObserver(this);
  Shell::Get()->system_tray_notifier()->AddScreenCaptureObserver(this);
  Shell::Get()->system_tray_notifier()->AddScreenShareObserver(this);
}

ScreenSecurityNotificationController::~ScreenSecurityNotificationController() {
  Shell::Get()->system_tray_notifier()->RemoveScreenShareObserver(this);
  Shell::Get()->system_tray_notifier()->RemoveScreenCaptureObserver(this);
  Shell::Get()->RemoveShellObserver(this);
}

void ScreenSecurityNotificationController::CreateNotification(
    const base::string16& message,
    bool is_capture) {
  message_center::RichNotificationData data;
  data.buttons.push_back(message_center::ButtonInfo(l10n_util::GetStringUTF16(
      is_capture ? IDS_ASH_STATUS_TRAY_SCREEN_CAPTURE_STOP
                 : IDS_ASH_STATUS_TRAY_SCREEN_SHARE_STOP)));

  auto delegate =
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          base::BindRepeating(
              [](base::WeakPtr<ScreenSecurityNotificationController> controller,
                 bool is_capture, base::Optional<int> button_index) {
                if (!button_index)
                  return;

                DCHECK_EQ(0, *button_index);

                controller->StopAllSessions(is_capture);
                if (is_capture) {
                  Shell::Get()->metrics()->RecordUserMetricsAction(
                      UMA_STATUS_AREA_SCREEN_CAPTURE_NOTIFICATION_STOP);
                }
              },
              weak_ptr_factory_.GetWeakPtr(), is_capture));

  std::unique_ptr<Notification> notification = ash::CreateSystemNotification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      is_capture ? kScreenCaptureNotificationId : kScreenShareNotificationId,
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_SCREEN_SHARE_TITLE),
      message, base::string16() /* display_source */, GURL(),
      message_center::NotifierId(
          message_center::NotifierType::SYSTEM_COMPONENT,
          is_capture ? kNotifierScreenCapture : kNotifierScreenShare),
      data, std::move(delegate), kNotificationScreenshareIcon,
      message_center::SystemNotificationWarningLevel::NORMAL);
  notification->SetSystemPriority();
  notification->set_pinned(true);
  message_center::MessageCenter::Get()->AddNotification(
      std::move(notification));
}

void ScreenSecurityNotificationController::StopAllSessions(bool is_capture) {
  message_center::MessageCenter::Get()->RemoveNotification(
      is_capture ? kScreenCaptureNotificationId : kScreenShareNotificationId,
      false /* by_user */);

  std::vector<base::OnceClosure> callbacks;
  std::swap(callbacks,
            is_capture ? capture_stop_callbacks_ : share_stop_callbacks_);
  for (base::OnceClosure& callback : callbacks) {
    if (callback)
      std::move(callback).Run();
  }
}

void ScreenSecurityNotificationController::OnScreenCaptureStart(
    const base::Closure& stop_callback,
    const base::string16& screen_capture_status) {
  capture_stop_callbacks_.emplace_back(std::move(stop_callback));

  // We do not want to show the screen capture notification and the chromecast
  // casting tray notification at the same time.
  //
  // This suppression technique is currently dependent on the order
  // that OnScreenCaptureStart and OnCastingSessionStartedOrStopped
  // get invoked. OnCastingSessionStartedOrStopped currently gets
  // called first.
  if (is_casting_)
    return;

  CreateNotification(screen_capture_status, true /* is_capture */);
}

void ScreenSecurityNotificationController::OnScreenCaptureStop() {
  StopAllSessions(true /* is_capture */);
}

void ScreenSecurityNotificationController::OnScreenShareStart(
    const base::Closure& stop_callback,
    const base::string16& helper_name) {
  share_stop_callbacks_.emplace_back(std::move(stop_callback));

  base::string16 help_label_text;
  if (!helper_name.empty()) {
    help_label_text = l10n_util::GetStringFUTF16(
        IDS_ASH_STATUS_TRAY_SCREEN_SHARE_BEING_HELPED_NAME, helper_name);
  } else {
    help_label_text = l10n_util::GetStringUTF16(
        IDS_ASH_STATUS_TRAY_SCREEN_SHARE_BEING_HELPED);
  }

  CreateNotification(help_label_text, false /* is_capture */);
}

void ScreenSecurityNotificationController::OnScreenShareStop() {
  StopAllSessions(false /* is_capture */);
}

void ScreenSecurityNotificationController::OnCastingSessionStartedOrStopped(
    bool started) {
  is_casting_ = started;
}

}  // namespace ash
