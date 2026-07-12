// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QPoint>
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

  // Convert Qt screen coordinates (positive Y points down) into interaction
  // coordinates (positive Y points up). Every drag path must use this so
  // modifier-bound controls cannot silently reverse vertical movement.
  static QPoint controlDelta(const QPoint &screenDelta);
};
