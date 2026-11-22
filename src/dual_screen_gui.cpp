/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file dual_screen_gui.cpp Bottom command desk, quick action bar, trackpad overlay, and window actions. */

#include "stdafx.h"
#include "dual_screen.h"
#include "dual_screen_gui.h"
#include "gfx_func.h"
#include "window_gui.h"
#include "window_func.h"
#include "sound_func.h"
#include "strings_func.h"
#include "tilehighlight_func.h"
#include "zoom_func.h"
#include "smallmap_gui.h"
#include "viewport_func.h"
#include "video/video_driver.hpp"
#include "table/strings.h"

#include <algorithm>

#include "safeguards.h"

namespace DualScreen {

namespace {

bool _trackpad_drag_locked = false;
bool _keyboard_requested = false;

enum BottomDeskWidgets : WidgetID {
	WID_BD_BACKGROUND,
};

enum DsActionBarWidgets : WidgetID {
	WID_DSA_MAP,
	WID_DSA_TRACKPAD,
	WID_DSA_SWAP,
	WID_DSA_RCLICK,
	WID_DSA_CLEAR,
	WID_DSA_KEYBOARD,
};

enum DsTrackpadWidgets : WidgetID {
	WID_DST_PAD,
	WID_DST_LCLICK,
	WID_DST_RCLICK,
	WID_DST_DRAG,
	WID_DST_ZOOM_OUT,
	WID_DST_ZOOM_IN,
	WID_DST_EXIT,
};

void ResizeWindowToBounds(Window *w, int target_w, int target_h)
{
	if (w == nullptr || w->nested_root == nullptr) return;
	int dx = target_w - w->width;
	int dy = target_h - w->height;

	uint rx = w->nested_root->resize_x;
	uint ry = w->nested_root->resize_y;
	if (rx == 0) {
		dx = 0;
	} else if (rx > 1) {
		int cur = static_cast<int>(w->nested_root->current_x) - static_cast<int>(w->nested_root->smallest_x);
		int target = std::max(0, cur + dx);
		target -= target % static_cast<int>(rx);
		dx = target - cur;
	} else {
		int min_dx = static_cast<int>(w->nested_root->smallest_x) - w->width;
		dx = std::max(dx, min_dx);
	}

	if (ry == 0) {
		dy = 0;
	} else if (ry > 1) {
		int cur = static_cast<int>(w->nested_root->current_y) - static_cast<int>(w->nested_root->smallest_y);
		int target = std::max(0, cur + dy);
		target -= target % static_cast<int>(ry);
		dy = target - cur;
	} else {
		int min_dy = static_cast<int>(w->nested_root->smallest_y) - w->height;
		dy = std::max(dy, min_dy);
	}

	if (dx != 0 || dy != 0) {
		ResizeWindow(w, dx, dy, false, false);
	}
}

void DockWindowToRect(Window *w, const Rect &r)
{
	if (w == nullptr) return;
	w->SetDirty();
	w->left = r.left;
	w->top = r.top;
	ResizeWindowToBounds(w, r.Width(), r.Height());
	w->SetDirty();
}

bool IsVitalOrPermanentWindow(WindowClass wc)
{
	switch (wc) {
		case WindowClass::MainWindow:
		case WindowClass::BottomDesk:
		case WindowClass::MainToolbar:
		case WindowClass::Statusbar:
		case WindowClass::DsActionBar:
		case WindowClass::DsTrackpad:
		case WindowClass::SmallMap:
		case WindowClass::News:
		case WindowClass::NetworkChat:
		case WindowClass::SelectGame:
			return true;
		default:
			return false;
	}
}

bool IsMovablePopupWindow(const Window *w)
{
	if (w == nullptr) return false;
	switch (w->window_class) {
		case WindowClass::MainWindow:
		case WindowClass::BottomDesk:
		case WindowClass::MainToolbar:
		case WindowClass::Statusbar:
		case WindowClass::DsActionBar:
		case WindowClass::DsTrackpad:
		case WindowClass::DropdownMenu:
		case WindowClass::ToolTips:
		case WindowClass::Bootstrap:
		case WindowClass::Highscore:
		case WindowClass::Endscreen:
		case WindowClass::News:
		case WindowClass::NetworkChat:
		case WindowClass::Console:
		case WindowClass::ModalProgress:
			return false;
		default:
			return true;
	}
}

static constexpr std::initializer_list<NWidgetPart> _nested_bottom_desk_widgets = {
	NWidget(WWT_EMPTY, Colours::Invalid, WID_BD_BACKGROUND), SetMinimalSize(1, 1), SetResize(1, 1),
};

struct BottomDeskWindow : Window {
	BottomDeskWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		this->left = 0;
		this->top = GetBottomOriginY();
		ResizeWindowToBounds(this, GetBottomWidth(), GetBottomHeight());
	}

	Point OnInitialPosition(int16_t, int16_t, int) override
	{
		return { 0, GetBottomOriginY() };
	}

	void OnPaint() override
	{
		Rect r = { 0, 0, this->width - 1, this->height - 1 };
		GfxFillRect(r, PC_BLACK);
		if (this->height > 4 && this->width > 4) {
			GfxFillRect(r.Shrink(1), PixelColour{0x04}); // Deep charcoal desk backdrop
		}
		if (_game_mode == GameMode::Menu) {
			DrawString(ScaleGUITrad(8), this->width - ScaleGUITrad(8), ScaleGUITrad(8), "AYN Thor Dual-Screen Command Desk", TextColour::Gold, AlignmentH::Centre);
		}
	}
};

static WindowDesc _bottom_desk_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::BottomDesk, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_bottom_desk_widgets
);

static constexpr std::initializer_list<NWidgetPart> _nested_ds_action_bar_widgets = {
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DSA_MAP), SetMinimalSize(60, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_TEXTBTN, Colours::DarkBlue, WID_DSA_TRACKPAD), SetMinimalSize(72, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DSA_SWAP), SetMinimalSize(64, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_TEXTBTN, Colours::DarkBlue, WID_DSA_RCLICK), SetMinimalSize(68, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DSA_CLEAR), SetMinimalSize(60, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_TEXTBTN, Colours::DarkBlue, WID_DSA_KEYBOARD), SetMinimalSize(56, 22), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
	EndContainer(),
};

struct DsActionBarWindow : Window {
	DsActionBarWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		this->Reposition();
	}

	void Close(int data = 0) override
	{
		SetActionBarHeight(0);
		this->Window::Close(data);
	}

	void Reposition()
	{
		if (!IsActive()) return;
		SetActionBarHeight(this->height);
		int bot_w = GetBottomWidth();
		int bot_bottom = GetBottomOriginY() + GetBottomHeight();
		if (const Window *sb = FindWindowById(WindowClass::Statusbar, 0); sb != nullptr && sb->top >= GetBottomOriginY()) {
			bot_bottom = sb->top;
		}
		this->left = 0;
		this->top = std::max(GetBottomOriginY(), bot_bottom - this->height);
		ResizeWindowToBounds(this, bot_w, this->height);
		this->SyncButtonStates();
		this->SetDirty();
	}

	void SyncButtonStates()
	{
		this->SetWidgetLoweredState(WID_DSA_TRACKPAD, IsTrackpadMode());
		this->SetWidgetLoweredState(WID_DSA_RCLICK, IsRightClickMode());
		this->SetWidgetLoweredState(WID_DSA_KEYBOARD, _keyboard_requested);
	}

	Point OnInitialPosition(int16_t, int16_t sm_height, int) override
	{
		int bot_bottom = GetBottomOriginY() + GetBottomHeight();
		if (const Window *sb = FindWindowById(WindowClass::Statusbar, 0); sb != nullptr && sb->top >= GetBottomOriginY()) {
			bot_bottom = sb->top;
		}
		return { 0, std::max(GetBottomOriginY(), bot_bottom - sm_height) };
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_DSA_MAP:      return "Map";
			case WID_DSA_TRACKPAD: return "Trackpad";
			case WID_DSA_SWAP:     return "Swap Scr";
			case WID_DSA_RCLICK:   return "R-Click";
			case WID_DSA_CLEAR:    return "Clear UI";
			case WID_DSA_KEYBOARD: return "Keybd";
			default:               return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void OnPaint() override
	{
		this->SyncButtonStates();
		this->DrawWidgets();
	}

	void OnClick(Point, WidgetID widget, int) override
	{
		SndClickBeep();
		switch (widget) {
			case WID_DSA_MAP:
				FocusOrToggleSmallMap();
				break;
			case WID_DSA_TRACKPAD:
				ToggleTrackpadMode();
				SyncTrackpadWindow();
				break;
			case WID_DSA_SWAP:
				SendActiveWindowToOtherScreen();
				break;
			case WID_DSA_RCLICK:
				ToggleRightClickMode();
				break;
			case WID_DSA_CLEAR:
				CloseBottomPopups();
				break;
			case WID_DSA_KEYBOARD:
				ToggleVirtualKeyboard();
				break;
			default:
				break;
		}
		this->SyncButtonStates();
		this->SetDirty();
	}
};

static WindowDesc _ds_action_bar_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::DsActionBar, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_ds_action_bar_widgets
);

static constexpr std::initializer_list<NWidgetPart> _nested_ds_trackpad_widgets = {
	NWidget(WWT_PANEL, Colours::DarkBlue, WID_DST_PAD), SetFill(1, 1), SetResize(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DST_LCLICK), SetMinimalSize(60, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DST_RCLICK), SetMinimalSize(60, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_TEXTBTN, Colours::DarkBlue, WID_DST_DRAG), SetMinimalSize(68, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DST_ZOOM_OUT), SetMinimalSize(56, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DST_ZOOM_IN), SetMinimalSize(56, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkBlue, WID_DST_EXIT), SetMinimalSize(56, 28), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_JUST_RAW_STRING),
	EndContainer(),
};

void DispatchTopScreenClick(bool right_click)
{
	Point clamped = GetTopCursorPos();
	_cursor.pos = clamped;
	_cursor.delta = { 0, 0 };
	_cursor.in_window = true;
	if (right_click) {
		_input_pressed.Set(ControlInput::Right);
		_input_clicked.Set(ControlInput::Right);
		HandleMouseEvents();
		_input_pressed.Reset(ControlInput::Right);
		HandleMouseEvents();
	} else {
		/* Clear ControlInput::Left clicked bit because we may be invoked from inside DsTrackpadWindow::OnClick. */
		_input_clicked.Reset(ControlInput::Left);
		_input_pressed.Set(ControlInput::Left);
		HandleMouseEvents();
		if (!_trackpad_drag_locked) {
			_input_pressed.Reset(ControlInput::Left);
			_input_clicked.Reset(ControlInput::Left);
			HandleMouseEvents();
		}
	}
}

struct DsTrackpadWindow : Window {
	DsTrackpadWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		this->flags.Reset(WindowFlag::WhiteBorder);
		this->Reposition();
	}

	void Close(int data = 0) override
	{
		SetTrackpadMode(false);
		ClearTrackpadDragLock();
		this->Window::Close(data);
		InvalidateDualScreenControls();
	}

	void Reposition()
	{
		Rect work = GetWorkArea(ScreenTarget::Bottom);
		DockWindowToRect(this, work);
		this->SetWidgetLoweredState(WID_DST_DRAG, _trackpad_drag_locked);
	}

	Point OnInitialPosition(int16_t, int16_t, int) override
	{
		Rect work = GetWorkArea(ScreenTarget::Bottom);
		return { work.left, work.top };
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_DST_LCLICK:   return "L-Click";
			case WID_DST_RCLICK:   return "R-Click";
			case WID_DST_DRAG:     return "Drag Lock";
			case WID_DST_ZOOM_OUT: return "Zoom -";
			case WID_DST_ZOOM_IN:  return "Zoom +";
			case WID_DST_EXIT:     return "Exit Pad";
			default:               return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_DST_PAD) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
		GfxFillRect(ir, PixelColour{0x04});

		/* Draw subtle trackpad alignment grid. */
		int step = std::max(24, ScaleGUITrad(32));
		for (int gx = ir.left + step; gx < ir.right; gx += step) {
			GfxFillRect(gx, ir.top + 2, gx, ir.bottom - 2, PixelColour{0x0C});
		}
		for (int gy = ir.top + step; gy < ir.bottom; gy += step) {
			GfxFillRect(ir.left + 2, gy, ir.right - 2, gy, PixelColour{0x0C});
		}

		int mid_x = (ir.left + ir.right) / 2;
		int mid_y = (ir.top + ir.bottom) / 2;
		GfxFillRect(mid_x - 10, mid_y, mid_x + 10, mid_y, PixelColour{0x2A});
		GfxFillRect(mid_x, mid_y - 10, mid_x, mid_y + 10, PixelColour{0x2A});

		int text_y = ir.top + ScaleGUITrad(12);
		DrawString(ir.left + 8, ir.right - 8, text_y, "AYN Thor Trackpad Mode (Controls Top Screen Cursor)", TextColour::Gold, AlignmentH::Centre);
		Point top_cur = GetTopCursorPos();
		std::string info = fmt::format("Top Cursor: ({}, {})  |  Drag Lock: {}",
			top_cur.x,
			top_cur.y,
			_trackpad_drag_locked ? "ON" : "OFF");
		DrawString(ir.left + 8, ir.right - 8, text_y + GetCharacterHeight(FontSize::Normal) + ScaleGUITrad(6), info, TextColour::White, AlignmentH::Centre);
		DrawString(ir.left + 8, ir.right - 8, ir.bottom - GetCharacterHeight(FontSize::Normal) - ScaleGUITrad(8),
			"Slide finger/mouse here to steer Top cursor  |  Tap to Left-Click", TextColour::Silver, AlignmentH::Centre);
	}

	void OnPaint() override
	{
		this->SetWidgetLoweredState(WID_DST_DRAG, _trackpad_drag_locked);
		this->DrawWidgets();
	}

	void OnClick(Point, WidgetID widget, int) override
	{
		switch (widget) {
			case WID_DST_LCLICK:
				SndClickBeep();
				DispatchTopScreenClick(false);
				break;
			case WID_DST_RCLICK:
				SndClickBeep();
				DispatchTopScreenClick(true);
				break;
			case WID_DST_DRAG: {
				SndClickBeep();
				_trackpad_drag_locked = !_trackpad_drag_locked;
				Point top_cur = GetTopCursorPos();
				_cursor.pos = top_cur;
				_cursor.delta = { 0, 0 };
				if (_trackpad_drag_locked) {
					_input_clicked.Reset(ControlInput::Left);
					_input_pressed.Set(ControlInput::Left);
				} else {
					_input_pressed.Reset(ControlInput::Left);
					_input_clicked.Reset(ControlInput::Left);
				}
				HandleMouseEvents();
				break;
			}
			case WID_DST_ZOOM_OUT:
				SndClickBeep();
				if (Window *mw = GetMainWindow(); mw != nullptr) DoZoomInOutWindow(ZOOM_OUT, mw);
				break;
			case WID_DST_ZOOM_IN:
				SndClickBeep();
				if (Window *mw = GetMainWindow(); mw != nullptr) DoZoomInOutWindow(ZOOM_IN, mw);
				break;
			case WID_DST_EXIT:
				SndClickBeep();
				SetTrackpadMode(false);
				SyncTrackpadWindow();
				InvalidateDualScreenControls();
				return;
			default:
				break;
		}
		this->SetDirty();
	}
};

static WindowDesc _ds_trackpad_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::DsTrackpad, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_ds_trackpad_widgets
);

void DockSmallMapToWorkArea()
{
	if (!IsActive()) return;
	if (_game_mode != GameMode::Normal && _game_mode != GameMode::Editor) return;

	Window *sm = FindWindowById(WindowClass::SmallMap, 0);
	if (sm == nullptr) {
		ShowSmallMap();
		sm = FindWindowById(WindowClass::SmallMap, 0);
	}
	if (sm == nullptr) return;

	/* Re-sort SmallMap to dual-screen backdrop Z-priority (below floating popups, above BottomDesk). */
	BringWindowToFrontById(WindowClass::SmallMap, 0);

	Rect work = GetWorkArea(ScreenTarget::Bottom);
	DockWindowToRect(sm, work);
}

} // namespace

void ShowBottomCommandDesk()
{
	if (!IsActive()) return;

	if (FindWindowById(WindowClass::BottomDesk, 0) == nullptr) {
		new BottomDeskWindow(_bottom_desk_desc);
	}

	if (_game_mode == GameMode::Normal || _game_mode == GameMode::Editor) {
		if (FindWindowById(WindowClass::DsActionBar, 0) == nullptr) {
			new DsActionBarWindow(_ds_action_bar_desc);
		}
		DockSmallMapToWorkArea();
		SyncTrackpadWindow();
	} else {
		SetActionBarHeight(0);
		SetTrackpadMode(false);
		ClearTrackpadDragLock();
		CloseWindowById(WindowClass::DsActionBar, 0);
		CloseWindowById(WindowClass::DsTrackpad, 0);
	}
}

void SyncTrackpadWindow()
{
	if (!IsActive() || !IsTrackpadMode() || (_game_mode != GameMode::Normal && _game_mode != GameMode::Editor)) {
		ClearTrackpadDragLock();
		CloseWindowById(WindowClass::DsTrackpad, 0);
		InvalidateDualScreenControls();
		return;
	}

	DsTrackpadWindow *tp = dynamic_cast<DsTrackpadWindow *>(FindWindowById(WindowClass::DsTrackpad, 0));
	if (tp == nullptr) {
		new DsTrackpadWindow(_ds_trackpad_desc);
	} else {
		tp->Reposition();
	}
	InvalidateDualScreenControls();
}

void RelocateDualScreenWindows()
{
	if (!IsActive()) {
		SetActionBarHeight(0);
		CloseWindowById(WindowClass::BottomDesk, 0);
		CloseWindowById(WindowClass::DsActionBar, 0);
		CloseWindowById(WindowClass::DsTrackpad, 0);
		return;
	}

	if (BottomDeskWindow *bd = dynamic_cast<BottomDeskWindow *>(FindWindowById(WindowClass::BottomDesk, 0)); bd != nullptr) {
		bd->left = 0;
		bd->top = GetBottomOriginY();
		ResizeWindowToBounds(bd, GetBottomWidth(), GetBottomHeight());
	}

	if (DsActionBarWindow *ab = dynamic_cast<DsActionBarWindow *>(FindWindowById(WindowClass::DsActionBar, 0)); ab != nullptr) {
		ab->Reposition();
	}

	if (Window *sm = FindWindowById(WindowClass::SmallMap, 0); sm != nullptr && (_game_mode == GameMode::Normal || _game_mode == GameMode::Editor)) {
		if (GetScreenAtPoint(sm->left, sm->top) == ScreenTarget::Bottom) {
			DockWindowToRect(sm, GetWorkArea(ScreenTarget::Bottom));
		}
	}

	if (DsTrackpadWindow *tp = dynamic_cast<DsTrackpadWindow *>(FindWindowById(WindowClass::DsTrackpad, 0)); tp != nullptr) {
		tp->Reposition();
	}
}

void MigrateWindowsToDefaultScreens()
{
	if (!IsActive()) return;

	Rect bot_work = GetWorkArea(ScreenTarget::Bottom);
	for (Window *w : Window::Iterate()) {
		switch (w->window_class) {
			case WindowClass::MainWindow:
			case WindowClass::Bootstrap:
			case WindowClass::Highscore:
			case WindowClass::Endscreen:
			case WindowClass::BottomDesk:
			case WindowClass::MainToolbar:
			case WindowClass::Statusbar:
			case WindowClass::DsActionBar:
			case WindowClass::DsTrackpad:
			case WindowClass::Console:
			case WindowClass::News:
			case WindowClass::NetworkChat:
				continue;

			case WindowClass::SmallMap:
				if (_game_mode == GameMode::Normal || _game_mode == GameMode::Editor) {
					DockSmallMapToWorkArea();
				}
				continue;

			default:
				break;
		}

		if (GetDefaultScreenForWindow(w->window_class) != ScreenTarget::Bottom) continue;
		if (GetScreenAtPoint(w->left + w->width / 2, w->top + w->height / 2) == ScreenTarget::Bottom) continue;

		int nx = w->flags.Test(WindowFlag::Centred)
			? (bot_work.left + std::max(0, (bot_work.Width() - w->width) / 2))
			: (bot_work.left + w->left);
		int ny = w->flags.Test(WindowFlag::Centred)
			? (bot_work.top + std::max(0, (bot_work.Height() - w->height) / 2))
			: (bot_work.top + w->top);
		Point clamped = ClampWindowToScreen(nx, ny, w->width, w->height, ScreenTarget::Bottom);
		if (clamped.y < bot_work.top) clamped.y = bot_work.top;

		w->SetDirty();
		if (w->viewport != nullptr) {
			w->viewport->left += clamped.x - w->left;
			w->viewport->top += clamped.y - w->top;
		}
		w->left = clamped.x;
		w->top = clamped.y;
		w->SetDirty();
	}
}

void SendActiveWindowToOtherScreen()
{
	if (!IsActive()) return;

	Window *target = nullptr;
	for (Window *w : Window::IterateFromFront()) {
		if (IsMovablePopupWindow(w)) {
			target = w;
			break;
		}
	}
	if (target == nullptr) return;

	ScreenTarget cur = GetScreenAtPoint(target->left + target->width / 2, target->top + target->height / 2);
	ScreenTarget next = (cur == ScreenTarget::Top) ? ScreenTarget::Bottom : ScreenTarget::Top;

	Rect area = GetWorkArea(next);
	int nx = area.left + std::max(0, (area.Width() - target->width) / 2);
	int ny = area.top + std::max(0, (area.Height() - target->height) / 2);
	Point clamped = ClampWindowToScreen(nx, ny, target->width, target->height, next);
	if (clamped.y < area.top) clamped.y = area.top;

	target->SetDirty();
	if (target->viewport != nullptr) {
		target->viewport->left += clamped.x - target->left;
		target->viewport->top += clamped.y - target->top;
	}
	target->left = clamped.x;
	target->top = clamped.y;
	target->SetDirty();
}

void CloseBottomPopups()
{
	if (!IsActive()) return;

	/* Window::Iterate() remains safe across Window::Close() calls and skips already-closed children. */
	for (Window *w : Window::Iterate()) {
		if (IsVitalOrPermanentWindow(w->window_class)) continue;
		if (GetScreenAtPoint(w->left + w->width / 2, w->top + w->height / 2) == ScreenTarget::Bottom) {
			w->Close();
		}
	}
}

void FocusOrToggleSmallMap()
{
	if (!IsActive()) {
		ShowSmallMap();
		return;
	}

	if (IsTrackpadMode()) {
		SetTrackpadMode(false);
		SyncTrackpadWindow();
	}
	CloseBottomPopups();
	DockSmallMapToWorkArea();
}

void ToggleVirtualKeyboard()
{
	_keyboard_requested = !_keyboard_requested;
	if (VideoDriver *drv = VideoDriver::GetInstance(); drv != nullptr) {
		if (_keyboard_requested) {
			drv->EditBoxGainedFocus();
		} else {
			drv->EditBoxLostFocus();
		}
	}
	InvalidateDualScreenControls();
}

Rect GetTrackpadPadScreenRect()
{
	if (!IsActive() || !IsTrackpadMode()) return {};
	Window *tp = FindWindowById(WindowClass::DsTrackpad, 0);
	if (tp == nullptr) return {};
	const NWidgetBase *pad = tp->GetWidget<NWidgetBase>(WID_DST_PAD);
	if (pad == nullptr) return {};
	return pad->GetCurrentRect().Translate(tp->left, tp->top);
}

Rect GetTrackpadDragButtonScreenRect()
{
	if (!IsActive() || !IsTrackpadMode()) return {};
	Window *tp = FindWindowById(WindowClass::DsTrackpad, 0);
	if (tp == nullptr) return {};
	const NWidgetBase *btn = tp->GetWidget<NWidgetBase>(WID_DST_DRAG);
	if (btn == nullptr) return {};
	return btn->GetCurrentRect().Translate(tp->left, tp->top);
}

bool IsTrackpadDragLocked()
{
	return _trackpad_drag_locked;
}

void ClearTrackpadDragLock()
{
	if (_trackpad_drag_locked) {
		_trackpad_drag_locked = false;
		_input_pressed.Reset(ControlInput::Left);
		_input_clicked.Reset(ControlInput::Left);
	}
}

void InvalidateDualScreenControls()
{
	if (DsActionBarWindow *ab = dynamic_cast<DsActionBarWindow *>(FindWindowById(WindowClass::DsActionBar, 0)); ab != nullptr) {
		ab->SyncButtonStates();
		ab->SetDirty();
	}
	if (Window *tp = FindWindowById(WindowClass::DsTrackpad, 0); tp != nullptr) {
		tp->SetDirty();
	}
}

} // namespace DualScreen
