// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/ax_node_data.h"

#include <stddef.h>

#include <algorithm>
#include <set>

#include "base/no_destructor.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/gfx/transform.h"

namespace ui {

namespace {

bool IsFlagSet(uint32_t bitfield, uint32_t flag) {
  return (bitfield & (1 << flag)) != 0;
}

uint32_t ModifyFlag(uint32_t bitfield, uint32_t flag, bool set) {
  return set ? (bitfield |= (1 << flag)) : (bitfield &= ~(1 << flag));
}

std::string StateBitfieldToString(uint32_t state_enum) {
  std::string str;
  for (uint32_t i = static_cast<uint32_t>(ax::mojom::State::kNone) + 1;
       i <= static_cast<uint32_t>(ax::mojom::State::kMaxValue); ++i) {
    if (IsFlagSet(state_enum, i))
      str += " " +
             base::ToUpperASCII(ui::ToString(static_cast<ax::mojom::State>(i)));
  }
  return str;
}

std::string ActionsBitfieldToString(uint32_t actions) {
  std::string str;
  for (uint32_t i = static_cast<uint32_t>(ax::mojom::Action::kNone) + 1;
       i <= static_cast<uint32_t>(ax::mojom::Action::kMaxValue); ++i) {
    if (IsFlagSet(actions, i)) {
      str += ui::ToString(static_cast<ax::mojom::Action>(i));
      actions = ModifyFlag(actions, i, false);
      str += actions ? "," : "";
    }
  }
  return str;
}

std::string IntVectorToString(const std::vector<int>& items) {
  std::string str;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0)
      str += ",";
    str += base::NumberToString(items[i]);
  }
  return str;
}

// Predicate that returns true if the first value of a pair is |first|.
template<typename FirstType, typename SecondType>
struct FirstIs {
  FirstIs(FirstType first)
      : first_(first) {}
  bool operator()(std::pair<FirstType, SecondType> const& p) {
    return p.first == first_;
  }
  FirstType first_;
};

// Helper function that finds a key in a vector of pairs by matching on the
// first value, and returns an iterator.
template<typename FirstType, typename SecondType>
typename std::vector<std::pair<FirstType, SecondType>>::const_iterator
    FindInVectorOfPairs(
        FirstType first,
        const std::vector<std::pair<FirstType, SecondType>>& vector) {
  return std::find_if(vector.begin(),
                      vector.end(),
                      FirstIs<FirstType, SecondType>(first));
}

}  // namespace

// Return true if |attr| is a node ID that would need to be mapped when
// renumbering the ids in a combined tree.
bool IsNodeIdIntAttribute(ax::mojom::IntAttribute attr) {
  switch (attr) {
    case ax::mojom::IntAttribute::kActivedescendantId:
    case ax::mojom::IntAttribute::kDetailsId:
    case ax::mojom::IntAttribute::kErrormessageId:
    case ax::mojom::IntAttribute::kInPageLinkTargetId:
    case ax::mojom::IntAttribute::kMemberOfId:
    case ax::mojom::IntAttribute::kNextOnLineId:
    case ax::mojom::IntAttribute::kPreviousOnLineId:
    case ax::mojom::IntAttribute::kTableHeaderId:
    case ax::mojom::IntAttribute::kTableColumnHeaderId:
    case ax::mojom::IntAttribute::kTableRowHeaderId:
    case ax::mojom::IntAttribute::kNextFocusId:
    case ax::mojom::IntAttribute::kPreviousFocusId:
      return true;

    // Note: all of the attributes are included here explicitly,
    // rather than using "default:", so that it's a compiler error to
    // add a new attribute without explicitly considering whether it's
    // a node id attribute or not.
    case ax::mojom::IntAttribute::kNone:
    case ax::mojom::IntAttribute::kDefaultActionVerb:
    case ax::mojom::IntAttribute::kScrollX:
    case ax::mojom::IntAttribute::kScrollXMin:
    case ax::mojom::IntAttribute::kScrollXMax:
    case ax::mojom::IntAttribute::kScrollY:
    case ax::mojom::IntAttribute::kScrollYMin:
    case ax::mojom::IntAttribute::kScrollYMax:
    case ax::mojom::IntAttribute::kTextSelStart:
    case ax::mojom::IntAttribute::kTextSelEnd:
    case ax::mojom::IntAttribute::kTableRowCount:
    case ax::mojom::IntAttribute::kTableColumnCount:
    case ax::mojom::IntAttribute::kTableRowIndex:
    case ax::mojom::IntAttribute::kTableColumnIndex:
    case ax::mojom::IntAttribute::kTableCellColumnIndex:
    case ax::mojom::IntAttribute::kTableCellColumnSpan:
    case ax::mojom::IntAttribute::kTableCellRowIndex:
    case ax::mojom::IntAttribute::kTableCellRowSpan:
    case ax::mojom::IntAttribute::kSortDirection:
    case ax::mojom::IntAttribute::kHierarchicalLevel:
    case ax::mojom::IntAttribute::kNameFrom:
    case ax::mojom::IntAttribute::kDescriptionFrom:
    case ax::mojom::IntAttribute::kSetSize:
    case ax::mojom::IntAttribute::kPosInSet:
    case ax::mojom::IntAttribute::kColorValue:
    case ax::mojom::IntAttribute::kAriaCurrentState:
    case ax::mojom::IntAttribute::kHasPopup:
    case ax::mojom::IntAttribute::kBackgroundColor:
    case ax::mojom::IntAttribute::kColor:
    case ax::mojom::IntAttribute::kInvalidState:
    case ax::mojom::IntAttribute::kCheckedState:
    case ax::mojom::IntAttribute::kRestriction:
    case ax::mojom::IntAttribute::kTextDirection:
    case ax::mojom::IntAttribute::kTextPosition:
    case ax::mojom::IntAttribute::kTextStyle:
    case ax::mojom::IntAttribute::kAriaColumnCount:
    case ax::mojom::IntAttribute::kAriaCellColumnIndex:
    case ax::mojom::IntAttribute::kAriaRowCount:
    case ax::mojom::IntAttribute::kAriaCellRowIndex:
      return false;
  }

  NOTREACHED();
  return false;
}

// Return true if |attr| contains a vector of node ids that would need
// to be mapped when renumbering the ids in a combined tree.
bool IsNodeIdIntListAttribute(ax::mojom::IntListAttribute attr) {
  switch (attr) {
    case ax::mojom::IntListAttribute::kControlsIds:
    case ax::mojom::IntListAttribute::kDescribedbyIds:
    case ax::mojom::IntListAttribute::kFlowtoIds:
    case ax::mojom::IntListAttribute::kIndirectChildIds:
    case ax::mojom::IntListAttribute::kLabelledbyIds:
    case ax::mojom::IntListAttribute::kRadioGroupIds:
      return true;

    // Note: all of the attributes are included here explicitly,
    // rather than using "default:", so that it's a compiler error to
    // add a new attribute without explicitly considering whether it's
    // a node id attribute or not.
    case ax::mojom::IntListAttribute::kNone:
    case ax::mojom::IntListAttribute::kMarkerTypes:
    case ax::mojom::IntListAttribute::kMarkerStarts:
    case ax::mojom::IntListAttribute::kMarkerEnds:
    case ax::mojom::IntListAttribute::kCharacterOffsets:
    case ax::mojom::IntListAttribute::kCachedLineStarts:
    case ax::mojom::IntListAttribute::kWordStarts:
    case ax::mojom::IntListAttribute::kWordEnds:
    case ax::mojom::IntListAttribute::kCustomActionIds:
      return false;
  }

  NOTREACHED();
  return false;
}

AXNodeData::AXNodeData() = default;
AXNodeData::~AXNodeData() = default;

AXNodeData::AXNodeData(const AXNodeData& other) {
  id = other.id;
  role = other.role;
  state = other.state;
  actions = other.actions;
  string_attributes = other.string_attributes;
  int_attributes = other.int_attributes;
  float_attributes = other.float_attributes;
  bool_attributes = other.bool_attributes;
  intlist_attributes = other.intlist_attributes;
  stringlist_attributes = other.stringlist_attributes;
  html_attributes = other.html_attributes;
  child_ids = other.child_ids;
  relative_bounds = other.relative_bounds;
}

AXNodeData& AXNodeData::operator=(AXNodeData other) {
  id = other.id;
  role = other.role;
  state = other.state;
  actions = other.actions;
  string_attributes = other.string_attributes;
  int_attributes = other.int_attributes;
  float_attributes = other.float_attributes;
  bool_attributes = other.bool_attributes;
  intlist_attributes = other.intlist_attributes;
  stringlist_attributes = other.stringlist_attributes;
  html_attributes = other.html_attributes;
  child_ids = other.child_ids;
  relative_bounds = other.relative_bounds;
  return *this;
}

bool AXNodeData::HasBoolAttribute(ax::mojom::BoolAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, bool_attributes);
  return iter != bool_attributes.end();
}

bool AXNodeData::GetBoolAttribute(ax::mojom::BoolAttribute attribute) const {
  bool result;
  if (GetBoolAttribute(attribute, &result))
    return result;
  return false;
}

bool AXNodeData::GetBoolAttribute(ax::mojom::BoolAttribute attribute,
                                  bool* value) const {
  auto iter = FindInVectorOfPairs(attribute, bool_attributes);
  if (iter != bool_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasFloatAttribute(ax::mojom::FloatAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, float_attributes);
  return iter != float_attributes.end();
}

float AXNodeData::GetFloatAttribute(ax::mojom::FloatAttribute attribute) const {
  float result;
  if (GetFloatAttribute(attribute, &result))
    return result;
  return 0.0;
}

bool AXNodeData::GetFloatAttribute(ax::mojom::FloatAttribute attribute,
                                   float* value) const {
  auto iter = FindInVectorOfPairs(attribute, float_attributes);
  if (iter != float_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasIntAttribute(ax::mojom::IntAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, int_attributes);
  return iter != int_attributes.end();
}

int AXNodeData::GetIntAttribute(ax::mojom::IntAttribute attribute) const {
  int result;
  if (GetIntAttribute(attribute, &result))
    return result;
  return 0;
}

bool AXNodeData::GetIntAttribute(ax::mojom::IntAttribute attribute,
                                 int* value) const {
  auto iter = FindInVectorOfPairs(attribute, int_attributes);
  if (iter != int_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasStringAttribute(
    ax::mojom::StringAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  return iter != string_attributes.end();
}

const std::string& AXNodeData::GetStringAttribute(
    ax::mojom::StringAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  return iter != string_attributes.end() ? iter->second : base::EmptyString();
}

bool AXNodeData::GetStringAttribute(ax::mojom::StringAttribute attribute,
                                    std::string* value) const {
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  if (iter != string_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

base::string16 AXNodeData::GetString16Attribute(
    ax::mojom::StringAttribute attribute) const {
  std::string value_utf8;
  if (!GetStringAttribute(attribute, &value_utf8))
    return base::string16();
  return base::UTF8ToUTF16(value_utf8);
}

bool AXNodeData::GetString16Attribute(ax::mojom::StringAttribute attribute,
                                      base::string16* value) const {
  std::string value_utf8;
  if (!GetStringAttribute(attribute, &value_utf8))
    return false;
  *value = base::UTF8ToUTF16(value_utf8);
  return true;
}

bool AXNodeData::HasIntListAttribute(
    ax::mojom::IntListAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  return iter != intlist_attributes.end();
}

const std::vector<int32_t>& AXNodeData::GetIntListAttribute(
    ax::mojom::IntListAttribute attribute) const {
  static const base::NoDestructor<std::vector<int32_t>> empty_vector;
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  if (iter != intlist_attributes.end())
    return iter->second;
  return *empty_vector;
}

bool AXNodeData::GetIntListAttribute(ax::mojom::IntListAttribute attribute,
                                     std::vector<int32_t>* value) const {
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  if (iter != intlist_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasStringListAttribute(
    ax::mojom::StringListAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, stringlist_attributes);
  return iter != stringlist_attributes.end();
}

const std::vector<std::string>& AXNodeData::GetStringListAttribute(
    ax::mojom::StringListAttribute attribute) const {
  static const base::NoDestructor<std::vector<std::string>> empty_vector;
  auto iter = FindInVectorOfPairs(attribute, stringlist_attributes);
  if (iter != stringlist_attributes.end())
    return iter->second;
  return *empty_vector;
}

bool AXNodeData::GetStringListAttribute(
    ax::mojom::StringListAttribute attribute,
    std::vector<std::string>* value) const {
  auto iter = FindInVectorOfPairs(attribute, stringlist_attributes);
  if (iter != stringlist_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::GetHtmlAttribute(
    const char* html_attr, std::string* value) const {
  for (const std::pair<std::string, std::string>& html_attribute :
       html_attributes) {
    const std::string& attr = html_attribute.first;
    if (base::LowerCaseEqualsASCII(attr, html_attr)) {
      *value = html_attribute.second;
      return true;
    }
  }

  return false;
}

bool AXNodeData::GetHtmlAttribute(
    const char* html_attr, base::string16* value) const {
  std::string value_utf8;
  if (!GetHtmlAttribute(html_attr, &value_utf8))
    return false;
  *value = base::UTF8ToUTF16(value_utf8);
  return true;
}

void AXNodeData::AddStringAttribute(ax::mojom::StringAttribute attribute,
                                    const std::string& value) {
  string_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddIntAttribute(ax::mojom::IntAttribute attribute, int value) {
  int_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::RemoveIntAttribute(ax::mojom::IntAttribute attribute) {
  DCHECK_GE(static_cast<int>(attribute), 0);
  base::EraseIf(int_attributes, [attribute](const auto& int_attribute) {
    return int_attribute.first == attribute;
  });
}

void AXNodeData::AddFloatAttribute(ax::mojom::FloatAttribute attribute,
                                   float value) {
  float_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddBoolAttribute(ax::mojom::BoolAttribute attribute,
                                  bool value) {
  bool_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddIntListAttribute(ax::mojom::IntListAttribute attribute,
                                     const std::vector<int32_t>& value) {
  intlist_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddStringListAttribute(
    ax::mojom::StringListAttribute attribute,
    const std::vector<std::string>& value) {
  stringlist_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::SetName(const std::string& name) {
  auto iter = std::find_if(string_attributes.begin(), string_attributes.end(),
                           [](const auto& string_attribute) {
                             return string_attribute.first ==
                                    ax::mojom::StringAttribute::kName;
                           });
  if (iter == string_attributes.end()) {
    string_attributes.push_back(
        std::make_pair(ax::mojom::StringAttribute::kName, name));
  } else {
    iter->second = name;
  }
}

void AXNodeData::SetName(const base::string16& name) {
  SetName(base::UTF16ToUTF8(name));
}

void AXNodeData::SetNameExplicitlyEmpty() {
  SetNameFrom(ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

void AXNodeData::SetDescription(const std::string& description) {
  auto iter = std::find_if(string_attributes.begin(), string_attributes.end(),
                           [](const auto& string_attribute) {
                             return string_attribute.first ==
                                    ax::mojom::StringAttribute::kDescription;
                           });
  if (iter == string_attributes.end()) {
    string_attributes.push_back(
        std::make_pair(ax::mojom::StringAttribute::kDescription, description));
  } else {
    iter->second = description;
  }
}

void AXNodeData::SetDescription(const base::string16& description) {
  SetDescription(base::UTF16ToUTF8(description));
}

void AXNodeData::SetValue(const std::string& value) {
  auto iter = std::find_if(string_attributes.begin(), string_attributes.end(),
                           [](const auto& string_attribute) {
                             return string_attribute.first ==
                                    ax::mojom::StringAttribute::kValue;
                           });
  if (iter == string_attributes.end()) {
    string_attributes.push_back(
        std::make_pair(ax::mojom::StringAttribute::kValue, value));
  } else {
    iter->second = value;
  }
}

void AXNodeData::SetValue(const base::string16& value) {
  SetValue(base::UTF16ToUTF8(value));
}

bool AXNodeData::HasState(ax::mojom::State state_enum) const {
  return IsFlagSet(state, static_cast<uint32_t>(state_enum));
}

bool AXNodeData::HasAction(ax::mojom::Action action) const {
  return IsFlagSet(actions, static_cast<uint32_t>(action));
}

bool AXNodeData::HasTextStyle(ax::mojom::TextStyle text_style_enum) const {
  int32_t style = GetIntAttribute(ax::mojom::IntAttribute::kTextStyle);
  return IsFlagSet(style, static_cast<uint32_t>(text_style_enum));
}

ax::mojom::State AXNodeData::AddState(ax::mojom::State state_enum) {
  DCHECK_GT(static_cast<int>(state_enum),
            static_cast<int>(ax::mojom::State::kNone));
  DCHECK_LE(static_cast<int>(state_enum),
            static_cast<int>(ax::mojom::State::kMaxValue));
  state = ModifyFlag(state, static_cast<uint32_t>(state_enum), true);
  return static_cast<ax::mojom::State>(state);
}

ax::mojom::State AXNodeData::RemoveState(ax::mojom::State state_enum) {
  DCHECK_GT(static_cast<int>(state_enum),
            static_cast<int>(ax::mojom::State::kNone));
  DCHECK_LE(static_cast<int>(state_enum),
            static_cast<int>(ax::mojom::State::kMaxValue));
  state = ModifyFlag(state, static_cast<uint32_t>(state_enum), false);
  return static_cast<ax::mojom::State>(state);
}

ax::mojom::Action AXNodeData::AddAction(ax::mojom::Action action_enum) {
  switch (action_enum) {
    case ax::mojom::Action::kNone:
      NOTREACHED();
      break;

    // Note: all of the attributes are included here explicitly, rather than
    // using "default:", so that it's a compiler error to add a new action
    // without explicitly considering whether there are mutually exclusive
    // actions that can be performed on a UI control at the same time.
    case ax::mojom::Action::kBlur:
    case ax::mojom::Action::kFocus: {
      ax::mojom::Action excluded_action =
          (action_enum == ax::mojom::Action::kBlur) ? ax::mojom::Action::kFocus
                                                    : ax::mojom::Action::kBlur;
      DCHECK(HasAction(excluded_action));
    } break;
    case ax::mojom::Action::kClearAccessibilityFocus:
    case ax::mojom::Action::kCustomAction:
    case ax::mojom::Action::kDecrement:
    case ax::mojom::Action::kDoDefault:
    case ax::mojom::Action::kGetImageData:
    case ax::mojom::Action::kHitTest:
    case ax::mojom::Action::kIncrement:
    case ax::mojom::Action::kLoadInlineTextBoxes:
    case ax::mojom::Action::kReplaceSelectedText:
    case ax::mojom::Action::kScrollToMakeVisible:
    case ax::mojom::Action::kScrollToPoint:
    case ax::mojom::Action::kSetAccessibilityFocus:
    case ax::mojom::Action::kSetScrollOffset:
    case ax::mojom::Action::kSetSelection:
    case ax::mojom::Action::kSetSequentialFocusNavigationStartingPoint:
    case ax::mojom::Action::kSetValue:
    case ax::mojom::Action::kShowContextMenu:
    case ax::mojom::Action::kScrollBackward:
    case ax::mojom::Action::kScrollForward:
    case ax::mojom::Action::kScrollUp:
    case ax::mojom::Action::kScrollDown:
    case ax::mojom::Action::kScrollLeft:
    case ax::mojom::Action::kScrollRight:
    case ax::mojom::Action::kGetTextLocation:
      break;
  }

  actions = ModifyFlag(actions, static_cast<uint32_t>(action_enum), true);
  return static_cast<ax::mojom::Action>(actions);
}

void AXNodeData::AddTextStyle(ax::mojom::TextStyle text_style_enum) {
  DCHECK_GE(static_cast<int>(text_style_enum),
            static_cast<int>(ax::mojom::TextStyle::kMinValue));
  DCHECK_LE(static_cast<int>(text_style_enum),
            static_cast<int>(ax::mojom::TextStyle::kMaxValue));
  int32_t style = GetIntAttribute(ax::mojom::IntAttribute::kTextStyle);
  style = ModifyFlag(style, static_cast<uint32_t>(text_style_enum), true);
  RemoveIntAttribute(ax::mojom::IntAttribute::kTextStyle);
  AddIntAttribute(ax::mojom::IntAttribute::kTextStyle, style);
}

ax::mojom::CheckedState AXNodeData::GetCheckedState() const {
  return static_cast<ax::mojom::CheckedState>(
      GetIntAttribute(ax::mojom::IntAttribute::kCheckedState));
}

void AXNodeData::SetCheckedState(ax::mojom::CheckedState checked_state) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kCheckedState))
    RemoveIntAttribute(ax::mojom::IntAttribute::kCheckedState);
  if (checked_state != ax::mojom::CheckedState::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kCheckedState,
                    static_cast<int32_t>(checked_state));
  }
}

ax::mojom::DefaultActionVerb AXNodeData::GetDefaultActionVerb() const {
  return static_cast<ax::mojom::DefaultActionVerb>(
      GetIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));
}

void AXNodeData::SetDefaultActionVerb(
    ax::mojom::DefaultActionVerb default_action_verb) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb))
    RemoveIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb);
  if (default_action_verb != ax::mojom::DefaultActionVerb::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb,
                    static_cast<int32_t>(default_action_verb));
  }
}

ax::mojom::HasPopup AXNodeData::GetHasPopup() const {
  return static_cast<ax::mojom::HasPopup>(
      GetIntAttribute(ax::mojom::IntAttribute::kHasPopup));
}

void AXNodeData::SetHasPopup(ax::mojom::HasPopup has_popup) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kHasPopup))
    RemoveIntAttribute(ax::mojom::IntAttribute::kHasPopup);
  if (has_popup != ax::mojom::HasPopup::kFalse) {
    AddIntAttribute(ax::mojom::IntAttribute::kHasPopup,
                    static_cast<int32_t>(has_popup));
  }
}

ax::mojom::InvalidState AXNodeData::GetInvalidState() const {
  return static_cast<ax::mojom::InvalidState>(
      GetIntAttribute(ax::mojom::IntAttribute::kInvalidState));
}

void AXNodeData::SetInvalidState(ax::mojom::InvalidState invalid_state) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kInvalidState))
    RemoveIntAttribute(ax::mojom::IntAttribute::kInvalidState);
  if (invalid_state != ax::mojom::InvalidState::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kInvalidState,
                    static_cast<int32_t>(invalid_state));
  }
}

ax::mojom::NameFrom AXNodeData::GetNameFrom() const {
  return static_cast<ax::mojom::NameFrom>(
      GetIntAttribute(ax::mojom::IntAttribute::kNameFrom));
}

void AXNodeData::SetNameFrom(ax::mojom::NameFrom name_from) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kNameFrom))
    RemoveIntAttribute(ax::mojom::IntAttribute::kNameFrom);
  if (name_from != ax::mojom::NameFrom::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kNameFrom,
                    static_cast<int32_t>(name_from));
  }
}

ax::mojom::TextPosition AXNodeData::GetTextPosition() const {
  return static_cast<ax::mojom::TextPosition>(
      GetIntAttribute(ax::mojom::IntAttribute::kTextPosition));
}

void AXNodeData::SetTextPosition(ax::mojom::TextPosition text_position) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kTextPosition))
    RemoveIntAttribute(ax::mojom::IntAttribute::kTextPosition);
  if (text_position != ax::mojom::TextPosition::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kTextPosition,
                    static_cast<int32_t>(text_position));
  }
}

ax::mojom::Restriction AXNodeData::GetRestriction() const {
  return static_cast<ax::mojom::Restriction>(
      GetIntAttribute(ax::mojom::IntAttribute::kRestriction));
}

void AXNodeData::SetRestriction(ax::mojom::Restriction restriction) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kRestriction))
    RemoveIntAttribute(ax::mojom::IntAttribute::kRestriction);
  if (restriction != ax::mojom::Restriction::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kRestriction,
                    static_cast<int32_t>(restriction));
  }
}

ax::mojom::TextDirection AXNodeData::GetTextDirection() const {
  return static_cast<ax::mojom::TextDirection>(
      GetIntAttribute(ax::mojom::IntAttribute::kTextDirection));
}

void AXNodeData::SetTextDirection(ax::mojom::TextDirection text_direction) {
  if (HasIntAttribute(ax::mojom::IntAttribute::kTextDirection))
    RemoveIntAttribute(ax::mojom::IntAttribute::kTextDirection);
  if (text_direction != ax::mojom::TextDirection::kNone) {
    AddIntAttribute(ax::mojom::IntAttribute::kTextDirection,
                    static_cast<int32_t>(text_direction));
  }
}

std::string AXNodeData::ToString() const {
  std::string result;

  result += "id=" + base::NumberToString(id);
  result += " ";
  result += ui::ToString(role);

  result += StateBitfieldToString(state);

  result += " (" + base::NumberToString(relative_bounds.bounds.x()) + ", " +
            base::NumberToString(relative_bounds.bounds.y()) + ")-(" +
            base::NumberToString(relative_bounds.bounds.width()) + ", " +
            base::NumberToString(relative_bounds.bounds.height()) + ")";

  if (relative_bounds.offset_container_id != -1)
    result += " offset_container_id=" +
              base::NumberToString(relative_bounds.offset_container_id);

  if (relative_bounds.transform && !relative_bounds.transform->IsIdentity())
    result += " transform=" + relative_bounds.transform->ToString();

  for (const std::pair<ax::mojom::IntAttribute, int32_t>& int_attribute :
       int_attributes) {
    std::string value = base::NumberToString(int_attribute.second);
    switch (int_attribute.first) {
      case ax::mojom::IntAttribute::kDefaultActionVerb:
        result += " action=" + base::UTF16ToUTF8(ActionVerbToUnlocalizedString(
                                   static_cast<ax::mojom::DefaultActionVerb>(
                                       int_attribute.second)));
        break;
      case ax::mojom::IntAttribute::kScrollX:
        result += " scroll_x=" + value;
        break;
      case ax::mojom::IntAttribute::kScrollXMin:
        result += " scroll_x_min=" + value;
        break;
      case ax::mojom::IntAttribute::kScrollXMax:
        result += " scroll_x_max=" + value;
        break;
      case ax::mojom::IntAttribute::kScrollY:
        result += " scroll_y=" + value;
        break;
      case ax::mojom::IntAttribute::kScrollYMin:
        result += " scroll_y_min=" + value;
        break;
      case ax::mojom::IntAttribute::kScrollYMax:
        result += " scroll_y_max=" + value;
        break;
      case ax::mojom::IntAttribute::kHierarchicalLevel:
        result += " level=" + value;
        break;
      case ax::mojom::IntAttribute::kTextSelStart:
        result += " sel_start=" + value;
        break;
      case ax::mojom::IntAttribute::kTextSelEnd:
        result += " sel_end=" + value;
        break;
      case ax::mojom::IntAttribute::kAriaColumnCount:
        result += " aria_column_count=" + value;
        break;
      case ax::mojom::IntAttribute::kAriaCellColumnIndex:
        result += " aria_cell_column_index=" + value;
        break;
      case ax::mojom::IntAttribute::kAriaRowCount:
        result += " aria_row_count=" + value;
        break;
      case ax::mojom::IntAttribute::kAriaCellRowIndex:
        result += " aria_cell_row_index=" + value;
        break;
      case ax::mojom::IntAttribute::kTableRowCount:
        result += " rows=" + value;
        break;
      case ax::mojom::IntAttribute::kTableColumnCount:
        result += " cols=" + value;
        break;
      case ax::mojom::IntAttribute::kTableCellColumnIndex:
        result += " col=" + value;
        break;
      case ax::mojom::IntAttribute::kTableCellRowIndex:
        result += " row=" + value;
        break;
      case ax::mojom::IntAttribute::kTableCellColumnSpan:
        result += " colspan=" + value;
        break;
      case ax::mojom::IntAttribute::kTableCellRowSpan:
        result += " rowspan=" + value;
        break;
      case ax::mojom::IntAttribute::kTableColumnHeaderId:
        result += " column_header_id=" + value;
        break;
      case ax::mojom::IntAttribute::kTableColumnIndex:
        result += " column_index=" + value;
        break;
      case ax::mojom::IntAttribute::kTableHeaderId:
        result += " header_id=" + value;
        break;
      case ax::mojom::IntAttribute::kTableRowHeaderId:
        result += " row_header_id=" + value;
        break;
      case ax::mojom::IntAttribute::kTableRowIndex:
        result += " row_index=" + value;
        break;
      case ax::mojom::IntAttribute::kSortDirection:
        switch (static_cast<ax::mojom::SortDirection>(int_attribute.second)) {
          case ax::mojom::SortDirection::kUnsorted:
            result += " sort_direction=none";
            break;
          case ax::mojom::SortDirection::kAscending:
            result += " sort_direction=ascending";
            break;
          case ax::mojom::SortDirection::kDescending:
            result += " sort_direction=descending";
            break;
          case ax::mojom::SortDirection::kOther:
            result += " sort_direction=other";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kNameFrom:
        result += " name_from=";
        result += ui::ToString(
            static_cast<ax::mojom::NameFrom>(int_attribute.second));
        break;
      case ax::mojom::IntAttribute::kDescriptionFrom:
        result += " description_from=";
        result += ui::ToString(
            static_cast<ax::mojom::DescriptionFrom>(int_attribute.second));
        break;
      case ax::mojom::IntAttribute::kActivedescendantId:
        result += " activedescendant=" + value;
        break;
      case ax::mojom::IntAttribute::kDetailsId:
        result += " details=" + value;
        break;
      case ax::mojom::IntAttribute::kErrormessageId:
        result += " errormessage=" + value;
        break;
      case ax::mojom::IntAttribute::kInPageLinkTargetId:
        result += " in_page_link_target_id=" + value;
        break;
      case ax::mojom::IntAttribute::kMemberOfId:
        result += " member_of_id=" + value;
        break;
      case ax::mojom::IntAttribute::kNextOnLineId:
        result += " next_on_line_id=" + value;
        break;
      case ax::mojom::IntAttribute::kPreviousOnLineId:
        result += " previous_on_line_id=" + value;
        break;
      case ax::mojom::IntAttribute::kColorValue:
        result += base::StringPrintf(" color_value=&%X", int_attribute.second);
        break;
      case ax::mojom::IntAttribute::kAriaCurrentState:
        switch (
            static_cast<ax::mojom::AriaCurrentState>(int_attribute.second)) {
          case ax::mojom::AriaCurrentState::kFalse:
            result += " aria_current_state=false";
            break;
          case ax::mojom::AriaCurrentState::kTrue:
            result += " aria_current_state=true";
            break;
          case ax::mojom::AriaCurrentState::kPage:
            result += " aria_current_state=page";
            break;
          case ax::mojom::AriaCurrentState::kStep:
            result += " aria_current_state=step";
            break;
          case ax::mojom::AriaCurrentState::kLocation:
            result += " aria_current_state=location";
            break;
          case ax::mojom::AriaCurrentState::kDate:
            result += " aria_current_state=date";
            break;
          case ax::mojom::AriaCurrentState::kTime:
            result += " aria_current_state=time";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kBackgroundColor:
        result +=
            base::StringPrintf(" background_color=&%X", int_attribute.second);
        break;
      case ax::mojom::IntAttribute::kColor:
        result += base::StringPrintf(" color=&%X", int_attribute.second);
        break;
      case ax::mojom::IntAttribute::kTextDirection:
        switch (static_cast<ax::mojom::TextDirection>(int_attribute.second)) {
          case ax::mojom::TextDirection::kLtr:
            result += " text_direction=ltr";
            break;
          case ax::mojom::TextDirection::kRtl:
            result += " text_direction=rtl";
            break;
          case ax::mojom::TextDirection::kTtb:
            result += " text_direction=ttb";
            break;
          case ax::mojom::TextDirection::kBtt:
            result += " text_direction=btt";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kTextPosition:
        switch (static_cast<ax::mojom::TextPosition>(int_attribute.second)) {
          case ax::mojom::TextPosition::kNone:
            result += " text_position=none";
            break;
          case ax::mojom::TextPosition::kSubscript:
            result += " text_position=subscript";
            break;
          case ax::mojom::TextPosition::kSuperscript:
            result += " text_position=superscript";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kTextStyle: {
        std::string text_style_value;
        if (HasTextStyle(ax::mojom::TextStyle::kBold))
          text_style_value += "bold,";
        if (HasTextStyle(ax::mojom::TextStyle::kItalic))
          text_style_value += "italic,";
        if (HasTextStyle(ax::mojom::TextStyle::kUnderline))
          text_style_value += "underline,";
        if (HasTextStyle(ax::mojom::TextStyle::kLineThrough))
          text_style_value += "line-through,";
        result += text_style_value.substr(0, text_style_value.size() - 1);
        break;
      }
      case ax::mojom::IntAttribute::kSetSize:
        result += " setsize=" + value;
        break;
      case ax::mojom::IntAttribute::kPosInSet:
        result += " posinset=" + value;
        break;
      case ax::mojom::IntAttribute::kHasPopup:
        switch (static_cast<ax::mojom::HasPopup>(int_attribute.second)) {
          case ax::mojom::HasPopup::kTrue:
            result += " haspopup=true";
            break;
          case ax::mojom::HasPopup::kMenu:
            result += " haspopup=menu";
            break;
          case ax::mojom::HasPopup::kListbox:
            result += " haspopup=listbox";
            break;
          case ax::mojom::HasPopup::kTree:
            result += " haspopup=tree";
            break;
          case ax::mojom::HasPopup::kGrid:
            result += " haspopup=grid";
            break;
          case ax::mojom::HasPopup::kDialog:
            result += " haspopup=dialog";
            break;
          case ax::mojom::HasPopup::kFalse:
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kInvalidState:
        switch (static_cast<ax::mojom::InvalidState>(int_attribute.second)) {
          case ax::mojom::InvalidState::kFalse:
            result += " invalid_state=false";
            break;
          case ax::mojom::InvalidState::kTrue:
            result += " invalid_state=true";
            break;
          case ax::mojom::InvalidState::kSpelling:
            result += " invalid_state=spelling";
            break;
          case ax::mojom::InvalidState::kGrammar:
            result += " invalid_state=grammar";
            break;
          case ax::mojom::InvalidState::kOther:
            result += " invalid_state=other";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kCheckedState:
        switch (static_cast<ax::mojom::CheckedState>(int_attribute.second)) {
          case ax::mojom::CheckedState::kFalse:
            result += " checked_state=false";
            break;
          case ax::mojom::CheckedState::kTrue:
            result += " checked_state=true";
            break;
          case ax::mojom::CheckedState::kMixed:
            result += " checked_state=mixed";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kRestriction:
        switch (static_cast<ax::mojom::Restriction>(int_attribute.second)) {
          case ax::mojom::Restriction::kReadOnly:
            result += " restriction=readonly";
            break;
          case ax::mojom::Restriction::kDisabled:
            result += " restriction=disabled";
            break;
          default:
            break;
        }
        break;
      case ax::mojom::IntAttribute::kNextFocusId:
        result += " next_focus_id=" + value;
        break;
      case ax::mojom::IntAttribute::kPreviousFocusId:
        result += " previous_focus_id=" + value;
        break;
      case ax::mojom::IntAttribute::kNone:
        break;
    }
  }

  for (const std::pair<ax::mojom::StringAttribute, std::string>&
           string_attribute : string_attributes) {
    std::string value = string_attribute.second;
    switch (string_attribute.first) {
      case ax::mojom::StringAttribute::kAccessKey:
        result += " access_key=" + value;
        break;
      case ax::mojom::StringAttribute::kAriaInvalidValue:
        result += " aria_invalid_value=" + value;
        break;
      case ax::mojom::StringAttribute::kAutoComplete:
        result += " autocomplete=" + value;
        break;
      case ax::mojom::StringAttribute::kChildTreeId:
        result += " child_tree_id=" + value;
        break;
      case ax::mojom::StringAttribute::kClassName:
        result += " class_name=" + value;
        break;
      case ax::mojom::StringAttribute::kDescription:
        result += " description=" + value;
        break;
      case ax::mojom::StringAttribute::kDisplay:
        result += " display=" + value;
        break;
      case ax::mojom::StringAttribute::kFontFamily:
        result += " font-family=" + value;
        break;
      case ax::mojom::StringAttribute::kHtmlTag:
        result += " html_tag=" + value;
        break;
      case ax::mojom::StringAttribute::kImageDataUrl:
        result += " image_data_url=(" +
                  base::NumberToString(static_cast<int>(value.size())) +
                  " bytes)";
        break;
      case ax::mojom::StringAttribute::kInnerHtml:
        result += " inner_html=" + value;
        break;
      case ax::mojom::StringAttribute::kKeyShortcuts:
        result += " key_shortcuts=" + value;
        break;
      case ax::mojom::StringAttribute::kLanguage:
        result += " language=" + value;
        break;
      case ax::mojom::StringAttribute::kLiveRelevant:
        result += " relevant=" + value;
        break;
      case ax::mojom::StringAttribute::kLiveStatus:
        result += " live=" + value;
        break;
      case ax::mojom::StringAttribute::kContainerLiveRelevant:
        result += " container_relevant=" + value;
        break;
      case ax::mojom::StringAttribute::kContainerLiveStatus:
        result += " container_live=" + value;
        break;
      case ax::mojom::StringAttribute::kPlaceholder:
        result += " placeholder=" + value;
        break;
      case ax::mojom::StringAttribute::kRole:
        result += " role=" + value;
        break;
      case ax::mojom::StringAttribute::kRoleDescription:
        result += " role_description=" + value;
        break;
      case ax::mojom::StringAttribute::kUrl:
        result += " url=" + value;
        break;
      case ax::mojom::StringAttribute::kName:
        result += " name=" + value;
        break;
      case ax::mojom::StringAttribute::kValue:
        result += " value=" + value;
        break;
      case ax::mojom::StringAttribute::kNone:
        break;
    }
  }

  for (const std::pair<ax::mojom::FloatAttribute, float>& float_attribute :
       float_attributes) {
    std::string value = base::NumberToString(float_attribute.second);
    switch (float_attribute.first) {
      case ax::mojom::FloatAttribute::kValueForRange:
        result += " value_for_range=" + value;
        break;
      case ax::mojom::FloatAttribute::kMaxValueForRange:
        result += " max_value=" + value;
        break;
      case ax::mojom::FloatAttribute::kMinValueForRange:
        result += " min_value=" + value;
        break;
      case ax::mojom::FloatAttribute::kStepValueForRange:
        result += " step_value=" + value;
        break;
      case ax::mojom::FloatAttribute::kFontSize:
        result += " font_size=" + value;
        break;
      case ax::mojom::FloatAttribute::kNone:
        break;
    }
  }

  for (const std::pair<ax::mojom::BoolAttribute, bool>& bool_attribute :
       bool_attributes) {
    std::string value = bool_attribute.second ? "true" : "false";
    switch (bool_attribute.first) {
      case ax::mojom::BoolAttribute::kEditableRoot:
        result += " editable_root=" + value;
        break;
      case ax::mojom::BoolAttribute::kLiveAtomic:
        result += " atomic=" + value;
        break;
      case ax::mojom::BoolAttribute::kBusy:
        result += " busy=" + value;
        break;
      case ax::mojom::BoolAttribute::kContainerLiveAtomic:
        result += " container_atomic=" + value;
        break;
      case ax::mojom::BoolAttribute::kContainerLiveBusy:
        result += " container_busy=" + value;
        break;
      case ax::mojom::BoolAttribute::kUpdateLocationOnly:
        result += " update_location_only=" + value;
        break;
      case ax::mojom::BoolAttribute::kCanvasHasFallback:
        result += " has_fallback=" + value;
        break;
      case ax::mojom::BoolAttribute::kModal:
        result += " modal=" + value;
        break;
      case ax::mojom::BoolAttribute::kScrollable:
        result += " scrollable=" + value;
        break;
      case ax::mojom::BoolAttribute::kClickable:
        result += " clickable=" + value;
        break;
      case ax::mojom::BoolAttribute::kClipsChildren:
        result += " clips_children=" + value;
        break;
      case ax::mojom::BoolAttribute::kSelected:
        result += " selected=" + value;
        break;
      case ax::mojom::BoolAttribute::kSupportsTextLocation:
        result += " supports_text_location=" + value;
        break;
      case ax::mojom::BoolAttribute::kNone:
        break;
    }
  }

  for (const std::pair<ax::mojom::IntListAttribute, std::vector<int32_t>>&
           intlist_attribute : intlist_attributes) {
    const std::vector<int32_t>& values = intlist_attribute.second;
    switch (intlist_attribute.first) {
      case ax::mojom::IntListAttribute::kIndirectChildIds:
        result += " indirect_child_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kControlsIds:
        result += " controls_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kDescribedbyIds:
        result += " describedby_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kFlowtoIds:
        result += " flowto_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kLabelledbyIds:
        result += " labelledby_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kRadioGroupIds:
        result += " radio_group_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kMarkerTypes: {
        std::string types_str;
        for (size_t i = 0; i < values.size(); ++i) {
          int32_t type = values[i];
          if (type == static_cast<int32_t>(ax::mojom::MarkerType::kNone))
            continue;

          if (i > 0)
            types_str += ',';

          if (type & static_cast<int32_t>(ax::mojom::MarkerType::kSpelling))
            types_str += "spelling&";
          if (type & static_cast<int32_t>(ax::mojom::MarkerType::kGrammar))
            types_str += "grammar&";
          if (type & static_cast<int32_t>(ax::mojom::MarkerType::kTextMatch))
            types_str += "text_match&";
          if (type &
              static_cast<int32_t>(ax::mojom::MarkerType::kActiveSuggestion))
            types_str += "active_suggestion&";
          if (type & static_cast<int32_t>(ax::mojom::MarkerType::kSuggestion))
            types_str += "suggestion&";

          if (!types_str.empty())
            types_str = types_str.substr(0, types_str.size() - 1);
        }

        if (!types_str.empty())
          result += " marker_types=" + types_str;
        break;
      }
      case ax::mojom::IntListAttribute::kMarkerStarts:
        result += " marker_starts=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kMarkerEnds:
        result += " marker_ends=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kCharacterOffsets:
        result += " character_offsets=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kCachedLineStarts:
        result += " cached_line_start_offsets=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kWordStarts:
        result += " word_starts=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kWordEnds:
        result += " word_ends=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kCustomActionIds:
        result += " custom_action_ids=" + IntVectorToString(values);
        break;
      case ax::mojom::IntListAttribute::kNone:
        break;
    }
  }

  for (const std::pair<ax::mojom::StringListAttribute,
                       std::vector<std::string>>& stringlist_attribute :
       stringlist_attributes) {
    const std::vector<std::string>& values = stringlist_attribute.second;
    switch (stringlist_attribute.first) {
      case ax::mojom::StringListAttribute::kCustomActionDescriptions:
        result +=
            " custom_action_descriptions: " + base::JoinString(values, ",");
        break;
      case ax::mojom::StringListAttribute::kNone:
        break;
    }
  }

  result += " actions=" + ActionsBitfieldToString(actions);

  if (!child_ids.empty())
    result += " child_ids=" + IntVectorToString(child_ids);

  return result;
}

}  // namespace ui
