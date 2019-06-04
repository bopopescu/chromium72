// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/keyboard/test/keyboard_test_util.h"

#include "base/run_loop.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "ui/display/screen.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_controller_observer.h"

namespace keyboard {

namespace {

class KeyboardVisibilityChangeWaiter : public KeyboardControllerObserver {
 public:
  explicit KeyboardVisibilityChangeWaiter(bool wait_until)
      : wait_until_(wait_until) {
    KeyboardController::Get()->AddObserver(this);
  }
  ~KeyboardVisibilityChangeWaiter() override {
    KeyboardController::Get()->RemoveObserver(this);
  }

  void Wait() { run_loop_.Run(); }

 private:
  void OnKeyboardVisibilityStateChanged(const bool is_visible) override {
    if (is_visible == wait_until_)
      run_loop_.QuitWhenIdle();
  }

  base::RunLoop run_loop_;
  const bool wait_until_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardVisibilityChangeWaiter);
};

bool WaitVisibilityChangesTo(bool wait_until) {
  if (KeyboardController::Get()->IsKeyboardVisible() == wait_until)
    return true;
  KeyboardVisibilityChangeWaiter waiter(wait_until);
  waiter.Wait();
  return true;
}

}  // namespace

bool WaitUntilShown() {
  // KeyboardController send a visibility update once the show animation
  // finishes.
  return WaitVisibilityChangesTo(true /* wait_until */);
}

bool WaitUntilHidden() {
  // Unlike |WaitUntilShown|, KeyboardController updates its visibility
  // at the beginning of the hide animation. There's currently no way to
  // actually detect when the hide animation finishes.
  // TODO(https://crbug.com/849995): Find a proper solution to this.
  return WaitVisibilityChangesTo(false /* wait_until */);
}

bool IsKeyboardShowing() {
  auto* keyboard_controller = KeyboardController::Get();
  DCHECK(keyboard_controller->IsEnabled());

  // KeyboardController sets its state to SHOWN when it is about to show.
  return keyboard_controller->GetStateForTest() ==
         KeyboardControllerState::SHOWN;
}

bool IsKeyboardHiding() {
  auto* keyboard_controller = KeyboardController::Get();
  DCHECK(keyboard_controller->IsEnabled());

  return keyboard_controller->GetStateForTest() ==
             KeyboardControllerState::WILL_HIDE ||
         keyboard_controller->GetStateForTest() ==
             KeyboardControllerState::HIDDEN;
}

gfx::Rect KeyboardBoundsFromRootBounds(const gfx::Rect& root_bounds,
                                       int keyboard_height) {
  return gfx::Rect(root_bounds.x(), root_bounds.bottom() - keyboard_height,
                   root_bounds.width(), keyboard_height);
}

TestKeyboardUI::TestKeyboardUI(ui::InputMethod* input_method)
    : input_method_(input_method) {}

TestKeyboardUI::~TestKeyboardUI() {
  // Destroy the window before the delegate.
  window_.reset();
}

aura::Window* TestKeyboardUI::LoadKeyboardWindow(LoadCallback callback) {
  DCHECK(!window_);
  window_ = std::make_unique<aura::Window>(&delegate_);
  window_->Init(ui::LAYER_NOT_DRAWN);
  window_->set_owned_by_parent(false);

  // Set a default size for the keyboard.
  display::Screen* screen = display::Screen::GetScreen();
  window_->SetBounds(
      KeyboardBoundsFromRootBounds(screen->GetPrimaryDisplay().bounds()));

  // Simulate an asynchronous load.
  base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                   std::move(callback));

  return window_.get();
}

aura::Window* TestKeyboardUI::GetKeyboardWindow() const {
  return window_.get();
}

ui::InputMethod* TestKeyboardUI::GetInputMethod() {
  return input_method_;
}

}  // namespace keyboard
