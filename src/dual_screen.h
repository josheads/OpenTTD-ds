/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file dual_screen.h Core geometry, window routing, and input state for dual-screen mode (AYN Thor & desktop simulator). */

#ifndef DUAL_SCREEN_H
#define DUAL_SCREEN_H

#include "core/geometry_type.hpp"
#include "window_type.h"

namespace DualScreen {

/** Physical/logical display target in dual-screen mode. */
enum class ScreenTarget : uint8_t {
	Top = 0,    ///< Primary 16:9 top display (unobstructed world map).
	Bottom = 1, ///< Secondary 31:27 bottom display (command desk, toolbars, minimap, windows).
};

/** Default logical resolutions (1.5x crisp integer-friendly scaling onto AYN Thor 1920x1080 + 1240x1080). */
static constexpr int DEFAULT_TOP_WIDTH = 1280;
static constexpr int DEFAULT_TOP_HEIGHT = 720;
static constexpr int DEFAULT_BOTTOM_WIDTH = 826;
static constexpr int DEFAULT_BOTTOM_HEIGHT = 720;

/** Current dual-screen configuration. */
struct DualScreenConfig {
	bool active = false;
	int top_w = DEFAULT_TOP_WIDTH;
	int top_h = DEFAULT_TOP_HEIGHT;
	int bot_w = DEFAULT_BOTTOM_WIDTH;
	int bot_h = DEFAULT_BOTTOM_HEIGHT;
};

/** Reset all dual-screen state to inactive defaults (primarily for unit tests). */
void Reset();

/** Whether dual-screen mode is currently active. */
bool IsActive();

/** Set dual-screen logical geometry and activation state. */
void SetGeometry(int top_w, int top_h, int bot_w, int bot_h, bool active);

/** Get a copy of the current dual-screen configuration. */
DualScreenConfig GetConfig();

int GetTopWidth();
int GetTopHeight();
int GetBottomWidth();
int GetBottomHeight();
int GetBottomOriginY();

/** Virtual framebuffer dimensions enclosing both stacked screens. */
int GetVirtualWidth();
int GetVirtualHeight();

/** Inclusive screen rectangles in virtual framebuffer coordinates. */
Rect GetTopScreenRect();
Rect GetBottomScreenRect();
Rect GetScreenRect(ScreenTarget target);

/** Determine which screen owns virtual coordinate (x, y). */
ScreenTarget GetScreenAtPoint(int x, int y);

/** Determine the default screen for a newly opened window of class wc. */
ScreenTarget GetDefaultScreenForWindow(WindowClass wc);

/** Return the usable work area (excluding docked bars) on the given screen, as an inclusive Rect. */
Rect GetWorkArea(ScreenTarget target);

/** Clamp a cursor/pointer point inside the valid bounds of target screen (excluding the dead zone). */
Point ClampPointToScreen(int x, int y, ScreenTarget target);

/** Clamp a window top-left corner so the window stays inside target screen bounds (excluding the dead zone). */
Point ClampWindowToScreen(int left, int top, int width, int height, ScreenTarget target);

/** Map normalized [0, 1] touch coordinates on the Top display to virtual framebuffer pixels. */
Point MapTopNormalizedToVirtual(float nx, float ny);

/** Map normalized [0, 1] touch coordinates on the Bottom display to virtual framebuffer pixels. */
Point MapBottomNormalizedToVirtual(float nx, float ny);

/** Trackpad mode: bottom work area acts as relative cursor pad for the Top display. */
bool IsTrackpadMode();
void SetTrackpadMode(bool enabled);
void ToggleTrackpadMode();

/** Sticky/one-shot right-click mode toggle for touch convenience. */
bool IsRightClickMode();
void SetRightClickMode(bool enabled);
void ToggleRightClickMode();
bool ConsumeRightClickMode();

/** Tracked height of the Bottom DS Action Bar (0 when hidden or inactive). */
void SetActionBarHeight(int height);
int GetActionBarHeight();

/** Last tracked logical cursor position on the Top display (preserved across Bottom interactions). */
Point GetTopCursorPos();
void SetTopCursorPos(Point pt);

/** Outcome of advancing the multi-touch bottom-screen trackpad state machine. */
enum class TrackpadTouchResult : uint8_t {
	None = 0,       ///< Trackpad state updated; no immediate click or cursor move needed.
	Passthrough,    ///< Event does not belong to an active trackpad gesture; handle as direct bottom touch.
	CursorMoved,    ///< Top cursor position updated via relative trackpad delta (read via GetTopCursorPos()).
	TapClick,       ///< 1-finger tap completed on trackpad pad.
	TwoFingerClick, ///< 2-finger tap completed on trackpad pad (right-click).
};

/** Persistent multi-touch gesture state for the bottom-screen trackpad. */
struct TrackpadTouchState {
	bool active = false;
	bool moved = false;
	int pointer_id = -1;
	Point start{};
	Point last{};
};

/**
 * Advance the bottom-screen multi-touch trackpad state machine for a single MotionEvent.
 * Handles 1-finger relative movement (1.5x gain), 1-finger tap, 2-finger right-click tap,
 * 3+ finger tap suppression, mid-gesture mode exit, and zero-jump pointer-ID handoff.
 */
TrackpadTouchResult ProcessTrackpadTouch(TrackpadTouchState &state, int action_masked, int pointer_id, int pointer_count, Point vpt, bool on_pad);

} // namespace DualScreen

#endif /* DUAL_SCREEN_H */
