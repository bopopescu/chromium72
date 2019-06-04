// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/input/actions_parser.h"

#include "base/format_macros.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"

namespace content {

namespace {

SyntheticPointerActionParams::PointerActionType ToSyntheticPointerActionType(
    std::string action_type) {
  if (action_type == "pointerDown")
    return SyntheticPointerActionParams::PointerActionType::PRESS;
  if (action_type == "pointerMove")
    return SyntheticPointerActionParams::PointerActionType::MOVE;
  if (action_type == "pointerUp")
    return SyntheticPointerActionParams::PointerActionType::RELEASE;
  if (action_type == "pointerLeave")
    return SyntheticPointerActionParams::PointerActionType::LEAVE;
  if (action_type == "pause")
    return SyntheticPointerActionParams::PointerActionType::IDLE;
  return SyntheticPointerActionParams::PointerActionType::NOT_INITIALIZED;
}

SyntheticGestureParams::GestureSourceType ToSyntheticGestureSourceType(
    std::string pointer_type) {
  if (pointer_type == "touch")
    return SyntheticGestureParams::TOUCH_INPUT;
  else if (pointer_type == "mouse")
    return SyntheticGestureParams::MOUSE_INPUT;
  else if (pointer_type == "pen")
    return SyntheticGestureParams::PEN_INPUT;
  else
    return SyntheticGestureParams::DEFAULT_INPUT;
}

SyntheticPointerActionParams::Button ToSyntheticMouseButton(int button) {
  if (button == 0)
    return SyntheticPointerActionParams::Button::LEFT;
  if (button == 1)
    return SyntheticPointerActionParams::Button::MIDDLE;
  if (button == 2)
    return SyntheticPointerActionParams::Button::RIGHT;
  if (button == 3)
    return SyntheticPointerActionParams::Button::BACK;
  if (button == 4)
    return SyntheticPointerActionParams::Button::FORWARD;
  NOTREACHED() << "Unexpected button";
  return SyntheticPointerActionParams::Button();
}

}  // namespace

ActionsParser::ActionsParser(base::Value* pointer_actions_value)
    : longest_action_sequence_(0),
      pointer_actions_value_(pointer_actions_value),
      action_index_(0) {}

ActionsParser::~ActionsParser() {}

bool ActionsParser::ParsePointerActionSequence() {
  const base::ListValue* pointer_list;
  if (!pointer_actions_value_ ||
      !pointer_actions_value_->GetAsList(&pointer_list)) {
    error_message_ = std::string("pointer_list is missing or not a list");
    return false;
  }

  for (const auto& pointer_value : *pointer_list) {
    const base::DictionaryValue* pointer_actions;
    if (!pointer_value.GetAsDictionary(&pointer_actions)) {
      error_message_ =
          std::string("pointer actions is missing or not a dictionary");
      return false;
    } else if (!ParsePointerActions(*pointer_actions)) {
      return false;
    }
    action_index_++;
  }

  gesture_params_.gesture_source_type =
      ToSyntheticGestureSourceType(pointer_type_);
  // Group a list of actions from all pointers into a
  // SyntheticPointerActionListParams object, which is a list of actions, which
  // will be dispatched together.
  for (size_t action_index = 0; action_index < longest_action_sequence_;
       ++action_index) {
    SyntheticPointerActionListParams::ParamList param_list;
    for (const auto pointer_action_list : pointer_actions_list_) {
      if (action_index < pointer_action_list.size())
        param_list.push_back(pointer_action_list[action_index]);
    }
    gesture_params_.PushPointerActionParamsList(param_list);
  }

  return true;
}

bool ActionsParser::ParsePointerActions(const base::DictionaryValue& pointer) {
  int pointer_id = -1;
  // If the json format of each pointer has "type" element, it is from the new
  // Action API, otherwise it is from gpuBenchmarking.pointerActionSequence
  // API. We have to keep both formats for now, but later on once we switch to
  // the new Action API in all tests, we will remove the old format.
  if (pointer.HasKey("type")) {
    std::string source_type;
    if (!pointer.GetString("type", &source_type)) {
      error_message_ =
          std::string("action sequence type is missing or not a string");
      return false;
    } else if (source_type == "") {
      error_message_ = std::string("action sequence type cannot be empty");
      return false;
    } else if (source_type != "pointer") {
      error_message_ =
          std::string("we only support action sequence type of pointer");
      return false;
    }

    if (source_type_.empty())
      source_type_ = source_type;

    if (source_type_ != source_type) {
      error_message_ = std::string(
          "currently multiple action sequence type are not supported");
      return false;
    }

    if (source_type_ == "pointer" && !pointer.HasKey("parameters")) {
      error_message_ =
          std::string("action sequence parameters is missing for pointer type");
      return false;
    }

    const base::DictionaryValue* parameters;
    std::string pointer_type;
    if (!pointer.GetDictionary("parameters", &parameters)) {
      error_message_ =
          std::string("action sequence parameters is not a dictionary");
      return false;
    }

    if (!parameters->GetString("pointerType", &pointer_type)) {
      error_message_ = std::string(
          "action sequence pointer type is missing or not a string");
      return false;
    } else if (pointer_type != "touch" && pointer_type != "mouse" &&
               pointer_type != "pen") {
      error_message_ = std::string(
          "action sequence pointer type is an unsupported input type");
      return false;
    }

    if (pointer_type_.empty()) {
      pointer_type_ = pointer_type;
    }

    if (pointer_type_ != pointer_type) {
      error_message_ = std::string(
          "currently multiple action sequence pointer type are not supported");
      return false;
    }

    if (pointer_type != "touch" && action_index_ > 0) {
      error_message_ = std::string(
          "for input type of mouse and pen, we only support one device");
      return false;
    }

    std::string pointer_name;
    if (!pointer.GetString("id", &pointer_name)) {
      error_message_ = std::string("pointer name is missing or not a string");
      return false;
    }

    if (pointer_name_set_.find(pointer_name) != pointer_name_set_.end()) {
      error_message_ = std::string("pointer name already exists");
      return false;
    }

    pointer_name_set_.insert(pointer_name);
    pointer_id_set_.insert(action_index_);
    pointer_id = action_index_;
  } else {
    std::string pointer_type;
    if (!pointer.GetString("source", &pointer_type)) {
      error_message_ = std::string("source type is missing or not a string");
      return false;
    } else if (pointer_type != "touch" && pointer_type != "mouse" &&
               pointer_type != "pen") {
      error_message_ =
          std::string("source type is an unsupported input source");
      return false;
    }

    if (pointer_type_.empty()) {
      pointer_type_ = pointer_type;
    }

    if (pointer_type_ != pointer_type) {
      error_message_ =
          std::string("currently multiple input sources are not not supported");
      return false;
    }

    if (pointer_type != "touch" && action_index_ > 0) {
      error_message_ = std::string(
          "for input source type of mouse and pen, we only support one device "
          "in "
          "one sequence");
      return false;
    }

    if (pointer.HasKey("id")) {
      if (!pointer.GetInteger("id", &pointer_id)) {
        error_message_ = std::string("pointer id is not an integer");
        return false;
      }

      if (pointer_id < 0) {
        error_message_ = std::string("pointer id can not be negative");
        return false;
      }
    }

    if (pointer_id != -1) {
      if (pointer_id_set_.find(pointer_id) != pointer_id_set_.end()) {
        error_message_ = std::string("pointer id already exists");
        return false;
      }

      if (action_index_ != static_cast<int>(pointer_id_set_.size())) {
        error_message_ = std::string("some pointers do not have a pointer id");
        return false;
      }

      pointer_id_set_.insert(pointer_id);
    } else {
      if (pointer_id_set_.size() > 0) {
        error_message_ = std::string("this pointer does not have a pointer id");
        return false;
      }
    }
  }

  const base::ListValue* actions;
  if (!pointer.GetList("actions", &actions)) {
    error_message_ = base::StringPrintf(
        "pointer[%d].actions is missing or not a list", action_index_);
    return false;
  }

  if (!ParseActions(*actions, pointer_id))
    return false;

  return true;
}

bool ActionsParser::ParseActions(const base::ListValue& actions,
                                 int pointer_id) {
  SyntheticPointerActionListParams::ParamList param_list;
  for (const auto& action_value : actions) {
    const base::DictionaryValue* action;
    if (!action_value.GetAsDictionary(&action)) {
      error_message_ = base::StringPrintf(
          "actions[%d].actions is missing or not a dictionary", action_index_);
      return false;
    } else if (!ParseAction(*action, param_list, pointer_id)) {
      return false;
    }
  }

  if (param_list.size() > longest_action_sequence_)
    longest_action_sequence_ = param_list.size();

  pointer_actions_list_.push_back(param_list);
  return true;
}

bool ActionsParser::ParseAction(
    const base::DictionaryValue& action,
    SyntheticPointerActionListParams::ParamList& param_list,
    int pointer_id) {
  SyntheticPointerActionParams::PointerActionType pointer_action_type =
      SyntheticPointerActionParams::PointerActionType::NOT_INITIALIZED;
  std::string type;
  if (action.HasKey("type")) {
    if (!action.GetString("type", &type)) {
      error_message_ = base::StringPrintf(
          "actions[%d].actions.name is missing or not a string", action_index_);
      return false;
    }
  } else {
    if (!action.GetString("name", &type)) {
      error_message_ = base::StringPrintf(
          "actions[%d].actions.name is missing or not a string", action_index_);
      return false;
    }
  }
  pointer_action_type = ToSyntheticPointerActionType(type);
  if (pointer_action_type ==
      SyntheticPointerActionParams::PointerActionType::NOT_INITIALIZED) {
    error_message_ = base::StringPrintf(
        "actions[%d].actions.name is an unsupported action name",
        action_index_);
    return false;
  }

  double position_x = 0;
  double position_y = 0;
  if (action.HasKey("x") && !action.GetDouble("x", &position_x)) {
    error_message_ = base::StringPrintf("actions[%d].actions.x is not a number",
                                        action_index_);
    return false;
  }

  if (action.HasKey("y") && !action.GetDouble("y", &position_y)) {
    error_message_ = base::StringPrintf("actions[%d].actions.y is not a number",
                                        action_index_);
    return false;
  }

  int button_id = 0;
  if (action.HasKey("button") && !action.GetInteger("button", &button_id)) {
    error_message_ = base::StringPrintf(
        "actions[%d].actions.button is not a string", action_index_);
    return false;
  } else if (button_id < 0 || button_id > 4) {
    error_message_ = base::StringPrintf(
        "actions[%d].actions.button is an unsupported button", action_index_);
    return false;
  }
  SyntheticPointerActionParams::Button button =
      ToSyntheticMouseButton(button_id);

  double duration = 0;
  int num_idle = 0;
  if (pointer_action_type ==
      SyntheticPointerActionParams::PointerActionType::IDLE) {
    num_idle = 1;
    if (action.HasKey("duration") && !action.GetDouble("duration", &duration)) {
      error_message_ = base::StringPrintf(
          "actions[%d].actions.x is not a number", action_index_);
      return false;
    }
  }

  // If users pause for given seconds, we convert to the number of idle frames.
  if (duration > 0) {
    num_idle = static_cast<int>(std::ceil(
        duration / viz::BeginFrameArgs::DefaultInterval().InSecondsF()));
  }

  SyntheticPointerActionParams action_param(pointer_action_type);
  if (pointer_id == -1)
    action_param.set_pointer_id(action_index_);
  else
    action_param.set_pointer_id(pointer_id);
  switch (pointer_action_type) {
    case SyntheticPointerActionParams::PointerActionType::PRESS:
      action_param.set_position(gfx::PointF(position_x, position_y));
      action_param.set_button(button);
      break;
    case SyntheticPointerActionParams::PointerActionType::MOVE:
      action_param.set_position(gfx::PointF(position_x, position_y));
      break;
    case SyntheticPointerActionParams::PointerActionType::RELEASE:
      action_param.set_button(button);
      break;
    case SyntheticPointerActionParams::PointerActionType::LEAVE:
    case SyntheticPointerActionParams::PointerActionType::IDLE:
    case SyntheticPointerActionParams::PointerActionType::NOT_INITIALIZED:
      break;
  }
  param_list.push_back(action_param);

  // We queue all the IDLE actions in the action parameter list to make sure we
  // will pause long enough on the given pointer.
  for (int count = 1; count < num_idle; ++count)
    param_list.push_back(action_param);

  return true;
}

}  // namespace content
