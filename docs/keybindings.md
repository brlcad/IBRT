# Controls and Key Bindings

This document describes the bindings implemented by the IBRT desktop viewer.
The names below refer to physical keys on each platform, rather than Qt's
internal modifier names.

Most viewport bindings require the pointer or keyboard focus to be in the
render viewport. They are ignored while a scene is loading. When an ImGui
control is actively accepting keyboard or mouse input, that control normally
takes precedence over the viewport.

## Platform terminology

| Meaning in this document | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Primary drag | Normal click-and-drag | Left-button drag | Left-button drag |
| Secondary drag | Configured secondary/right click-and-drag | Right-button drag | Right-button drag |
| Main shortcut modifier | Command (`⌘`) | Control (`Ctrl`) | Control (`Ctrl`) |
| Alternate modifier | Option (`⌥`) | Alt | Alt |

Qt maps its internal `ControlModifier` to the physical Command key on macOS
and to the physical Control key on Windows and Linux. It maps `AltModifier` to
Option on macOS. The physical macOS Control key maps to Qt's Meta modifier and
has no modifier-specific IBRT binding.

On a Mac trackpad, a primary drag is a normal press-and-drag. A secondary drag
requires the secondary-click gesture configured in macOS, commonly a
two-finger click, to remain held while the pointer moves. A two-finger scroll
is a wheel/scroll gesture, not a secondary drag. IBRT does not handle native
pinch or rotate gestures.

## Application shortcuts

| Action | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Open Model | `⌘O` | `Ctrl+O` | `Ctrl+O` |
| Reset View | `⌘R` | `Ctrl+R` | `Ctrl+R` |
| Quit | `⌘Q` | No IBRT shortcut; `Alt+F4` normally closes the window | `Ctrl+Q` on common Qt desktops; `Alt+F4` normally closes the window |

Open and Quit use Qt's platform-specific standard key sequences. In
particular, Qt's standard Quit sequence is unbound on Windows. Reset View is
defined as Qt `Ctrl+R`, which becomes `⌘R` on macOS.

Reset View rebuilds or reloads the active scene as needed, returns to Orbit
mode, restores the camera's default field of view and orientation, and
reframes the scene. It does not change the current perspective/orthographic
projection choice.

There are currently no direct application shortcuts for choosing Orbit or Fly
from the View menu, selecting Y-up or Z-up, opening the BRL-CAD object
hierarchy, choosing a demo model, toggling full screen, or hiding the
coordinate-axis indicator. `Tab` changes Orbit/Fly mode only while the
viewport handles the key, and `G` hides the control panel rather than the axis
indicator.

### Menu mnemonics

The menu labels define the following mnemonics where the platform and desktop
environment enable them:

- File: `Alt+F`
- View: `Alt+V`
- Select Model: `Alt+S`
- In the File menu: Open Model (`O`), Reset View (`R`), Exit (`X`), and Demo
  Models (`D`)
- In the View menu: Standard Views (`V`) and Orthographic (`O`)

Qt enables these mnemonics by default on Windows and X11. They are disabled by
default on macOS; behavior on Linux can vary with the desktop environment and
display platform.

## Viewport keyboard bindings

These bindings are handled directly by the render viewport after the ImGui
keyboard-capture check, except for `G` as noted below.

| Action | macOS | Windows/Linux | Behavior |
| --- | --- | --- | --- |
| Front view | `1` | `1` | Switch to Orbit mode and reframe the scene from the front. |
| Back view | `⌘1` | `Ctrl+1` | Switch to Orbit mode and reframe the scene from the back. |
| Right view | `3` | `3` | Switch to Orbit mode and reframe the scene from the right. |
| Left view | `⌘3` | `Ctrl+3` | Switch to Orbit mode and reframe the scene from the left. |
| Top view | `7` | `7` | Switch to Orbit mode and reframe the scene from above. |
| Bottom view | `⌘7` | `Ctrl+7` | Switch to Orbit mode and reframe the scene from below. |
| Isometric view | `0` | `0` | Switch to Orbit mode and reframe to an isometric view. |
| Toggle projection | `5` | `5` | Toggle between perspective and orthographic projection. |
| Toggle navigation mode | `Tab` | `Tab` | Switch between Orbit and Fly while preserving the visible view. |
| Toggle control overlay | `G` | `G` | Show or hide the ImGui control panel; the coordinate-axis indicator remains visible. |

The standard-view and projection keys ignore auto-repeat. `Tab` currently does
not, so holding it can switch modes repeatedly. `G` is handled before ImGui's
keyboard-capture check and is effectively modifier-insensitive: a modified
`G` key event can also toggle the overlay.

The shortcut hints displayed beside Standard Views in the View menu are
informational. Those digit bindings are viewport bindings, not global QAction
shortcuts, so they do not override typing in a focused ImGui field.

The current menu hints and in-viewport help use the portable labels `Ctrl` and
`Alt` on every platform. On macOS, read those labels as Command and Option,
respectively; the displayed text has not yet been made platform-native.

## Fly-mode keyboard and pointer controls

| Input | Behavior |
| --- | --- |
| `W` | Move forward along the viewing direction. |
| `S` | Move backward. |
| `A` | Strafe left. |
| `D` | Strafe right. |
| `Q` | Move opposite the selected world-up direction: down. |
| `E` | Move along the selected world-up direction: up. |
| Primary drag | Change yaw and pitch to look around. |
| Wheel or vertical two-finger scroll | Narrow or widen the field of view, clamped to 20°–90°. |
| `Tab` | Return to Orbit mode while preserving the visible view. |

The default world-up direction is Z, so `Q` and `E` initially move along
negative and positive Z. If Y-up is selected, they move along negative and
positive Y instead.

Each movement key press advances by one configured Fly speed step. Holding a
key relies on operating-system key-repeat events rather than frame-time-based
continuous movement, so held-key speed can vary with keyboard repeat settings.

Orbit modifier gestures are not applied in Fly mode. A primary drag continues
to look around even when Shift, Control/Command, or Alt/Option is held.

## Orbit-mode pointer controls

The unmodified controls are the same on all platforms:

| Input | Behavior |
| --- | --- |
| Primary drag | Orbit the camera around the view target. |
| Secondary drag | Pan in the screen plane. The visible model follows the pointer. |
| Wheel or vertical two-finger scroll | Dolly toward or away from the view target. |

### Modifier-assisted orbit controls

| Behavior | macOS | Windows/Linux |
| --- | --- | --- |
| Translate in the screen plane | `Shift` + drag | `Shift` + drag |
| Free orbit rotation | `Command` + drag | `Ctrl` + drag |
| Dolly/scale | `Shift+Command` + drag | `Shift+Ctrl` + drag |
| Translate along world X | `Option` + primary drag | `Alt` + left drag |
| Translate along world Y | `Option+Shift` + primary drag | `Alt+Shift` + left drag |
| Translate along world Z | `Option` + secondary drag | `Alt` + right drag |

Screen-plane translation uses a grab convention: dragging left, right, up, or
down makes the visible model move in that same screen direction. The operation
changes the camera/view target; it does not transform the model geometry.

Free orbit rotation changes the camera azimuth and elevation. Dolly/scale uses
only vertical motion: dragging up moves closer and makes the model appear
larger; dragging down moves farther away. Horizontal motion has no scale
effect.

Axis-constrained translation combines horizontal and vertical pointer motion
into displacement along the named world axis. The selected Y-up or Z-up
convention does not rename those world axes.

On Linux, a window manager may reserve Alt-drag for moving or resizing windows.
If that happens, the desktop can intercept an axis-constrained gesture before
IBRT receives it.

On macOS, physical Control is not the modifier represented by `Ctrl` in the
source or the current in-viewport help. Use Command for the rotate and scale
bindings. macOS can interpret Control-click as a secondary click, but IBRT does
not explicitly define Control-drag as an alternative modifier binding; use the
configured secondary-click gesture when a secondary drag is required.

### Current modifier-drag quirk

The Shift, Control/Command, and Shift+Control/Command classifier branches do
not currently require a mouse button to be held. In Orbit mode, moving the
pointer while holding one of those modifier combinations can therefore
translate, rotate, or dolly even without a click. The documented gesture is a
drag, and requiring a held button is the intended behavior.

The Alt/Option axis-constrained branches do require the specific primary or
secondary button shown in the table.

## Control-panel keyboard input

When an ImGui field or widget captures the keyboard, it takes precedence over
the viewport for most keys. IBRT explicitly forwards Enter/Return, Backspace,
Delete, the arrow keys, Home, End, Tab, `W`, `A`, `S`, `D`, `Q`, and `E`, plus
printable character input and modifier state, to ImGui. `G` is the exception:
it toggles the entire control overlay even while ImGui wants the keyboard.

## Implementation references

- Application shortcuts and menu labels: `apps/IBRT/mainwindow.cpp`
- View, projection, Fly, and overlay hotkeys: `apps/IBRT/renderwidget.cpp`
- Modifier-assisted pointer classification: `apps/IBRT/interactioncontroller.cpp`
- Platform shortcut and modifier semantics: [Qt QKeySequence documentation](https://doc.qt.io/qt-6/qkeysequence.html)
