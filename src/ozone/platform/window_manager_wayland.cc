// Copyright 2014 Intel Corporation. All rights reserved.
// Copyright 2016-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "ozone/platform/window_manager_wayland.h"

#include <sys/mman.h>
#include <string>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ozone/platform/desktop_platform_screen_delegate.h"
#include "ozone/platform/messages.h"
#include "ozone/platform/ozone_gpu_platform_support_host.h"
#include "ozone/platform/ozone_wayland_window.h"
#include "ozone/wayland/ozone_wayland_screen.h"
#include "ui/aura/window.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/event_switches.h"
#include "ui/events/event_utils.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

WindowManagerWayland::WindowManagerWayland(OzoneGpuPlatformSupportHost* proxy)
    : open_windows_(NULL),
      proxy_(proxy),
      keyboard_(KeyboardEvdevNeva::Create(&modifiers_,
                KeyboardLayoutEngineManager::GetKeyboardLayoutEngine(),
                base::Bind(&WindowManagerWayland::PostUiEvent,
                           base::Unretained(this)))),
      platform_screen_(NULL),
      dragging_(false),
      touch_slot_generator_(0),
      weak_ptr_factory_(this) {
  proxy_->RegisterHandler(this);
}

WindowManagerWayland::~WindowManagerWayland() {
}

ui::DeviceHotplugEventObserver*
WindowManagerWayland::GetHotplugEventObserver() {
  return ui::DeviceDataManager::GetInstance();
}

void WindowManagerWayland::OnRootWindowCreated(
    OzoneWaylandWindow* window) {
  open_windows().push_back(window);
}

void WindowManagerWayland::OnRootWindowClosed(
    OzoneWaylandWindow* window) {
  open_windows().remove(window);
  if (window && GetActiveWindow(window->GetDisplayId()) == window) {
    active_window_map_[window->GetDisplayId()] = nullptr;
    if (!open_windows().empty())
      OnActivationChanged(open_windows().front()->GetHandle(), true);
  }

  if (event_grabber_ == gfx::AcceleratedWidget(window->GetHandle()))
    event_grabber_ = gfx::kNullAcceleratedWidget;

  if (current_capture_ == gfx::AcceleratedWidget(window->GetHandle())) {
    OzoneWaylandWindow* window = GetWindow(current_capture_);
    window->GetDelegate()->OnLostCapture();
    current_capture_ = gfx::kNullAcceleratedWidget;
  }

  if (open_windows().empty()) {
    delete open_windows_;
    open_windows_ = NULL;
    return;
  }

}

void WindowManagerWayland::Restore(OzoneWaylandWindow* window) {
  if (window) {
    active_window_map_[window->GetDisplayId()] = window;
    event_grabber_  = window->GetHandle();
  }
}

void WindowManagerWayland::OnPlatformScreenCreated(
      ozonewayland::OzoneWaylandScreen* screen) {
  DCHECK(!platform_screen_);
  platform_screen_ = screen;
}

PlatformCursor WindowManagerWayland::GetPlatformCursor() {
  return platform_cursor_;
}

void WindowManagerWayland::SetPlatformCursor(PlatformCursor cursor) {
  platform_cursor_ = cursor;
}

bool WindowManagerWayland::HasWindowsOpen() const {
  return open_windows_ ? !open_windows_->empty() : false;
}

OzoneWaylandWindow* WindowManagerWayland::GetActiveWindow(
    const std::string& display_id) const {
  if (active_window_map_.find(display_id) != active_window_map_.end())
    return active_window_map_.at(display_id);
  return nullptr;
}

void WindowManagerWayland::GrabEvents(gfx::AcceleratedWidget widget) {
  if (current_capture_ == widget)
    return;

  if (current_capture_) {
    OzoneWaylandWindow* window = GetWindow(current_capture_);
    window->GetDelegate()->OnLostCapture();
  }

  current_capture_ = widget;
  event_grabber_ = widget;
}

void WindowManagerWayland::UngrabEvents(gfx::AcceleratedWidget widget) {
  if (current_capture_ != widget)
    return;

  if (current_capture_) {
    OzoneWaylandWindow* window = GetWindow(current_capture_);
    if (window) {
      OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());
      current_capture_ = gfx::kNullAcceleratedWidget;
      event_grabber_ = active_window ? active_window->GetHandle() : 0;
    }
  }
}

OzoneWaylandWindow*
WindowManagerWayland::GetWindow(unsigned handle) {
  OzoneWaylandWindow* window = NULL;
  const std::list<OzoneWaylandWindow*>& windows = open_windows();
  std::list<OzoneWaylandWindow*>::const_iterator it;
  for (it = windows.begin(); it != windows.end(); ++it) {
    if ((*it)->GetHandle() == handle) {
      window = *it;
      break;
    }
  }

  return window;
}

////////////////////////////////////////////////////////////////////////////////
// WindowManagerWayland, Private implementation:
void WindowManagerWayland::OnActivationChanged(unsigned windowhandle,
                                               bool active) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Invalid window handle " << windowhandle;
    return;
  }

  OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());

  if (active) {
    event_grabber_ = windowhandle;
    if (current_capture_)
      return;

    if (active_window && active_window == window)
      return;

    if (active_window)
      active_window->GetDelegate()->OnActivationChanged(false);

    active_window_map_[window->GetDisplayId()] = window;
    window->GetDelegate()->OnActivationChanged(active);
  } else if (active_window == window) {
    active_window->GetDelegate()->OnActivationChanged(active);
    if (event_grabber_ == gfx::AcceleratedWidget(active_window->GetHandle()))
      event_grabber_ = gfx::kNullAcceleratedWidget;

    active_window_map_[window->GetDisplayId()] = nullptr;
  }
}

std::list<OzoneWaylandWindow*>&
WindowManagerWayland::open_windows() {
  if (!open_windows_)
    open_windows_ = new std::list<OzoneWaylandWindow*>();

  return *open_windows_;
}

void WindowManagerWayland::OnWindowFocused(unsigned handle) {
  OnActivationChanged(handle, true);
}

void WindowManagerWayland::OnWindowEnter(unsigned handle) {
  OnWindowFocused(handle);
}

void WindowManagerWayland::OnWindowLeave(unsigned handle) {
}

void WindowManagerWayland::OnWindowClose(unsigned handle) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  window->GetDelegate()->OnCloseRequest();
}

void WindowManagerWayland::OnWindowResized(unsigned handle,
                                           unsigned width,
                                           unsigned height) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  if (!window->GetResizeEnabled())
    return;

  const gfx::Rect& current_bounds = window->GetBounds();
  window->SetBounds(gfx::Rect(current_bounds.x(),
                              current_bounds.y(),
                              width,
                              height));
}

void WindowManagerWayland::OnWindowUnminimized(unsigned handle) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  window->GetDelegate()->OnWindowStateChanged(PLATFORM_WINDOW_STATE_MAXIMIZED);
}

void WindowManagerWayland::OnWindowDeActivated(unsigned windowhandle) {
  OnActivationChanged(windowhandle, false);
}

void WindowManagerWayland::OnWindowActivated(unsigned windowhandle) {
  OnWindowFocused(windowhandle);
}

////////////////////////////////////////////////////////////////////////////////
// GpuPlatformSupportHost implementation:
void WindowManagerWayland::OnGpuProcessLaunched(
    int host_id,
    scoped_refptr<base::SingleThreadTaskRunner> ui_runner,
    scoped_refptr<base::SingleThreadTaskRunner> send_runner,
    const base::Callback<void(IPC::Message*)>& send_callback) {
}

void WindowManagerWayland::OnChannelDestroyed(int host_id) {
}

void WindowManagerWayland::OnGpuServiceLaunched(
    scoped_refptr<base::SingleThreadTaskRunner> host_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_runner,
    GpuHostBindInterfaceCallback binder,
    GpuHostTerminateCallback terminate_callback) {
}

void WindowManagerWayland::OnMessageReceived(const IPC::Message& message) {
  IPC_BEGIN_MESSAGE_MAP(WindowManagerWayland, message)
  IPC_MESSAGE_HANDLER(WaylandInput_CloseWidget, CloseWidget)
  IPC_MESSAGE_HANDLER(WaylandWindow_Resized, WindowResized)
  IPC_MESSAGE_HANDLER(WaylandWindow_Activated, WindowActivated)
  IPC_MESSAGE_HANDLER(WaylandWindow_DeActivated, WindowDeActivated)
  IPC_MESSAGE_HANDLER(WaylandWindow_Unminimized, WindowUnminimized)
  IPC_MESSAGE_HANDLER(WaylandInput_MotionNotify, MotionNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_ButtonNotify, ButtonNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchNotify, TouchNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_AxisNotify, AxisNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerEnter, PointerEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerLeave, PointerLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelEnter, InputPanelEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelLeave, InputPanelLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardEnter, KeyboardEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardLeave, KeyboardLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyNotify, KeyNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_VirtualKeyNotify, VirtualKeyNotify)
  IPC_MESSAGE_HANDLER(WaylandOutput_ScreenChanged, ScreenChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_InitializeXKB, InitializeXKB)
  IPC_MESSAGE_HANDLER(WaylandInput_DragEnter, DragEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_DragData, DragData)
  IPC_MESSAGE_HANDLER(WaylandInput_DragLeave, DragLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_DragMotion, DragMotion)
  IPC_MESSAGE_HANDLER(WaylandInput_DragDrop, DragDrop)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelVisibilityChanged, InputPanelVisibilityChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelRectChanged, InputPanelRectChanged)
  IPC_MESSAGE_HANDLER(WaylandWindow_Close, WindowClose)
  IPC_MESSAGE_HANDLER(WaylandWindow_Exposed, NativeWindowExposed)
  IPC_MESSAGE_HANDLER(WaylandWindow_StateChanged, NativeWindowStateChanged)
  IPC_MESSAGE_HANDLER(WaylandWindow_StateAboutToChange, NativeWindowStateAboutToChange)
  IPC_MESSAGE_HANDLER(WaylandInput_CursorVisibilityChanged, CursorVisibilityChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardAdded, KeyboardAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardRemoved, KeyboardRemoved)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerAdded, PointerAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerRemoved, PointerRemoved)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchscreenAdded, TouchscreenAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchscreenRemoved, TouchscreenRemoved)
  IPC_END_MESSAGE_MAP()
}

void WindowManagerWayland::MotionNotify(float x, float y) {
  if (dragging_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&WindowManagerWayland::NotifyDragging,
                              weak_ptr_factory_.GetWeakPtr(), x, y));
  } else {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&WindowManagerWayland::NotifyMotion,
                              weak_ptr_factory_.GetWeakPtr(), x, y));
  }
}

void WindowManagerWayland::ButtonNotify(unsigned handle,
                                        EventType type,
                                        EventFlags flags,
                                        float x,
                                        float y) {
  dragging_ =
      ((type == ui::ET_MOUSE_PRESSED) && (flags == ui::EF_LEFT_MOUSE_BUTTON));

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyButtonPress,
          weak_ptr_factory_.GetWeakPtr(), handle, type, flags, x, y));
}

void WindowManagerWayland::AxisNotify(float x,
                                      float y,
                                      int xoffset,
                                      int yoffset) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyAxis,
          weak_ptr_factory_.GetWeakPtr(), x, y, xoffset, yoffset));
}

void WindowManagerWayland::PointerEnter(uint32_t device_id,
                                        unsigned handle,
                                        float x,
                                        float y) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyPointerEnter,
                 weak_ptr_factory_.GetWeakPtr(), device_id, handle, x, y));
}

void WindowManagerWayland::PointerLeave(uint32_t device_id,
                                        unsigned handle,
                                        float x,
                                        float y) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyPointerLeave,
                 weak_ptr_factory_.GetWeakPtr(), device_id, handle, x, y));
}

void WindowManagerWayland::InputPanelEnter(uint32_t device_id,
                                           unsigned handle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyInputPanelEnter,
          weak_ptr_factory_.GetWeakPtr(), device_id, handle));
}

void WindowManagerWayland::InputPanelLeave(uint32_t device_id) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyInputPanelLeave,
          weak_ptr_factory_.GetWeakPtr(), device_id));
}

void WindowManagerWayland::KeyNotify(EventType type,
                                     unsigned code,
                                     int device_id) {
  VirtualKeyNotify(type, code, device_id);
}

void WindowManagerWayland::VirtualKeyNotify(EventType type,
                                            uint32_t key,
                                            int device_id) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnVirtualKeyNotify,
                 weak_ptr_factory_.GetWeakPtr(), type, key, device_id));
}

void WindowManagerWayland::OnVirtualKeyNotify(EventType type,
                                              uint32_t key,
                                              int device_id) {
  keyboard_->OnKeyChange(key, type != ET_KEY_RELEASED, false, EventTimeForNow(),
                         device_id);
}

void WindowManagerWayland::TouchNotify(uint32_t device_id,
                                       unsigned handle,
                                       ui::EventType type,
                                       const ui::TouchEventInfo& event_info) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyTouchEvent,
                            weak_ptr_factory_.GetWeakPtr(), device_id, handle,
                            type, event_info));
}

void WindowManagerWayland::CloseWidget(unsigned handle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnWindowClose,
          weak_ptr_factory_.GetWeakPtr(), handle));
}

void WindowManagerWayland::ScreenChanged(unsigned width, unsigned height,
                                         int rotation) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyScreenChanged,
          weak_ptr_factory_.GetWeakPtr(), width, height, rotation));
}

void WindowManagerWayland::KeyboardAdded(int id, const std::string& name) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyKeyboardAdded,
                            weak_ptr_factory_.GetWeakPtr(), id, name));
}

void WindowManagerWayland::KeyboardRemoved(int id) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyKeyboardRemoved,
                            weak_ptr_factory_.GetWeakPtr(), id));
}

void WindowManagerWayland::PointerAdded(int id, const std::string& name) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyPointerAdded,
                            weak_ptr_factory_.GetWeakPtr(), id, name));
}

void WindowManagerWayland::PointerRemoved(int id) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyPointerRemoved,
                            weak_ptr_factory_.GetWeakPtr(), id));
}

void WindowManagerWayland::TouchscreenAdded(int id, const std::string& name) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyTouchscreenAdded,
                            weak_ptr_factory_.GetWeakPtr(), id, name));
}

void WindowManagerWayland::TouchscreenRemoved(int id) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyTouchscreenRemoved,
                            weak_ptr_factory_.GetWeakPtr(), id));
}

void WindowManagerWayland::WindowResized(unsigned handle,
                                         unsigned width,
                                         unsigned height) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnWindowResized,
          weak_ptr_factory_.GetWeakPtr(), handle, width, height));
}

void WindowManagerWayland::WindowUnminimized(unsigned handle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnWindowUnminimized,
          weak_ptr_factory_.GetWeakPtr(), handle));
}

void WindowManagerWayland::WindowDeActivated(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnWindowDeActivated,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::WindowActivated(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::OnWindowActivated,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::DragEnter(
    unsigned windowhandle,
    float x,
    float y,
    const std::vector<std::string>& mime_types,
    uint32_t serial) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyDragEnter,
          weak_ptr_factory_.GetWeakPtr(),
          windowhandle, x, y, mime_types, serial));
}

void WindowManagerWayland::DragData(unsigned windowhandle,
                                    base::FileDescriptor pipefd) {
  // TODO(mcatanzaro): pipefd will be leaked if the WindowManagerWayland is
  // destroyed before NotifyDragData is called.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyDragData,
          weak_ptr_factory_.GetWeakPtr(), windowhandle, pipefd));
}

void WindowManagerWayland::DragLeave(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyDragLeave,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::DragMotion(unsigned windowhandle,
                                      float x,
                                      float y,
                                      uint32_t time) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyDragMotion,
          weak_ptr_factory_.GetWeakPtr(), windowhandle, x, y, time));
}

void WindowManagerWayland::DragDrop(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyDragDrop,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::InitializeXKB(base::SharedMemoryHandle fd,
                                         uint32_t size) {
  char* map_str =
      reinterpret_cast<char*>(mmap(NULL,
                                   size,
                                   PROT_READ,
                                   MAP_SHARED,
                                   fd.GetHandle(),
                                   0));
  if (map_str == MAP_FAILED)
    return;

  KeyboardLayoutEngineManager::GetKeyboardLayoutEngine()->
      SetCurrentLayoutFromBuffer(map_str, strlen(map_str));
  munmap(map_str, size);
  close(fd.GetHandle());
}

////////////////////////////////////////////////////////////////////////////////
// PlatformEventSource implementation:
void WindowManagerWayland::PostUiEvent(Event* event) {
  DispatchEvent(event);
}

void WindowManagerWayland::DispatchUiEventTask(std::unique_ptr<Event> event) {
  DispatchEvent(event.get());
}

void WindowManagerWayland::OnDispatcherListChanged() {
}

////////////////////////////////////////////////////////////////////////////////
void WindowManagerWayland::NotifyMotion(float x,
                                        float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_MOVED,
                         position,
                         position,
                         EventTimeForNow(),
                         0,
                         0);
  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyDragging(float x, float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_DRAGGED, position, position, EventTimeForNow(),
                     ui::EF_LEFT_MOUSE_BUTTON, 0);
  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyButtonPress(unsigned handle,
                                             EventType type,
                                             EventFlags flags,
                                             float x,
                                             float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(type,
                         position,
                         position,
                         EventTimeForNow(),
                         flags,
                         flags);

  DispatchEvent(&mouseev);

  if (type == ET_MOUSE_RELEASED)
    OnWindowFocused(handle);
}

void WindowManagerWayland::NotifyAxis(float x,
                                      float y,
                                      int xoffset,
                                      int yoffset) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSEWHEEL, position, position, EventTimeForNow(), 0,
                     0);

  MouseWheelEvent wheelev(mouseev, xoffset, yoffset);

  DispatchEvent(&wheelev);
}

void WindowManagerWayland::NotifyPointerEnter(uint32_t device_id,
                                              unsigned handle,
                                              float x,
                                              float y) {
  OnWindowEnter(handle);

  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_ENTERED, position, position, EventTimeForNow(), 0,
                     0);

  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyPointerLeave(uint32_t device_id,
                                              unsigned handle,
                                              float x,
                                              float y) {
  OnWindowLeave(handle);
#if !defined(OS_WEBOS)
  // This is gm-rsi fix for LSM issue.
  // LSM send pointer leave event to a window
  // if we touch on another window of the second display
  // The first window after that can be unfocused.
  // We can't right handle pointer leave event
  // on the first window on client side.
  // Disable dispatching ET_MOUSE_EXITED event.
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_EXITED, position, position, EventTimeForNow(), 0,
                     0);

  DispatchEvent(&mouseev);
#endif
}

void WindowManagerWayland::NotifyInputPanelEnter(uint32_t device_id,
                                                 unsigned handle) {
  GrabDeviceEvents(device_id, handle);
}

void WindowManagerWayland::NotifyInputPanelLeave(uint32_t device_id) {
  UnGrabDeviceEvents(device_id);
}

void WindowManagerWayland::NotifyTouchEvent(uint32_t device_id,
                                            unsigned handle,
                                            ui::EventType type,
                                            const ui::TouchEventInfo& event_info) {
  gfx::Point position(event_info.x_, event_info.y_);
  base::TimeTicks timestamp =
      base::TimeTicks() + base::TimeDelta::FromMilliseconds(event_info.time_stamp_);
  uint32_t touch_slot = touch_slot_generator_.GetGeneratedID(event_info.touch_id_);

  if (type == ET_TOUCH_PRESSED)
    GrabTouchButton(device_id, handle);

  TouchEvent touchev(
      type, position, timestamp,
      ui::PointerDetails(ui::EventPointerType::POINTER_TYPE_TOUCH, touch_slot));
  touchev.set_source_device_id(device_id);

  DispatchEvent(&touchev);

  if (type == ET_TOUCH_RELEASED || type == ET_TOUCH_CANCELLED) {
    if(type == ET_TOUCH_CANCELLED)
      UnGrabTouchButton(device_id);
    touch_slot_generator_.ReleaseNumber(event_info.touch_id_);
  }
}

void WindowManagerWayland::NotifyScreenChanged(unsigned width,
                                               unsigned height,
                                               int rotation) {
  if (platform_screen_)
    platform_screen_->GetDelegate()->OnScreenChanged(width, height, rotation);
}

void WindowManagerWayland::NotifyKeyboardAdded(int id,
                                               const std::string& name) {
  keyboard_devices_.push_back(
      ui::InputDevice(id, ui::INPUT_DEVICE_UNKNOWN, name));
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(keyboard_devices_);
}

void WindowManagerWayland::NotifyKeyboardRemoved(int id) {
  base::EraseIf(keyboard_devices_,
                [id](const auto& device) { return device.id == id; });
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(keyboard_devices_);
}

void WindowManagerWayland::NotifyPointerAdded(int id, const std::string& name) {
  pointer_devices_.push_back(
      ui::InputDevice(id, ui::INPUT_DEVICE_UNKNOWN, name));
  GetHotplugEventObserver()->OnMouseDevicesUpdated(pointer_devices_);
}

void WindowManagerWayland::NotifyPointerRemoved(int id) {
  base::EraseIf(pointer_devices_,
                [id](const auto& device) { return device.id == id; });
  GetHotplugEventObserver()->OnMouseDevicesUpdated(pointer_devices_);
}

void WindowManagerWayland::NotifyTouchscreenAdded(int id,
                                                  const std::string& name) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kIgnoreTouchDevices))
    return;
  int max_touch_points = 1;
  std::string override_max_touch_points =
      command_line->GetSwitchValueASCII(switches::kForceMaxTouchPoints);
  if (!override_max_touch_points.empty()) {
    int temp;
    if (base::StringToInt(override_max_touch_points, &temp))
      max_touch_points = temp;
  }
  touchscreen_devices_.push_back(ui::TouchscreenDevice(
      id, ui::INPUT_DEVICE_UNKNOWN, name, gfx::Size(), max_touch_points));
  GetHotplugEventObserver()->OnTouchscreenDevicesUpdated(touchscreen_devices_);
}

void WindowManagerWayland::NotifyTouchscreenRemoved(int id) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kIgnoreTouchDevices)) {
    return;
  }
  base::EraseIf(touchscreen_devices_,
                [id](const auto& device) { return device.id == id; });
  GetHotplugEventObserver()->OnTouchscreenDevicesUpdated(touchscreen_devices_);
}

void WindowManagerWayland::NotifyDragEnter(
    unsigned windowhandle,
    float x,
    float y,
    const std::vector<std::string>& mime_types,
    uint32_t serial) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragEnter(windowhandle, x, y, mime_types, serial);
}

void WindowManagerWayland::NotifyDragData(unsigned windowhandle,
                                          base::FileDescriptor pipefd) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    close(pipefd.fd);
    return;
  }
  window->GetDelegate()->OnDragDataReceived(pipefd.fd);
}

void WindowManagerWayland::NotifyDragLeave(unsigned windowhandle) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragLeave();
}

void WindowManagerWayland::NotifyDragMotion(unsigned windowhandle,
                                            float x,
                                            float y,
                                            uint32_t time) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragMotion(x, y, time);
}

void WindowManagerWayland::NotifyDragDrop(unsigned windowhandle) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragDrop();
}

// Additional notification for app-runtime
void WindowManagerWayland::InputPanelVisibilityChanged(unsigned windowhandle,
                                                       bool visibility) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyInputPanelVisibilityChanged,
                 weak_ptr_factory_.GetWeakPtr(), windowhandle, visibility));
}

void WindowManagerWayland::InputPanelRectChanged(unsigned windowhandle,
                                                 int32_t x,
                                                 int32_t y,
                                                 uint32_t width,
                                                 uint32_t height) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&WindowManagerWayland::NotifyInputPanelRectChanged,
                            weak_ptr_factory_.GetWeakPtr(), windowhandle, x, y,
                            width, height));
}

void WindowManagerWayland::NativeWindowExposed(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyNativeWindowExposed,
                 weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::WindowClose(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyWindowClose,
                 weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::KeyboardEnter(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyKeyboardEnter,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::KeyboardLeave(unsigned windowhandle) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyKeyboardLeave,
          weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::CursorVisibilityChanged(bool visible) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyCursorVisibilityChanged,
                 weak_ptr_factory_.GetWeakPtr(), visible));
}

void WindowManagerWayland::NotifyInputPanelVisibilityChanged(
    unsigned windowhandle,
    bool visibility) {
  if (!visibility) {
    for (auto* const window : open_windows()) {
      if (window->GetHandle() != windowhandle)
        window->GetDelegate()->OnInputPanelVisibilityChanged(visibility);
    }
  }

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnInputPanelVisibilityChanged(visibility);
}

void WindowManagerWayland::NotifyInputPanelRectChanged(unsigned windowhandle,
                                                       int32_t x,
                                                       int32_t y,
                                                       uint32_t width,
                                                       uint32_t height) {
  for (auto* const window : open_windows()) {
    window->GetDelegate()->OnInputPanelRectChanged(x, y, width, height);
  }

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
}

void WindowManagerWayland::NativeWindowStateChanged(unsigned handle,
                                                    ui::WidgetState new_state) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyNativeWindowStateChanged,
                 weak_ptr_factory_.GetWeakPtr(), handle, new_state));
}

void WindowManagerWayland::NativeWindowStateAboutToChange(unsigned handle,
                                                          ui::WidgetState state) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowManagerWayland::NotifyNativeWindowStateAboutToChange,
                 weak_ptr_factory_.GetWeakPtr(), handle, state));
}

void WindowManagerWayland::NotifyNativeWindowExposed(unsigned windowhandle) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnWindowHostExposed();
}

void WindowManagerWayland::NotifyWindowClose(unsigned windowhandle) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnWindowHostClose();
}

void WindowManagerWayland::NotifyKeyboardEnter(unsigned windowhandle) {
  OnWindowEnter(windowhandle);

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnKeyboardEnter();
}

void WindowManagerWayland::NotifyKeyboardLeave(unsigned windowhandle) {
  OnWindowLeave(windowhandle);

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnKeyboardLeave();
}

void WindowManagerWayland::NotifyCursorVisibilityChanged(bool visible) {
  // Notify all open windows with cursor visibility state change
  const std::list<OzoneWaylandWindow*>& windows = open_windows();
  for (const OzoneWaylandWindow* window: windows) {
    window->GetDelegate()->OnCursorVisibilityChanged(visible);
  }
}

void WindowManagerWayland::NotifyNativeWindowStateChanged(unsigned handle,
                                                          ui::WidgetState new_state) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }
  VLOG(1) << __PRETTY_FUNCTION__;
  window->GetDelegate()->OnWindowHostStateChanged(new_state);
}

void WindowManagerWayland::NotifyNativeWindowStateAboutToChange(unsigned handle,
                                                                ui::WidgetState state) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }
  VLOG(1) << __PRETTY_FUNCTION__;
  window->GetDelegate()->OnWindowHostStateAboutToChange(state);
}

void WindowManagerWayland::GrabDeviceEvents(uint32_t device_id,
                                            unsigned widget) {
  OzoneWaylandWindow* window = GetWindow(widget);
  if (window) {
    OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());
    if (active_window && widget == active_window->GetHandle())
      device_event_grabber_map_[device_id] = widget;
  }
}

void WindowManagerWayland::UnGrabDeviceEvents(uint32_t device_id) {
  if (device_event_grabber_map_.find(device_id) !=
      device_event_grabber_map_.end())
    device_event_grabber_map_[device_id] = 0;
}

unsigned WindowManagerWayland::DeviceEventGrabber(uint32_t device_id) const {
  if (device_event_grabber_map_.find(device_id) !=
      device_event_grabber_map_.end())
    return device_event_grabber_map_.at(device_id);
  return 0;
}

void WindowManagerWayland::GrabTouchButton(uint32_t touch_button_id, unsigned widget) {
  OzoneWaylandWindow* window = GetWindow(widget);
  if (window) {
    OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());
    if (active_window && widget == active_window->GetHandle())
      touch_button_grabber_map_[touch_button_id] = widget;
  }
}

void WindowManagerWayland::UnGrabTouchButton(uint32_t touch_button_id) {
  if (device_event_grabber_map_.find(touch_button_id) !=
      device_event_grabber_map_.end())
    device_event_grabber_map_.erase(touch_button_id);
}

unsigned WindowManagerWayland::TouchButtonGrabber(uint32_t touch_button_id) const {
  if (touch_button_grabber_map_.find(touch_button_id) !=
      touch_button_grabber_map_.end())
    return touch_button_grabber_map_.at(touch_button_id);
  return 0;
}

}  // namespace ui
