/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file dual_screen_gui.h Bottom command desk, quick action bar, trackpad overlay, and window actions for dual-screen mode. */

#ifndef DUAL_SCREEN_GUI_H
#define DUAL_SCREEN_GUI_H

#include "core/geometry_type.hpp"

namespace DualScreen {

/** Create or refresh the Bottom Command Desk, Action Bar, and docked SmallMap. */
void ShowBottomCommandDesk();

/** Synchronize the DsTrackpadWindow visibility with DualScreen::IsTrackpadMode(). */
void SyncTrackpadWindow();

/** Reposition/resize dual-screen desk, action bar, trackpad, and docked smallmap after screen changes. */
void RelocateDualScreenWindows();

/** Migrate existing UI windows from Top to Bottom when transitioning Single -> Dual screen mode. */
void MigrateWindowsToDefaultScreens();

/** Move the top-most movable floating window between Top and Bottom screens. */
void SendActiveWindowToOtherScreen();

/** Close floating popups on the Bottom screen (revealing the docked minimap). */
void CloseBottomPopups();

/** Open, dock, or bring forward the SmallMap on the Bottom screen. */
void FocusOrToggleSmallMap();

/** Toggle virtual/on-screen keyboard input if supported by the video driver. */
void ToggleVirtualKeyboard();

/** Return screen-space inclusive rect of the interactive trackpad surface if open, or empty rect. */
Rect GetTrackpadPadScreenRect();

/** Return screen-space inclusive rect of the trackpad Drag Lock button if open, or empty rect. */
Rect GetTrackpadDragButtonScreenRect();

/** Whether trackpad drag-lock is currently active (holds left mouse button across Top cursor moves). */
bool IsTrackpadDragLocked();

/** Clear trackpad drag-lock if active. */
void ClearTrackpadDragLock();

/** Invalidate the DS action bar & trackpad button states for repainting. */
void InvalidateDualScreenControls();

} // namespace DualScreen

#endif /* DUAL_SCREEN_GUI_H */
