// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <Qt>

class InteractionController
{
 public:
  // These actions describe intent only. RenderWidget decides how that intent is
  // applied to either the camera or the currently selected object.
  enum class Action
  {
    None,
    Translate,
    Rotate,
    Scale
  };

  enum class AxisConstraint
  {
    Free,
    X,
    Y,
    Z
  };

  struct Result
  {
    Action action = Action::None;
    AxisConstraint axis = AxisConstraint::Free;
  };

  // Map the current mouse-button/modifier chord to a high-level manipulation.
  static Result classify(Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
};
