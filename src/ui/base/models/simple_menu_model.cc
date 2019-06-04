// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/models/simple_menu_model.h"

#include <stddef.h>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/metrics/histogram_functions.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ui/base/l10n/l10n_util.h"

namespace ui {

const int kSeparatorId = -1;

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel::Delegate, public:

bool SimpleMenuModel::Delegate::IsCommandIdVisible(int command_id) const {
  return true;
}

bool SimpleMenuModel::Delegate::IsItemForCommandIdDynamic(
    int command_id) const {
  return false;
}

base::string16 SimpleMenuModel::Delegate::GetLabelForCommandId(
    int command_id) const {
  return base::string16();
}

base::string16 SimpleMenuModel::Delegate::GetSublabelForCommandId(
    int command_id) const {
  return base::string16();
}

base::string16 SimpleMenuModel::Delegate::GetMinorTextForCommandId(
    int command_id) const {
  return base::string16();
}

bool SimpleMenuModel::Delegate::GetIconForCommandId(
    int command_id, gfx::Image* image_skia) const {
  return false;
}

void SimpleMenuModel::Delegate::CommandIdHighlighted(int command_id) {
}

void SimpleMenuModel::Delegate::OnMenuWillShow(SimpleMenuModel* /*source*/) {}

void SimpleMenuModel::Delegate::MenuClosed(SimpleMenuModel* /*source*/) {
}

bool SimpleMenuModel::Delegate::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, public:

SimpleMenuModel::SimpleMenuModel(Delegate* delegate)
    : delegate_(delegate),
      menu_model_delegate_(nullptr),
      method_factory_(this) {}

SimpleMenuModel::~SimpleMenuModel() {
}

void SimpleMenuModel::AddItem(int command_id, const base::string16& label) {
  AppendItem(Item(command_id, TYPE_COMMAND, label));
}

void SimpleMenuModel::AddItemWithStringId(int command_id, int string_id) {
  AddItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddItemWithIcon(int command_id,
                                      const base::string16& label,
                                      const gfx::ImageSkia& icon) {
  Item item(command_id, TYPE_COMMAND, label);
  item.icon = gfx::Image(icon);
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddItemWithStringIdAndIcon(int command_id,
                                                 int string_id,
                                                 const gfx::ImageSkia& icon) {
  AddItemWithIcon(command_id, l10n_util::GetStringUTF16(string_id), icon);
}

void SimpleMenuModel::AddCheckItem(int command_id,
                                   const base::string16& label) {
  AppendItem(Item(command_id, TYPE_CHECK, label));
}

void SimpleMenuModel::AddCheckItemWithStringId(int command_id, int string_id) {
  AddCheckItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddRadioItem(int command_id,
                                   const base::string16& label,
                                   int group_id) {
  Item item(command_id, TYPE_RADIO, label);
  item.group_id = group_id;
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddRadioItemWithStringId(int command_id, int string_id,
                                               int group_id) {
  AddRadioItem(command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::AddHighlightedItemWithStringIdAndIcon(
    int command_id,
    int string_id,
    const gfx::ImageSkia& icon) {
  Item item(command_id, TYPE_HIGHLIGHTED, l10n_util::GetStringUTF16(string_id));
  item.icon = gfx::Image(icon);
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddSeparator(MenuSeparatorType separator_type) {
  if (items_.empty()) {
    if (separator_type == NORMAL_SEPARATOR) {
      return;
    }
    DCHECK_EQ(SPACING_SEPARATOR, separator_type);
  } else if (items_.back().type == TYPE_SEPARATOR) {
    DCHECK_EQ(NORMAL_SEPARATOR, separator_type);
    DCHECK_EQ(NORMAL_SEPARATOR, items_.back().separator_type);
    return;
  }
#if !defined(USE_AURA)
  if (separator_type == SPACING_SEPARATOR)
    NOTIMPLEMENTED();
#endif
  Item item(kSeparatorId, TYPE_SEPARATOR, base::string16());
  item.separator_type = separator_type;
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddButtonItem(int command_id,
                                    ButtonMenuItemModel* model) {
  Item item(command_id, TYPE_BUTTON_ITEM, base::string16());
  item.button_model = model;
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddSubMenu(int command_id,
                                 const base::string16& label,
                                 MenuModel* model) {
  Item item(command_id, TYPE_SUBMENU, label);
  item.submenu = model;
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddSubMenuWithStringId(int command_id,
                                             int string_id, MenuModel* model) {
  AddSubMenu(command_id, l10n_util::GetStringUTF16(string_id), model);
}

void SimpleMenuModel::AddActionableSubMenu(int command_id,
                                           const base::string16& label,
                                           MenuModel* model) {
  Item item(command_id, TYPE_ACTIONABLE_SUBMENU, label);
  item.submenu = model;
  AppendItem(std::move(item));
}

void SimpleMenuModel::AddActionableSubmenuWithStringIdAndIcon(
    int command_id,
    int string_id,
    MenuModel* model,
    const gfx::ImageSkia& icon) {
  Item item(command_id, TYPE_ACTIONABLE_SUBMENU,
            l10n_util::GetStringUTF16(string_id));
  item.submenu = model;
  item.icon = gfx::Image(icon);
  AppendItem(std::move(item));
}

void SimpleMenuModel::InsertItemAt(int index,
                                   int command_id,
                                   const base::string16& label) {
  InsertItemAtIndex(Item(command_id, TYPE_COMMAND, label), index);
}

void SimpleMenuModel::InsertItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertItemAt(index, command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertSeparatorAt(int index,
                                        MenuSeparatorType separator_type) {
#if !defined(USE_AURA)
  if (separator_type != NORMAL_SEPARATOR) {
    NOTIMPLEMENTED();
  }
#endif
  Item item(kSeparatorId, TYPE_SEPARATOR, base::string16());
  item.separator_type = separator_type;
  InsertItemAtIndex(std::move(item), index);
}

void SimpleMenuModel::InsertCheckItemAt(int index,
                                        int command_id,
                                        const base::string16& label) {
  InsertItemAtIndex(Item(command_id, TYPE_CHECK, label), index);
}

void SimpleMenuModel::InsertCheckItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertCheckItemAt(index, command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertRadioItemAt(int index,
                                        int command_id,
                                        const base::string16& label,
                                        int group_id) {
  Item item(command_id, TYPE_RADIO, label);
  item.group_id = group_id;
  InsertItemAtIndex(std::move(item), index);
}

void SimpleMenuModel::InsertRadioItemWithStringIdAt(
    int index, int command_id, int string_id, int group_id) {
  InsertRadioItemAt(
      index, command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::InsertSubMenuAt(int index,
                                      int command_id,
                                      const base::string16& label,
                                      MenuModel* model) {
  Item item(command_id, TYPE_SUBMENU, label);
  item.submenu = model;
  InsertItemAtIndex(std::move(item), index);
}

void SimpleMenuModel::InsertSubMenuWithStringIdAt(
    int index, int command_id, int string_id, MenuModel* model) {
  InsertSubMenuAt(index, command_id, l10n_util::GetStringUTF16(string_id),
                  model);
}

void SimpleMenuModel::RemoveItemAt(int index) {
  items_.erase(items_.begin() + ValidateItemIndex(index));
  MenuItemsChanged();
}

void SimpleMenuModel::SetIcon(int index, const gfx::Image& icon) {
  items_[ValidateItemIndex(index)].icon = icon;
  MenuItemsChanged();
}

void SimpleMenuModel::SetLabel(int index, const base::string16& label) {
  items_[ValidateItemIndex(index)].label = label;
  MenuItemsChanged();
}

void SimpleMenuModel::SetSublabel(int index, const base::string16& sublabel) {
  items_[ValidateItemIndex(index)].sublabel = sublabel;
  MenuItemsChanged();
}

void SimpleMenuModel::SetMinorText(int index,
                                   const base::string16& minor_text) {
  items_[ValidateItemIndex(index)].minor_text = minor_text;
}

void SimpleMenuModel::SetMinorIcon(int index,
                                   const gfx::VectorIcon& minor_icon) {
  items_[ValidateItemIndex(index)].minor_icon = &minor_icon;
}

void SimpleMenuModel::SetEnabledAt(int index, bool enabled) {
  if (items_[ValidateItemIndex(index)].enabled == enabled)
    return;

  items_[ValidateItemIndex(index)].enabled = enabled;
  MenuItemsChanged();
}

void SimpleMenuModel::SetVisibleAt(int index, bool visible) {
  if (items_[ValidateItemIndex(index)].visible == visible)
    return;

  items_[ValidateItemIndex(index)].visible = visible;
  MenuItemsChanged();
}

void SimpleMenuModel::Clear() {
  items_.clear();
  MenuItemsChanged();
}

int SimpleMenuModel::GetIndexOfCommandId(int command_id) const {
  for (auto i = items_.begin(); i != items_.end(); ++i) {
    if (i->command_id == command_id)
      return static_cast<int>(std::distance(items_.begin(), i));
  }
  return -1;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, MenuModel implementation:

bool SimpleMenuModel::HasIcons() const {
  for (auto i = items_.begin(); i != items_.end(); ++i) {
    if (!i->icon.IsEmpty())
      return true;
  }

  return false;
}

int SimpleMenuModel::GetItemCount() const {
  return static_cast<int>(items_.size());
}

MenuModel::ItemType SimpleMenuModel::GetTypeAt(int index) const {
  return items_[ValidateItemIndex(index)].type;
}

ui::MenuSeparatorType SimpleMenuModel::GetSeparatorTypeAt(int index) const {
  return items_[ValidateItemIndex(index)].separator_type;
}

int SimpleMenuModel::GetCommandIdAt(int index) const {
  return items_[ValidateItemIndex(index)].command_id;
}

base::string16 SimpleMenuModel::GetLabelAt(int index) const {
  if (IsItemDynamicAt(index))
    return delegate_->GetLabelForCommandId(GetCommandIdAt(index));
  return items_[ValidateItemIndex(index)].label;
}

base::string16 SimpleMenuModel::GetSublabelAt(int index) const {
  if (IsItemDynamicAt(index))
    return delegate_->GetSublabelForCommandId(GetCommandIdAt(index));
  return items_[ValidateItemIndex(index)].sublabel;
}

base::string16 SimpleMenuModel::GetMinorTextAt(int index) const {
  if (IsItemDynamicAt(index))
    return delegate_->GetMinorTextForCommandId(GetCommandIdAt(index));
  return items_[ValidateItemIndex(index)].minor_text;
}

const gfx::VectorIcon* SimpleMenuModel::GetMinorIconAt(int index) const {
  return items_[ValidateItemIndex(index)].minor_icon;
}

bool SimpleMenuModel::IsItemDynamicAt(int index) const {
  if (delegate_)
    return delegate_->IsItemForCommandIdDynamic(GetCommandIdAt(index));
  return false;
}

bool SimpleMenuModel::GetAcceleratorAt(int index,
                                       ui::Accelerator* accelerator) const {
  if (delegate_) {
    return delegate_->GetAcceleratorForCommandId(GetCommandIdAt(index),
                                                 accelerator);
  }
  return false;
}

bool SimpleMenuModel::IsItemCheckedAt(int index) const {
  if (!delegate_)
    return false;
  MenuModel::ItemType item_type = GetTypeAt(index);
  return (item_type == TYPE_CHECK || item_type == TYPE_RADIO) ?
      delegate_->IsCommandIdChecked(GetCommandIdAt(index)) : false;
}

int SimpleMenuModel::GetGroupIdAt(int index) const {
  return items_[ValidateItemIndex(index)].group_id;
}

bool SimpleMenuModel::GetIconAt(int index, gfx::Image* icon) {
  if (IsItemDynamicAt(index))
    return delegate_->GetIconForCommandId(GetCommandIdAt(index), icon);

  ValidateItemIndex(index);
  if (items_[index].icon.IsEmpty())
    return false;

  *icon = items_[index].icon;
  return true;
}

ButtonMenuItemModel* SimpleMenuModel::GetButtonMenuItemAt(int index) const {
  return items_[ValidateItemIndex(index)].button_model;
}

bool SimpleMenuModel::IsEnabledAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (!delegate_ || command_id == kSeparatorId || GetButtonMenuItemAt(index))
    return items_[ValidateItemIndex(index)].enabled;

  return delegate_->IsCommandIdEnabled(command_id) &&
         items_[ValidateItemIndex(index)].enabled;
}

bool SimpleMenuModel::IsVisibleAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (!delegate_ || command_id == kSeparatorId || GetButtonMenuItemAt(index))
    return items_[ValidateItemIndex(index)].visible;

  return delegate_->IsCommandIdVisible(command_id) &&
         items_[ValidateItemIndex(index)].visible;
}

void SimpleMenuModel::HighlightChangedTo(int index) {
  if (delegate_)
    delegate_->CommandIdHighlighted(GetCommandIdAt(index));
}

void SimpleMenuModel::ActivatedAt(int index) {
  ActivatedAt(index, 0);
}

void SimpleMenuModel::ActivatedAt(int index, int event_flags) {
  if (!delegate_)
    return;

  int command_id = GetCommandIdAt(index);
  RecordHistogram(command_id);
  delegate_->ExecuteCommand(command_id, event_flags);
}

MenuModel* SimpleMenuModel::GetSubmenuModelAt(int index) const {
  return items_[ValidateItemIndex(index)].submenu;
}

void SimpleMenuModel::MenuWillShow() {
  if (delegate_)
    delegate_->OnMenuWillShow(this);
}

void SimpleMenuModel::MenuWillClose() {
  // Due to how menus work on the different platforms, ActivatedAt will be
  // called after this.  It's more convenient for the delegate to be called
  // afterwards though, so post a task.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&SimpleMenuModel::OnMenuClosed,
                                method_factory_.GetWeakPtr()));
}

void SimpleMenuModel::SetMenuModelDelegate(
      ui::MenuModelDelegate* menu_model_delegate) {
  menu_model_delegate_ = menu_model_delegate;
}

MenuModelDelegate* SimpleMenuModel::GetMenuModelDelegate() const {
  return menu_model_delegate_;
}

void SimpleMenuModel::OnMenuClosed() {
  if (delegate_)
    delegate_->MenuClosed(this);
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, Protected:

void SimpleMenuModel::MenuItemsChanged() {
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, Private:

SimpleMenuModel::Item::Item(Item&&) = default;
SimpleMenuModel::Item::Item(int command_id, ItemType type, base::string16 label)
    : command_id(command_id), type(type), label(label) {}
SimpleMenuModel::Item& SimpleMenuModel::Item::operator=(Item&&) = default;
SimpleMenuModel::Item::~Item() = default;

int SimpleMenuModel::ValidateItemIndex(int index) const {
  CHECK_GE(index, 0);
  CHECK_LT(static_cast<size_t>(index), items_.size());
  return index;
}

void SimpleMenuModel::AppendItem(Item item) {
  ValidateItem(item);
  items_.push_back(std::move(item));
  MenuItemsChanged();
}

void SimpleMenuModel::InsertItemAtIndex(Item item, int index) {
  ValidateItem(item);
  items_.insert(items_.begin() + index, std::move(item));
  MenuItemsChanged();
}

void SimpleMenuModel::ValidateItem(const Item& item) {
#ifndef NDEBUG
  if (item.type == TYPE_SEPARATOR) {
    DCHECK_EQ(item.command_id, kSeparatorId);
  } else {
    DCHECK_GE(item.command_id, 0);
  }
#endif  // NDEBUG
}

void SimpleMenuModel::RecordHistogram(int command_id) const {
  if (histogram_name_.empty())
    return;
  base::UmaHistogramSparse(histogram_name_, command_id);
}

}  // namespace ui
