/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file dual_screen.cpp Core geometry, window routing, and input state for dual-screen mode. */

#include "stdafx.h"
#include "dual_screen.h"
#include "gfx_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "core/math_func.hpp"

#include <algorithm>
#include <cmath>

#include "safeguards.h"

namespace DualScreen {

namespace {

DualScreenConfig _config{};
bool _trackpad_mode = false;
bool _right_click_mode = false;
int _action_bar_height = 0;
Point _top_cursor_pos = { DEFAULT_TOP_WIDTH / 2, DEFAULT_TOP_HEIGHT / 2 };

} // namespace

void Reset()
{
	_config = DualScreenConfig{};
	_trackpad_mode = false;
	_right_click_mode = false;
	_action_bar_height = 0;
	_top_cursor_pos = { DEFAULT_TOP_WIDTH / 2, DEFAULT_TOP_HEIGHT / 2 };
}

bool IsActive()
{
	return _config.active && _config.top_w > 0 && _config.top_h > 0 && _config.bot_w > 0 && _config.bot_h > 0;
}

void SetGeometry(int top_w, int top_h, int bot_w, int bot_h, bool active)
{
	_config.top_w = std::max(1, top_w);
	_config.top_h = std::max(1, top_h);
	_config.bot_w = std::max(1, bot_w);
	_config.bot_h = std::max(1, bot_h);
	_config.active = active;
	_top_cursor_pos = ClampPointToScreen(_top_cursor_pos.x, _top_cursor_pos.y, ScreenTarget::Top);
}

DualScreenConfig GetConfig()
{
	return _config;
}

int GetTopWidth()
{
	return IsActive() ? _config.top_w : _screen.width;
}

int GetTopHeight()
{
	return IsActive() ? _config.top_h : _screen.height;
}

int GetBottomWidth()
{
	return IsActive() ? _config.bot_w : _screen.width;
}

int GetBottomHeight()
{
	return IsActive() ? _config.bot_h : 0;
}

int GetBottomOriginY()
{
	return IsActive() ? _config.top_h : 0;
}

int GetVirtualWidth()
{
	return IsActive() ? std::max(_config.top_w, _config.bot_w) : _screen.width;
}

int GetVirtualHeight()
{
	return IsActive() ? (_config.top_h + _config.bot_h) : _screen.height;
}

Rect GetTopScreenRect()
{
	int w = GetTopWidth();
	int h = GetTopHeight();
	return { 0, 0, std::max(0, w - 1), std::max(0, h - 1) };
}

Rect GetBottomScreenRect()
{
	if (!IsActive()) return GetTopScreenRect();
	return { 0, _config.top_h, std::max(0, _config.bot_w - 1), _config.top_h + _config.bot_h - 1 };
}

Rect GetScreenRect(ScreenTarget target)
{
	return target == ScreenTarget::Top ? GetTopScreenRect() : GetBottomScreenRect();
}

ScreenTarget GetScreenAtPoint(int x, int y)
{
	(void)x;
	if (!IsActive()) return ScreenTarget::Top;
	return y >= _config.top_h ? ScreenTarget::Bottom : ScreenTarget::Top;
}

ScreenTarget GetDefaultScreenForWindow(WindowClass wc)
{
	if (!IsActive()) return ScreenTarget::Top;
	if (wc == WindowClass::MainWindow) return ScreenTarget::Top;
	return ScreenTarget::Bottom;
}

Rect GetWorkArea(ScreenTarget target)
{
	if (!IsActive() || target == ScreenTarget::Top) {
		return GetTopScreenRect();
	}

	int left = 0;
	int right = std::max(0, _config.bot_w - 1);
	int top = _config.top_h;
	int bottom = _config.top_h + _config.bot_h - 1;

	if (const Window *tb = FindWindowById(WindowClass::MainToolbar, 0); tb != nullptr && tb->top >= _config.top_h) {
		top = std::max(top, tb->top + tb->height);
	}
	if (const Window *sb = FindWindowById(WindowClass::Statusbar, 0); sb != nullptr && sb->top >= _config.top_h) {
		bottom = std::min(bottom, sb->top - 1);
	}
	if (const Window *ab = FindWindowById(WindowClass::DsActionBar, 0); ab != nullptr && ab->top >= _config.top_h) {
		bottom = std::min(bottom, ab->top - 1);
	} else if (_action_bar_height > 0) {
		bottom = std::min(bottom, bottom - _action_bar_height);
	}

	if (bottom < top) bottom = top;
	return { left, top, right, bottom };
}

Point ClampPointToScreen(int x, int y, ScreenTarget target)
{
	Rect r = GetScreenRect(target);
	return { Clamp(x, r.left, r.right), Clamp(y, r.top, r.bottom) };
}

Point ClampWindowToScreen(int left, int top, int width, int height, ScreenTarget target)
{
	Rect r = GetScreenRect(target);
	int screen_w = r.Width();
	int screen_h = r.Height();
	int eff_w = std::max(1, width);
	int eff_h = std::max(1, height);

	int max_x = (eff_w < screen_w) ? (r.right - eff_w + 1) : r.left;
	int max_y = (eff_h < screen_h) ? (r.bottom - eff_h + 1) : r.top;

	return {
		Clamp(left, r.left, std::max(r.left, max_x)),
		Clamp(top, r.top, std::max(r.top, max_y))
	};
}

Point MapTopNormalizedToVirtual(float nx, float ny)
{
	int w = GetTopWidth();
	int h = GetTopHeight();
	float cx = std::isfinite(nx) ? std::clamp(nx, 0.0f, 1.0f) : 0.0f;
	float cy = std::isfinite(ny) ? std::clamp(ny, 0.0f, 1.0f) : 0.0f;
	int px = Clamp(static_cast<int>(std::lround(cx * static_cast<float>(std::max(0, w - 1)))), 0, std::max(0, w - 1));
	int py = Clamp(static_cast<int>(std::lround(cy * static_cast<float>(std::max(0, h - 1)))), 0, std::max(0, h - 1));
	return { px, py };
}

Point MapBottomNormalizedToVirtual(float nx, float ny)
{
	if (!IsActive()) return MapTopNormalizedToVirtual(nx, ny);
	float cx = std::isfinite(nx) ? std::clamp(nx, 0.0f, 1.0f) : 0.0f;
	float cy = std::isfinite(ny) ? std::clamp(ny, 0.0f, 1.0f) : 0.0f;
	int px = Clamp(static_cast<int>(std::lround(cx * static_cast<float>(std::max(0, _config.bot_w - 1)))), 0, std::max(0, _config.bot_w - 1));
	int py = _config.top_h + Clamp(static_cast<int>(std::lround(cy * static_cast<float>(std::max(0, _config.bot_h - 1)))), 0, std::max(0, _config.bot_h - 1));
	return { px, py };
}

bool IsTrackpadMode()
{
	return _trackpad_mode;
}

void SetTrackpadMode(bool enabled)
{
	_trackpad_mode = enabled;
}

void ToggleTrackpadMode()
{
	_trackpad_mode = !_trackpad_mode;
}

bool IsRightClickMode()
{
	return _right_click_mode;
}

void SetRightClickMode(bool enabled)
{
	_right_click_mode = enabled;
}

void ToggleRightClickMode()
{
	_right_click_mode = !_right_click_mode;
}

bool ConsumeRightClickMode()
{
	if (!_right_click_mode) return false;
	_right_click_mode = false;
	return true;
}

void SetActionBarHeight(int height)
{
	_action_bar_height = std::max(0, height);
}

int GetActionBarHeight()
{
	return _action_bar_height;
}

Point GetTopCursorPos()
{
	return ClampPointToScreen(_top_cursor_pos.x, _top_cursor_pos.y, ScreenTarget::Top);
}

void SetTopCursorPos(Point pt)
{
	_top_cursor_pos = ClampPointToScreen(pt.x, pt.y, ScreenTarget::Top);
}

TrackpadTouchResult ProcessTrackpadTouch(TrackpadTouchState &state, int action_masked, int pointer_id, int pointer_count, Point vpt, bool on_pad)
{
	if (!IsActive()) return TrackpadTouchResult::None;

	if (state.active && !IsTrackpadMode()) {
		state.active = false;
		state.moved = false;
		state.pointer_id = -1;
	}

	constexpr int ACTION_DOWN = 0;
	constexpr int ACTION_UP = 1;
	constexpr int ACTION_MOVE = 2;
	constexpr int ACTION_CANCEL = 3;
	constexpr int ACTION_POINTER_DOWN = 5;
	constexpr int ACTION_POINTER_UP = 6;

	if (action_masked == ACTION_DOWN) {
		if (on_pad && IsTrackpadMode()) {
			state.active = true;
			state.moved = false;
			state.pointer_id = pointer_id;
			state.start = vpt;
			state.last = vpt;
			return TrackpadTouchResult::None;
		}
		state.active = false;
		state.moved = false;
		state.pointer_id = -1;
		return TrackpadTouchResult::Passthrough;
	}

	if (action_masked == ACTION_POINTER_DOWN) {
		/* A 3+ finger gesture must never trigger a 1-finger or 2-finger tap on subsequent pointer lifts. */
		if (state.active && pointer_count > 2) {
			state.moved = true;
		}
		return TrackpadTouchResult::None;
	}

	if (action_masked == ACTION_MOVE) {
		if (!state.active) return TrackpadTouchResult::Passthrough;

		if (pointer_id != state.pointer_id) {
			/* Remaining finger took over after primary pointer lifted: re-anchor without cursor jump. */
			state.pointer_id = pointer_id;
			state.start = vpt;
			state.last = vpt;
			state.moved = true;
			return TrackpadTouchResult::None;
		}

		int dx = vpt.x - state.last.x;
		int dy = vpt.y - state.last.y;
		if (std::abs(vpt.x - state.start.x) > 8 || std::abs(vpt.y - state.start.y) > 8) {
			state.moved = true;
		}
		state.last = vpt;
		Point cur_top = GetTopCursorPos();
		Point top_pt = ClampPointToScreen(cur_top.x + (dx * 3) / 2, cur_top.y + (dy * 3) / 2, ScreenTarget::Top);
		SetTopCursorPos(top_pt);
		return TrackpadTouchResult::CursorMoved;
	}

	if (action_masked == ACTION_POINTER_UP) {
		TrackpadTouchResult result = TrackpadTouchResult::None;
		if (state.active && !state.moved && pointer_count == 2) {
			state.moved = true;
			result = TrackpadTouchResult::TwoFingerClick;
		}
		if (pointer_id == state.pointer_id) {
			state.pointer_id = -1;
		}
		return result;
	}

	if (action_masked == ACTION_UP || action_masked == ACTION_CANCEL) {
		if (state.active) {
			bool tap = (action_masked == ACTION_UP && !state.moved);
			state.active = false;
			state.moved = false;
			state.pointer_id = -1;
			return tap ? TrackpadTouchResult::TapClick : TrackpadTouchResult::None;
		}
		state.pointer_id = -1;
		return TrackpadTouchResult::Passthrough;
	}

	return TrackpadTouchResult::None;
}

} // namespace DualScreen
