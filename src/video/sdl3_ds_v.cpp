/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sdl3_ds_v.cpp SDL3 Dual-Screen video driver for AYN Thor (Android) and desktop dual-window simulator. */

#ifdef WITH_SDL3

#include "../stdafx.h"
#include "../openttd.h"
#include "../debug.h"
#include "../gfx_func.h"
#include "../blitter/factory.hpp"
#include "../core/geometry_func.hpp"
#include "../core/math_func.hpp"
#include "../core/utf8.hpp"
#include "../dual_screen.h"
#include "../dual_screen_gui.h"
#include "../fileio_func.h"
#include "../fontcache.h"
#include "../framerate_type.h"
#include "../hotkeys.h"
#include "../network/network.h"
#include "../settings_type.h"
#include "../strings_func.h"
#include "../tilehighlight_func.h"
#include "../viewport_func.h"
#include "../window_func.h"
#include "../window_gui.h"
#include "../zoom_func.h"
#include "sdl3_ds_v.h"

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include "../os/android/android_ds_jni.h"
#include <android/native_window.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../safeguards.h"

static FVideoDriver_SDL_DS iFVideoDriver_SDL_DS;

namespace {

struct SDLVkMapping {
	SDL_Keycode vk_from;
	WindowKeyCodes map_to;
	uint8_t count;
	bool unprintable;

	constexpr SDLVkMapping(SDL_Keycode vk_first, SDL_Keycode vk_last, WindowKeyCodes map_first, [[maybe_unused]] WindowKeyCodes map_last, bool unprintable)
		: vk_from(vk_first), map_to(map_first), count(static_cast<uint8_t>(vk_last - vk_first + 1)), unprintable(unprintable) {}

	constexpr SDLVkMapping(SDL_Keycode vk_from, WindowKeyCodes map_to, bool unprintable)
		: vk_from(vk_from), map_to(map_to), count(1), unprintable(unprintable) {}
};

static constexpr SDLVkMapping _ds_vk_mapping[] = {
	{ SDLK_PAGEUP, WKC_PAGEUP, true },
	{ SDLK_PAGEDOWN, WKC_PAGEDOWN, true },
	{ SDLK_UP, WKC_UP, true },
	{ SDLK_DOWN, WKC_DOWN, true },
	{ SDLK_LEFT, WKC_LEFT, true },
	{ SDLK_RIGHT, WKC_RIGHT, true },
	{ SDLK_HOME, WKC_HOME, true },
	{ SDLK_END, WKC_END, true },
	{ SDLK_INSERT, WKC_INSERT, true },
	{ SDLK_DELETE, WKC_DELETE, true },
	{ SDLK_ESCAPE, WKC_ESC, true },
	{ SDLK_PAUSE, WKC_PAUSE, true },
	{ SDLK_BACKSPACE, WKC_BACKSPACE, true },
	{ SDLK_F1, SDLK_F12, WKC_F1, WKC_F12, true },
	{ SDLK_A, SDLK_Z, WindowKeyCodes{'A'}, WindowKeyCodes{'Z'}, false },
	{ SDLK_0, SDLK_9, WindowKeyCodes{'0'}, WindowKeyCodes{'9'}, false },
	{ SDLK_SPACE, WKC_SPACE, false },
	{ SDLK_RETURN, WKC_RETURN, false },
	{ SDLK_TAB, WKC_TAB, false },
	{ SDLK_KP_1, SDLK_KP_9, WindowKeyCodes{'1'}, WindowKeyCodes{'9'}, false },
	{ SDLK_KP_0, WindowKeyCodes{'0'}, false },
	{ SDLK_KP_DIVIDE, WKC_NUM_DIV, false },
	{ SDLK_KP_MULTIPLY, WKC_NUM_MUL, false },
	{ SDLK_KP_MINUS, WKC_NUM_MINUS, false },
	{ SDLK_KP_PLUS, WKC_NUM_PLUS, false },
	{ SDLK_KP_ENTER, WKC_NUM_ENTER, false },
	{ SDLK_KP_PERIOD, WKC_NUM_DECIMAL, false },
	{ SDLK_SLASH, WKC_SLASH, false },
	{ SDLK_SEMICOLON, WKC_SEMICOLON, false },
	{ SDLK_EQUALS, WKC_EQUALS, false },
	{ SDLK_LEFTBRACKET, WKC_L_BRACKET, false },
	{ SDLK_BACKSLASH, WKC_BACKSLASH, false },
	{ SDLK_RIGHTBRACKET, WKC_R_BRACKET, false },
	{ SDLK_APOSTROPHE, WKC_SINGLEQUOTE, false },
	{ SDLK_COMMA, WKC_COMMA, false },
	{ SDLK_MINUS, WKC_MINUS, false },
	{ SDLK_PERIOD, WKC_PERIOD, false },
};

uint ConvertSdlKey(const SDL_KeyboardEvent &key_event, char32_t *character)
{
	uint key = 0;
	bool unprintable = false;

	for (const auto &map : _ds_vk_mapping) {
		if (IsInsideBS(key_event.key, map.vk_from, map.count)) {
			key = key_event.key - map.vk_from + map.map_to;
			unprintable = map.unprintable;
			break;
		}
	}

	if (key_event.scancode == SDL_SCANCODE_GRAVE) key = WKC_BACKQUOTE;
	if (key_event.mod & SDL_KMOD_GUI) key |= WKC_META;
	if (key_event.mod & SDL_KMOD_SHIFT) key |= WKC_SHIFT;
	if (key_event.mod & SDL_KMOD_CTRL) key |= WKC_CTRL;
	if (key_event.mod & SDL_KMOD_ALT) key |= WKC_ALT;

	if ((key_event.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_ALT)) || unprintable) {
		*character = '\0';
	} else {
		*character = key_event.key;
	}
	return key;
}

uint ConvertSdlKeycode(SDL_Keycode kc)
{
	uint key = 0;
	for (const auto &map : _ds_vk_mapping) {
		if (IsInsideBS(kc, map.vk_from, map.count)) {
			key = kc - map.vk_from + map.map_to;
			break;
		}
	}
	if (SDL_GetScancodeFromKey(kc, nullptr) == SDL_SCANCODE_GRAVE) key = WKC_BACKQUOTE;
	return key;
}

void PanMainViewportPixels(int dx, int dy)
{
	Window *mw = GetMainWindow();
	if (mw == nullptr || mw->viewport == nullptr) return;
	mw->viewport->CancelFollow(*mw);
	mw->viewport->scrollpos_x += ScaleByZoom(dx, mw->viewport->zoom);
	mw->viewport->scrollpos_y += ScaleByZoom(dy, mw->viewport->zoom);
	mw->viewport->dest_scrollpos_x = mw->viewport->scrollpos_x;
	mw->viewport->dest_scrollpos_y = mw->viewport->scrollpos_y;
}

void SendSyntheticMouseClick(int x, int y, bool right)
{
	_cursor.pos.x = x;
	_cursor.pos.y = y;
	_cursor.delta = { 0, 0 };
	_cursor.in_window = true;
	HandleMouseEvents();

	if (right) {
		_input_pressed.Set(ControlInput::Right);
		_input_clicked.Set(ControlInput::Right);
		HandleMouseEvents();
		_input_pressed.Reset(ControlInput::Right);
		HandleMouseEvents();
	} else {
		_input_clicked.Reset(ControlInput::Left);
		_input_pressed.Set(ControlInput::Left);
		HandleMouseEvents();
		if (!DualScreen::IsTrackpadDragLocked()) {
			_input_pressed.Reset(ControlInput::Left);
			_input_clicked.Reset(ControlInput::Left);
			HandleMouseEvents();
		}
	}
}

bool PointInRectInclusive(const Rect &r, int x, int y)
{
	return r.left <= r.right && r.top <= r.bottom && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

bool ConsumeRightClickAndInvalidate()
{
	if (!DualScreen::ConsumeRightClickMode()) return false;
	DualScreen::InvalidateDualScreenControls();
	return true;
}

bool IsPointOnActiveTrackpadPad(int vx, int vy)
{
	if (!DualScreen::IsActive() || !DualScreen::IsTrackpadMode()) return false;
	Rect pad_rect = DualScreen::GetTrackpadPadScreenRect();
	if (!PointInRectInclusive(pad_rect, vx, vy)) return false;
	const Window *hit = FindWindowFromPt(vx, vy);
	return hit != nullptr && hit->window_class == WindowClass::DsTrackpad;
}

bool IsPointOnActiveTrackpadDragButton(int vx, int vy)
{
	if (!DualScreen::IsActive() || !DualScreen::IsTrackpadMode()) return false;
	Rect btn_rect = DualScreen::GetTrackpadDragButtonScreenRect();
	if (!PointInRectInclusive(btn_rect, vx, vy)) return false;
	const Window *hit = FindWindowFromPt(vx, vy);
	return hit != nullptr && hit->window_class == WindowClass::DsTrackpad;
}

void ReleaseTrackpadDragLockAtTopCursor()
{
	Point top_cur = DualScreen::GetTopCursorPos();
	_cursor.pos = top_cur;
	_cursor.delta = { 0, 0 };
	_cursor.in_window = true;
	DualScreen::ClearTrackpadDragLock();
	HandleMouseEvents();
	DualScreen::InvalidateDualScreenControls();
}

void MigrateLegacyDualScreenScale(int smoke_test_frames)
{
	if (smoke_test_frames > 0) return;
	if (FioCheckFileExists(".ds_scale_v2", Subdirectory::Base)) return;
	FioFOpenFile(".ds_scale_v2", "w", Subdirectory::Base);
	if (_gui_scale_cfg > 0 && _gui_scale_cfg <= 175) {
		_gui_scale_cfg = -1;
	}
}

#ifndef __ANDROID__
float ParseFloatParam(const StringList &params, std::string_view key, float def_val)
{
	auto val = GetDriverParam(params, key);
	if (!val.has_value() || val->empty()) return def_val;
	std::string tmp(*val);
	char *end = nullptr;
	float parsed = std::strtof(tmp.c_str(), &end);
	if (end == tmp.c_str() || !std::isfinite(parsed)) return def_val;
	return parsed;
}
#endif

} // namespace

std::optional<std::string_view> VideoDriver_SDL_DS::Start(const StringList &param)
{
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 0) return "Only real blitters supported";

#ifdef SDL_HINT_APP_NAME
	SDL_SetHint(SDL_HINT_APP_NAME, "OpenTTD");
#endif
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

	if (!SDL_WasInit(SDL_INIT_VIDEO)) {
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return SDL_GetError();
	}
	if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
		SDL_InitSubSystem(SDL_INIT_GAMEPAD);
	}

	this->cfg_top_w = std::max(640, GetDriverParamInt(param, "top_w", DualScreen::DEFAULT_TOP_WIDTH));
	this->cfg_top_h = std::max(480, GetDriverParamInt(param, "top_h", DualScreen::DEFAULT_TOP_HEIGHT));
	this->cfg_bot_w = std::max(640, GetDriverParamInt(param, "bot_w", DualScreen::DEFAULT_BOTTOM_WIDTH));
	this->cfg_bot_h = std::max(480, GetDriverParamInt(param, "bot_h", DualScreen::DEFAULT_BOTTOM_HEIGHT));
#ifndef __ANDROID__
	if (auto scale_param = GetDriverParam(param, "scale"); scale_param.has_value() && !scale_param->empty()) {
		this->desktop_scale = std::clamp(ParseFloatParam(param, "scale", 0.55f), 0.30f, 1.50f);
	} else {
		SDL_Rect usable{};
		SDL_DisplayID pri = SDL_GetPrimaryDisplay();
		if (pri != 0 && SDL_GetDisplayUsableBounds(pri, &usable) && usable.h > 0) {
			float fit_h = static_cast<float>(usable.h - 72) / static_cast<float>(this->cfg_top_h + this->cfg_bot_h);
			this->desktop_scale = std::clamp(fit_h, 0.45f, 0.75f);
		} else {
			this->desktop_scale = 0.55f;
		}
	}
#endif
	this->force_single = GetDriverParamBool(param, "single");
	this->smoke_test_frames = std::max(0, GetDriverParamInt(param, "smoke_test_frames", 0));
	this->smoke_test_reconfigure = GetDriverParamBool(param, "smoke_test_reconfigure");

	bool dual_active = !this->force_single;
#ifdef __ANDROID__
	this->last_secondary_surface_gen = AndroidDS::GetSecondaryWindowGeneration();
	if (this->smoke_test_frames == 0 && !AndroidDS::HasSecondaryWindow()) {
		dual_active = false;
	}
#endif

	if (dual_active) {
		MigrateLegacyDualScreenScale(this->smoke_test_frames);
	}

	DualScreen::SetGeometry(this->cfg_top_w, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h, dual_active);
	FontCache::LoadFontCaches(FONTSIZES_ALL);
	LoadStringWidthTable();

	if (!this->CreateDisplayOutputs(dual_active)) {
		this->DestroyAllOutputs();
		return SDL_GetError();
	}

	if (!this->AllocateBackingStore(this->cfg_top_w, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h, dual_active)) {
		this->DestroyAllOutputs();
		return "SDL3 DS: Failed to allocate backing store";
	}

	CopyPalette(this->local_palette, true);
	BlitterFactory::GetCurrentBlitter()->PostResize();
	GameSizeChanged();

	_cursor.in_window = true;
	_cursor.pos = { this->cfg_top_w / 2, this->cfg_top_h / 2 };
	DualScreen::SetTopCursorPos(_cursor.pos);

	const char *dname = SDL_GetCurrentVideoDriver();
	this->driver_info = fmt::format("sdl_ds ({}, {})", dname != nullptr ? dname : "unknown", dual_active ? "dual" : "single");
	Debug(Facility::Driver, Severity::Error, "SDL3 Dual-Screen initialized: {}", this->driver_info);

	this->OpenConnectedGamepads();
	this->last_input_time = std::chrono::steady_clock::now();
	this->last_zoom_time = this->last_input_time;

	SDL_StopTextInput(this->top_window);
	this->edit_box_focused = false;
	this->is_game_threaded = !GetDriverParamBool(param, "no_threads") && !GetDriverParamBool(param, "no_thread");

	return std::nullopt;
}

void VideoDriver_SDL_DS::Stop()
{
	this->CloseAllGamepads();
	this->DestroyAllOutputs();

	if (SDL_WasInit(SDL_INIT_GAMEPAD)) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_QuitSubSystem(SDL_INIT_VIDEO);
	if (!SDL_WasInit(SDL_INIT_AUDIO)) {
		SDL_Quit();
	}
}

bool VideoDriver_SDL_DS::CreateDisplayOutputs(bool dual_active)
{
	if (this->top_window == nullptr) {
#ifdef __ANDROID__
		SDL_WindowFlags top_flags = SDL_WINDOW_FULLSCREEN;
		int disp_top_w = this->cfg_top_w;
		int disp_top_h = this->cfg_top_h;
#else
		SDL_WindowFlags top_flags = SDL_WINDOW_RESIZABLE;
		int disp_top_w = std::max(320, static_cast<int>(std::lround(this->cfg_top_w * this->desktop_scale)));
		int disp_top_h = std::max(240, static_cast<int>(std::lround(this->cfg_top_h * this->desktop_scale)));
#endif
		this->top_window = SDL_CreateWindow("OpenTTD DS - Top Screen (AYN Thor 16:9)", disp_top_w, disp_top_h, top_flags);
		if (this->top_window == nullptr) return false;

		this->top_renderer = SDL_CreateRenderer(this->top_window, nullptr);
		if (this->top_renderer == nullptr) return false;
	}

	if (this->top_texture != nullptr) {
		SDL_DestroyTexture(this->top_texture);
		this->top_texture = nullptr;
	}
	this->top_texture = SDL_CreateTexture(this->top_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, this->cfg_top_w, this->cfg_top_h);
	if (this->top_texture == nullptr) return false;
	SDL_SetTextureScaleMode(this->top_texture, SDL_SCALEMODE_LINEAR);

#ifndef __ANDROID__
	if (dual_active) {
		int disp_top_w = 0, disp_top_h = 0;
		SDL_GetWindowSize(this->top_window, &disp_top_w, &disp_top_h);
		int disp_bot_w = std::max(320, static_cast<int>(std::lround(this->cfg_bot_w * this->desktop_scale)));
		int disp_bot_h = std::max(240, static_cast<int>(std::lround(this->cfg_bot_h * this->desktop_scale)));

		SDL_Rect usable{};
		SDL_DisplayID disp_id = SDL_GetDisplayForWindow(this->top_window);
		if (disp_id == 0 || !SDL_GetDisplayUsableBounds(disp_id, &usable)) {
			usable = { 0, 0, 1920, 1080 };
		}

		int top_x = usable.x + std::max(0, (usable.w - disp_top_w) / 2);
		int total_stack_h = disp_top_h + disp_bot_h + 44;
		int top_y = usable.y + std::max(32, (usable.h - total_stack_h) / 2);
		SDL_SetWindowPosition(this->top_window, top_x, top_y);

		if (this->bot_window == nullptr) {
			this->bot_window = SDL_CreateWindow("OpenTTD DS - Bottom Command Desk (AYN Thor 1240x1080)", disp_bot_w, disp_bot_h, SDL_WINDOW_RESIZABLE);
			if (this->bot_window == nullptr) return false;

			this->bot_renderer = SDL_CreateRenderer(this->bot_window, nullptr);
			if (this->bot_renderer == nullptr) return false;
		}
		int bot_x = top_x + (disp_top_w - disp_bot_w) / 2;
		int bot_y = std::min(usable.y + usable.h - disp_bot_h - 8, top_y + disp_top_h + 36);
		SDL_SetWindowPosition(this->bot_window, bot_x, bot_y);

		if (this->bot_texture != nullptr) {
			SDL_DestroyTexture(this->bot_texture);
			this->bot_texture = nullptr;
		}
		this->bot_texture = SDL_CreateTexture(this->bot_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, this->cfg_bot_w, this->cfg_bot_h);
		if (this->bot_texture == nullptr) return false;
		SDL_SetTextureScaleMode(this->bot_texture, SDL_SCALEMODE_LINEAR);
	} else {
		this->DestroySecondaryDesktopOutput();
	}
#else
	(void)dual_active;
#endif

	return true;
}

void VideoDriver_SDL_DS::DestroySecondaryDesktopOutput()
{
	if (this->bot_texture != nullptr) {
		SDL_DestroyTexture(this->bot_texture);
		this->bot_texture = nullptr;
	}
	if (this->bot_renderer != nullptr) {
		SDL_DestroyRenderer(this->bot_renderer);
		this->bot_renderer = nullptr;
	}
	if (this->bot_window != nullptr) {
		SDL_DestroyWindow(this->bot_window);
		this->bot_window = nullptr;
	}
}

void VideoDriver_SDL_DS::DestroyAllOutputs()
{
	this->DestroySecondaryDesktopOutput();
	if (this->top_texture != nullptr) {
		SDL_DestroyTexture(this->top_texture);
		this->top_texture = nullptr;
	}
	if (this->top_renderer != nullptr) {
		SDL_DestroyRenderer(this->top_renderer);
		this->top_renderer = nullptr;
	}
	if (this->top_window != nullptr) {
		SDL_DestroyWindow(this->top_window);
		this->top_window = nullptr;
	}
}

bool VideoDriver_SDL_DS::AllocateBackingStore(int top_w, int top_h, int bot_w, int bot_h, bool dual_active)
{
	int virt_w = dual_active ? std::max(top_w, bot_w) : top_w;
	int virt_h = dual_active ? (top_h + bot_h) : top_h;
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	this->framebuffer_32.assign(static_cast<size_t>(virt_w) * virt_h, 0xFF000000u);
	if (bpp == 8) {
		this->framebuffer_8.assign(static_cast<size_t>(virt_w) * virt_h, 0);
	} else {
		this->framebuffer_8.clear();
	}

	_screen.width = virt_w;
	_screen.height = virt_h;
	_screen.pitch = virt_w;
	_screen.dst_ptr = (bpp == 8) ? static_cast<void *>(this->framebuffer_8.data()) : static_cast<void *>(this->framebuffer_32.data());
	this->dirty_rect = { 0, 0, virt_w, virt_h };
	return true;
}

void VideoDriver_SDL_DS::ReconfigureMode(bool dual_active)
{
	bool was_dual = DualScreen::IsActive();

	/* Reset transient pointer/touch gesture state across display geometry changes. */
	this->mouse_trackpad_dragging = false;
	this->mouse_trackpad_moved = false;
	this->mouse_left_converted_to_right = false;
#ifdef __ANDROID__
	this->bot_trackpad_active = false;
	this->bot_trackpad_moved = false;
	this->bot_trackpad_pointer_id = -1;
#endif
	for (auto &f : this->top_fingers) f.active = false;
	this->top_active_fingers = 0;
	this->top_pinch_active = false;
	DualScreen::ClearTrackpadDragLock();
	_input_pressed.Reset({ControlInput::Left, ControlInput::Right});
	_input_clicked.Reset(ControlInput::Left);

	if (dual_active) {
		MigrateLegacyDualScreenScale(this->smoke_test_frames);
	}

	DualScreen::SetGeometry(this->cfg_top_w, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h, dual_active);
	if (!dual_active) {
		DualScreen::SetTrackpadMode(false);
	}
	int pre_scale = _gui_scale;
	ZoomLevel pre_zoom = _gui_zoom;
	if (was_dual != dual_active) {
		FontCache::LoadFontCaches(FONTSIZES_ALL);
		FontCache::ClearFontCaches(FONTSIZES_ALL);
		LoadStringWidthTable();
	}
	this->CreateDisplayOutputs(dual_active);
	this->AllocateBackingStore(this->cfg_top_w, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h, dual_active);
	CopyPalette(this->local_palette, true);
	BlitterFactory::GetCurrentBlitter()->PostResize();
	GameSizeChanged();
	if (was_dual != dual_active && pre_scale == _gui_scale && pre_zoom == _gui_zoom) {
		UpdateAllVirtCoords();
		ReInitAllWindows(true);
	}
	if (dual_active) {
		DualScreen::ShowBottomCommandDesk();
		if (!was_dual) {
			DualScreen::MigrateWindowsToDefaultScreens();
			RelocateAllWindows(_screen.width, _screen.height);
		}
	} else {
		DualScreen::RelocateDualScreenWindows();
	}

	/* Keep cursor inside valid bounds after virtual framebuffer size changes. */
	if (dual_active) {
		auto scr = DualScreen::GetScreenAtPoint(_cursor.pos.x, _cursor.pos.y);
		_cursor.pos = DualScreen::ClampPointToScreen(_cursor.pos.x, _cursor.pos.y, scr);
	} else {
		_cursor.pos = DualScreen::ClampPointToScreen(_cursor.pos.x, _cursor.pos.y, DualScreen::ScreenTarget::Top);
		DualScreen::SetTopCursorPos(_cursor.pos);
	}
	_cursor.delta = { 0, 0 };

	MarkWholeScreenDirty();
}

void VideoDriver_SDL_DS::MakeDirty(int left, int top, int width, int height)
{
	Rect r = { left, top, left + width, top + height };
	this->dirty_rect = BoundingRect(this->dirty_rect, r);
}

void VideoDriver_SDL_DS::CheckPaletteAnim()
{
	if (!CopyPalette(this->local_palette)) return;
	this->MakeDirty(0, 0, _screen.width, _screen.height);
}

bool VideoDriver_SDL_DS::LockVideoBuffer()
{
	if (this->buffer_locked) return false;
	this->buffer_locked = true;
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	_screen.dst_ptr = (bpp == 8) ? static_cast<void *>(this->framebuffer_8.data()) : static_cast<void *>(this->framebuffer_32.data());
	return true;
}

void VideoDriver_SDL_DS::UnlockVideoBuffer()
{
	this->buffer_locked = false;
}

void VideoDriver_SDL_DS::Convert8bppDirtyTo32bpp(const Rect &dirty)
{
	if (this->framebuffer_8.empty()) return;
	int x0 = std::max(0, dirty.left);
	int y0 = std::max(0, dirty.top);
	int x1 = std::min(_screen.width, dirty.right);
	int y1 = std::min(_screen.height, dirty.bottom);
	for (int y = y0; y < y1; ++y) {
		const uint8_t *src = this->framebuffer_8.data() + static_cast<size_t>(y) * _screen.width;
		uint32_t *dst = this->framebuffer_32.data() + static_cast<size_t>(y) * _screen.width;
		for (int x = x0; x < x1; ++x) {
			const Colour &c = this->local_palette.palette[src[x]];
			dst[x] = 0xFF000000u | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
		}
	}
}

void VideoDriver_SDL_DS::PresentTopRegion(const Rect &dirty)
{
	if (this->top_renderer == nullptr || this->top_texture == nullptr) return;
	int x0 = std::max(0, dirty.left);
	int y0 = std::max(0, dirty.top);
	int x1 = std::min(this->cfg_top_w, dirty.right);
	int y1 = std::min(this->cfg_top_h, dirty.bottom);
	if (x1 > x0 && y1 > y0) {
		SDL_Rect sr{ x0, y0, x1 - x0, y1 - y0 };
		const uint32_t *src = this->framebuffer_32.data() + static_cast<size_t>(y0) * _screen.width + x0;
		SDL_UpdateTexture(this->top_texture, &sr, src, _screen.width * static_cast<int>(sizeof(uint32_t)));
	}
	SDL_RenderClear(this->top_renderer);
	SDL_RenderTexture(this->top_renderer, this->top_texture, nullptr, nullptr);
	SDL_RenderPresent(this->top_renderer);
}

void VideoDriver_SDL_DS::PresentBottomRegion(const Rect &dirty)
{
	if (!DualScreen::IsActive()) return;
	int bot_y0 = this->cfg_top_h;
	int bot_y1 = this->cfg_top_h + this->cfg_bot_h;

#ifndef __ANDROID__
	if (this->bot_renderer == nullptr || this->bot_texture == nullptr) return;
	int x0 = std::max(0, dirty.left);
	int y0 = std::max(bot_y0, dirty.top);
	int x1 = std::min(this->cfg_bot_w, dirty.right);
	int y1 = std::min(bot_y1, dirty.bottom);
	if (x1 > x0 && y1 > y0) {
		SDL_Rect sr{ x0, y0 - bot_y0, x1 - x0, y1 - y0 };
		const uint32_t *src = this->framebuffer_32.data() + static_cast<size_t>(y0) * _screen.width + x0;
		SDL_UpdateTexture(this->bot_texture, &sr, src, _screen.width * static_cast<int>(sizeof(uint32_t)));
	}
	SDL_RenderClear(this->bot_renderer);
	SDL_RenderTexture(this->bot_renderer, this->bot_texture, nullptr, nullptr);
	SDL_RenderPresent(this->bot_renderer);
#else
	if (dirty.right <= 0 || dirty.left >= this->cfg_bot_w || dirty.bottom <= bot_y0 || dirty.top >= bot_y1) return;
	ANativeWindow *nw = AndroidDS::AcquireSecondaryWindow();
	if (nw == nullptr) return;

	if (ANativeWindow_getWidth(nw) != this->cfg_bot_w ||
			ANativeWindow_getHeight(nw) != this->cfg_bot_h ||
			ANativeWindow_getFormat(nw) != WINDOW_FORMAT_RGBX_8888) {
		ANativeWindow_setBuffersGeometry(nw, this->cfg_bot_w, this->cfg_bot_h, WINDOW_FORMAT_RGBX_8888);
	}
	ANativeWindow_Buffer buf{};
	if (ANativeWindow_lock(nw, &buf, nullptr) == 0 && buf.bits != nullptr) {
		if (buf.width > 0 && buf.height > 0) {
			int copy_w = std::min(this->cfg_bot_w, buf.width);
			int copy_h = std::min(this->cfg_bot_h, buf.height);
			if (buf.stride >= copy_w) {
				for (int y = 0; y < copy_h; ++y) {
					const uint32_t *src = this->framebuffer_32.data() + static_cast<size_t>(bot_y0 + y) * _screen.width;
					uint32_t *dst = reinterpret_cast<uint32_t *>(buf.bits) + static_cast<size_t>(y) * buf.stride;
					for (int x = 0; x < copy_w; ++x) {
						uint32_t px = src[x];
						uint32_t r = (px >> 16) & 0xFFu;
						uint32_t g = (px >> 8) & 0xFFu;
						uint32_t b = px & 0xFFu;
						dst[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
					}
				}
			}
		}
		ANativeWindow_unlockAndPost(nw);
	}
	AndroidDS::ReleaseSecondaryWindow(nw);
#endif
}

void VideoDriver_SDL_DS::CheckSmokeTestCompletion()
{
	if (this->smoke_test_frames <= 0) return;
	++this->rendered_frames;
	if (this->rendered_frames < this->smoke_test_frames) return;

	uint32_t top_first = this->framebuffer_32[0];
	int top_varied = 0;
	for (int y = 0; y < this->cfg_top_h; y += 8) {
		const uint32_t *row = this->framebuffer_32.data() + static_cast<size_t>(y) * _screen.width;
		for (int x = 0; x < this->cfg_top_w; x += 8) {
			if (row[x] != top_first) ++top_varied;
		}
	}

	int bot_varied = 0;
	if (DualScreen::IsActive()) {
		uint32_t bot_first = this->framebuffer_32[static_cast<size_t>(this->cfg_top_h) * _screen.width];
		for (int y = this->cfg_top_h; y < this->cfg_top_h + this->cfg_bot_h; y += 8) {
			const uint32_t *row = this->framebuffer_32.data() + static_cast<size_t>(y) * _screen.width;
			for (int x = 0; x < this->cfg_bot_w; x += 8) {
				if (row[x] != bot_first) ++bot_varied;
			}
		}
	}

	bool dual = DualScreen::IsActive();
	bool windows_fit = true;
	if (dual) {
		for (const Window *w : Window::Iterate()) {
			switch (w->window_class) {
				case WindowClass::SelectGame:
				case WindowClass::MainToolbar:
				case WindowClass::Statusbar:
				case WindowClass::DsActionBar:
				case WindowClass::SmallMap:
					if (w->width <= 0 || w->height <= 0 ||
							w->width > this->cfg_bot_w || w->height > this->cfg_bot_h ||
							w->left < 0 || w->left + w->width > this->cfg_bot_w ||
							w->top < this->cfg_top_h || w->top + w->height > this->cfg_top_h + this->cfg_bot_h) {
						Debug(Facility::Driver, Severity::Critical,
							"SMOKE_TEST_WINDOW_OVERFLOW class={} pos=({},{}) size={}x{} bot={}x{} y0={}",
							static_cast<int>(w->window_class), w->left, w->top, w->width, w->height,
							this->cfg_bot_w, this->cfg_bot_h, this->cfg_top_h);
						windows_fit = false;
					}
					break;
				default:
					break;
			}
		}
	}

	bool ok = (top_varied > 0) && (!dual || (bot_varied > 0 && this->smoke_has_desk)) &&
		this->smoke_has_ui && windows_fit && this->smoke_reconfig_ok;
	if (this->smoke_test_reconfigure && this->smoke_reconfig_phase < 2) {
		ok = false;
	}
	Debug(Facility::Driver, Severity::Critical,
		"{} frames={} dual={} gui_scale={} top_varied={} bot_varied={} desk={} ui={} fit={} reconfig={}",
		ok ? "SMOKE_TEST_OK" : "SMOKE_TEST_FAIL",
		this->rendered_frames, dual ? 1 : 0, _gui_scale, top_varied, bot_varied,
		this->smoke_has_desk ? 1 : 0, this->smoke_has_ui ? 1 : 0,
		windows_fit ? 1 : 0,
		this->smoke_test_reconfigure ? (this->smoke_reconfig_ok ? 1 : 0) : -1);
	std::fflush(stderr);

	if (!ok) {
		std::_Exit(1);
	}
	_exit_game = true;
}

void VideoDriver_SDL_DS::Paint()
{
	PerformanceMeasurer framerate(PerformanceElement::Video);

	if (IsEmptyRect(this->dirty_rect) && this->local_palette.count_dirty == 0 && this->smoke_test_frames == 0) return;

	if (this->local_palette.count_dirty != 0) {
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();
		switch (blitter->UsePaletteAnimation()) {
			case Blitter::PaletteAnimation::VideoBackend:
			case Blitter::PaletteAnimation::Blitter:
				blitter->PaletteAnimate(this->local_palette);
				break;
			case Blitter::PaletteAnimation::None:
				break;
			default:
				NOT_REACHED();
		}
		this->local_palette.count_dirty = 0;
	}

	Rect dirty = this->dirty_rect;
	if (!this->framebuffer_8.empty() && !IsEmptyRect(dirty)) {
		this->Convert8bppDirtyTo32bpp(dirty);
	}

	if (!IsEmptyRect(dirty)) {
		this->PresentTopRegion(dirty);
		this->PresentBottomRegion(dirty);
	}

	this->dirty_rect = {};
	this->CheckSmokeTestCompletion();
}

void VideoDriver_SDL_DS::MainLoop()
{
	this->StartGameThread();

	while (!_exit_game) {
		this->Tick();
		this->SleepTillNextTick();
	}

	this->StopGameThread();
}

bool VideoDriver_SDL_DS::PollEvent()
{
#ifdef __ANDROID__
	this->DrainSecondaryTouches();
#endif

	SDL_Event ev;
	if (!SDL_PollEvent(&ev)) return false;

	this->HandleSDLEvent(ev);
	return true;
}

void VideoDriver_SDL_DS::HandleDesktopMouseEvent(const SDL_Event &ev)
{
	SDL_WindowID wid = 0;
	float fx = 0.0f;
	float fy = 0.0f;
	if (ev.type == SDL_EVENT_MOUSE_MOTION) {
		wid = ev.motion.windowID;
		fx = ev.motion.x;
		fy = ev.motion.y;
	} else {
		wid = ev.button.windowID;
		fx = ev.button.x;
		fy = ev.button.y;
	}

	bool on_bottom_window = (this->bot_window != nullptr && wid == SDL_GetWindowID(this->bot_window));
	SDL_Window *src_win = on_bottom_window ? this->bot_window : this->top_window;
	int win_w = 1, win_h = 1;
	if (src_win != nullptr) {
		SDL_GetWindowSize(src_win, &win_w, &win_h);
		win_w = std::max(1, win_w);
		win_h = std::max(1, win_h);
	}

	int vx = 0;
	int vy = 0;
	if (on_bottom_window && DualScreen::IsActive()) {
		vx = Clamp(static_cast<int>(std::lround(fx * this->cfg_bot_w / win_w)), 0, this->cfg_bot_w - 1);
		vy = this->cfg_top_h + Clamp(static_cast<int>(std::lround(fy * this->cfg_bot_h / win_h)), 0, this->cfg_bot_h - 1);
	} else {
		vx = Clamp(static_cast<int>(std::lround(fx * this->cfg_top_w / win_w)), 0, this->cfg_top_w - 1);
		vy = Clamp(static_cast<int>(std::lround(fy * this->cfg_top_h / win_h)), 0, this->cfg_top_h - 1);
	}

	/* Handle bottom-screen trackpad interaction smoothly on desktop too. */
	if (this->mouse_trackpad_dragging && (!DualScreen::IsTrackpadMode() || (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT))) {
		bool was_moved = this->mouse_trackpad_moved;
		this->mouse_trackpad_dragging = false;
		if (DualScreen::IsTrackpadMode() && ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT) {
			if (!was_moved) {
				Point top_pt = DualScreen::GetTopCursorPos();
				SendSyntheticMouseClick(top_pt.x, top_pt.y, ConsumeRightClickAndInvalidate());
			}
			return;
		}
	}

	if (on_bottom_window && DualScreen::IsTrackpadMode()) {
		if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT && IsPointOnActiveTrackpadPad(vx, vy)) {
			this->mouse_trackpad_dragging = true;
			this->mouse_trackpad_moved = false;
			this->mouse_trackpad_start = { vx, vy };
			this->mouse_trackpad_last = { vx, vy };
			return;
		}
		if (this->mouse_trackpad_dragging && ev.type == SDL_EVENT_MOUSE_MOTION) {
			int dx = vx - this->mouse_trackpad_last.x;
			int dy = vy - this->mouse_trackpad_last.y;
			if (std::abs(vx - this->mouse_trackpad_start.x) > 4 || std::abs(vy - this->mouse_trackpad_start.y) > 4) {
				this->mouse_trackpad_moved = true;
			}
			this->mouse_trackpad_last = { vx, vy };

			Point cur_top = DualScreen::GetTopCursorPos();
			Point top_pt = DualScreen::ClampPointToScreen(cur_top.x + dx * 2, cur_top.y + dy * 2, DualScreen::ScreenTarget::Top);
			DualScreen::SetTopCursorPos(top_pt);
			_cursor.UpdateCursorPosition(top_pt.x, top_pt.y);
			DualScreen::InvalidateDualScreenControls();
			HandleMouseEvents();
			return;
		}
	}

	if (on_bottom_window && DualScreen::IsTrackpadDragLocked()) {
		if (ev.type == SDL_EVENT_MOUSE_MOTION) {
			/* Keep locked Top-screen drag intact while moving cursor over Bottom controls. */
			return;
		}
		if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
			if (IsPointOnActiveTrackpadDragButton(vx, vy)) {
				ReleaseTrackpadDragLockAtTopCursor();
				return;
			}
			ReleaseTrackpadDragLockAtTopCursor();
		}
	}

	if (!on_bottom_window) {
		DualScreen::SetTopCursorPos({ vx, vy });
	}

	if (ev.type == SDL_EVENT_MOUSE_MOTION) {
		_cursor.UpdateCursorPosition(vx, vy);
		HandleMouseEvents();
	} else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		_cursor.UpdateCursorPosition(vx, vy);
		Uint8 btn = ev.button.button;
		if (btn == SDL_BUTTON_LEFT && ConsumeRightClickAndInvalidate()) {
			btn = SDL_BUTTON_RIGHT;
			this->mouse_left_converted_to_right = true;
		}
		if (_rightclick_emulate && (SDL_GetModState() & SDL_KMOD_CTRL)) {
			btn = SDL_BUTTON_RIGHT;
		}
		switch (btn) {
			case SDL_BUTTON_LEFT:   _input_pressed.Set(ControlInput::Left); break;
			case SDL_BUTTON_RIGHT:  _input_pressed.Set(ControlInput::Right); _input_clicked.Set(ControlInput::Right); break;
			case SDL_BUTTON_MIDDLE: _input_pressed.Set(ControlInput::Middle); _input_clicked.Set(ControlInput::Middle); break;
			case SDL_BUTTON_X1:     _input_pressed.Set(ControlInput::X1); _input_clicked.Set(ControlInput::X1); break;
			case SDL_BUTTON_X2:     _input_pressed.Set(ControlInput::X2); _input_clicked.Set(ControlInput::X2); break;
			default: break;
		}
		HandleMouseEvents();
	} else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (ev.button.button == SDL_BUTTON_LEFT && on_bottom_window && DualScreen::IsTrackpadDragLocked()) {
			/* Keep ControlInput::Left held while trackpad drag-lock is active (e.g., releasing the Drag Lock button). */
			this->mouse_left_converted_to_right = false;
			return;
		}
		_cursor.UpdateCursorPosition(vx, vy);
		if (_rightclick_emulate) {
			_input_pressed.Reset({ControlInput::Left, ControlInput::Right});
			_input_clicked.Reset(ControlInput::Left);
			this->mouse_left_converted_to_right = false;
		} else {
			switch (ev.button.button) {
				case SDL_BUTTON_LEFT:
					_input_pressed.Reset(ControlInput::Left);
					_input_clicked.Reset(ControlInput::Left);
					if (this->mouse_left_converted_to_right) {
						_input_pressed.Reset(ControlInput::Right);
						this->mouse_left_converted_to_right = false;
					}
					break;
				case SDL_BUTTON_RIGHT:  _input_pressed.Reset(ControlInput::Right); break;
				case SDL_BUTTON_MIDDLE: _input_pressed.Reset(ControlInput::Middle); break;
				case SDL_BUTTON_X1:     _input_pressed.Reset(ControlInput::X1); break;
				case SDL_BUTTON_X2:     _input_pressed.Reset(ControlInput::X2); break;
				default: break;
			}
		}
		if (DualScreen::IsTrackpadDragLocked() && (_rightclick_emulate || ev.button.button == SDL_BUTTON_LEFT)) {
			DualScreen::ClearTrackpadDragLock();
			DualScreen::InvalidateDualScreenControls();
		}
		HandleMouseEvents();
	}
}

void VideoDriver_SDL_DS::HandleTopTouchEvent(const SDL_Event &ev)
{
	if (ev.tfinger.touchID == SDL_MOUSE_TOUCHID) return;

	Point vpt = DualScreen::MapTopNormalizedToVirtual(ev.tfinger.x, ev.tfinger.y);
	DualScreen::SetTopCursorPos(vpt);
	uint64_t fid = static_cast<uint64_t>(ev.tfinger.fingerID);
	auto now = std::chrono::steady_clock::now();

	if (ev.type == SDL_EVENT_FINGER_DOWN) {
		for (auto &f : this->top_fingers) {
			if (!f.active) {
				f.active = true;
				f.id = fid;
				f.norm_x = ev.tfinger.x;
				f.norm_y = ev.tfinger.y;
				f.start_virt = vpt;
				f.last_virt = vpt;
				f.dragged = false;
				f.down_time = now;
				++this->top_active_fingers;
				break;
			}
		}

		if (this->top_active_fingers == 2) {
			FingerState *f0 = nullptr;
			FingerState *f1 = nullptr;
			for (auto &f : this->top_fingers) {
				if (f.active) {
					if (f0 == nullptr) f0 = &f;
					else if (f1 == nullptr) f1 = &f;
				}
			}
			if (f0 != nullptr && f1 != nullptr) {
				this->top_pinch_active = true;
				this->top_prev_centroid_x = 0.5f * (f0->last_virt.x + f1->last_virt.x);
				this->top_prev_centroid_y = 0.5f * (f0->last_virt.y + f1->last_virt.y);
				float dx = static_cast<float>(f1->last_virt.x - f0->last_virt.x);
				float dy = static_cast<float>(f1->last_virt.y - f0->last_virt.y);
				this->top_prev_dist = std::max(20.0f, std::hypot(dx, dy));
				_input_pressed.Reset(ControlInput::Left);
				_input_clicked.Reset(ControlInput::Left);
				HandleMouseEvents();
			}
		}
		return;
	}

	FingerState *cur = nullptr;
	for (auto &f : this->top_fingers) {
		if (f.active && f.id == fid) {
			cur = &f;
			break;
		}
	}
	if (cur == nullptr) return;

	if (ev.type == SDL_EVENT_FINGER_MOTION) {
		int dx = vpt.x - cur->last_virt.x;
		int dy = vpt.y - cur->last_virt.y;
		cur->norm_x = ev.tfinger.x;
		cur->norm_y = ev.tfinger.y;
		cur->last_virt = vpt;

		if (this->top_active_fingers >= 2 && this->top_pinch_active) {
			FingerState *f0 = nullptr;
			FingerState *f1 = nullptr;
			for (auto &f : this->top_fingers) {
				if (f.active) {
					if (f0 == nullptr) f0 = &f;
					else if (f1 == nullptr) f1 = &f;
				}
			}
			if (f0 != nullptr && f1 != nullptr) {
				float cx = 0.5f * (f0->last_virt.x + f1->last_virt.x);
				float cy = 0.5f * (f0->last_virt.y + f1->last_virt.y);
				PanMainViewportPixels(static_cast<int>(std::lround(this->top_prev_centroid_x - cx)),
					static_cast<int>(std::lround(this->top_prev_centroid_y - cy)));
				this->top_prev_centroid_x = cx;
				this->top_prev_centroid_y = cy;

				float dist = std::max(20.0f, std::hypot(static_cast<float>(f1->last_virt.x - f0->last_virt.x), static_cast<float>(f1->last_virt.y - f0->last_virt.y)));
				if (Window *mw = GetMainWindow(); mw != nullptr) {
					if (dist > this->top_prev_dist * 1.18f) {
						DoZoomInOutWindow(ZOOM_IN, mw);
						this->top_prev_dist = dist;
					} else if (dist < this->top_prev_dist * 0.84f) {
						DoZoomInOutWindow(ZOOM_OUT, mw);
						this->top_prev_dist = dist;
					}
				}
			}
			return;
		}

		if (this->top_active_fingers == 1 && !this->top_pinch_active) {
			if (!cur->dragged && (std::abs(vpt.x - cur->start_virt.x) >= 10 || std::abs(vpt.y - cur->start_virt.y) >= 10)) {
				cur->dragged = true;
				if (_thd.place_mode != HT_NONE) {
					_cursor.UpdateCursorPosition(cur->start_virt.x, cur->start_virt.y);
					_input_pressed.Set(ControlInput::Left);
					HandleMouseEvents();
				}
			}
			if (cur->dragged) {
				if (_thd.place_mode != HT_NONE) {
					_cursor.UpdateCursorPosition(vpt.x, vpt.y);
					HandleMouseEvents();
				} else {
					PanMainViewportPixels(-dx, -dy);
				}
			}
		}
		return;
	}

	if (ev.type == SDL_EVENT_FINGER_UP || ev.type == SDL_EVENT_FINGER_CANCELED) {
		bool was_single = (this->top_active_fingers == 1 && !this->top_pinch_active);
		bool was_dragged = cur->dragged;
		auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cur->down_time).count();

		cur->active = false;
		this->top_active_fingers = std::max(0, this->top_active_fingers - 1);
		if (this->top_active_fingers < 2) {
			this->top_pinch_active = false;
		}

		if (was_single && ev.type == SDL_EVENT_FINGER_UP) {
			if (was_dragged && _thd.place_mode != HT_NONE) {
				_cursor.UpdateCursorPosition(vpt.x, vpt.y);
				_input_pressed.Reset(ControlInput::Left);
				_input_clicked.Reset(ControlInput::Left);
				HandleMouseEvents();
			} else if (!was_dragged) {
				bool right = (held_ms >= 500) || ConsumeRightClickAndInvalidate();
				SendSyntheticMouseClick(vpt.x, vpt.y, right);
			}
		}
	}
}

void VideoDriver_SDL_DS::DrainSecondaryTouches()
{
#ifdef __ANDROID__
	AndroidDS::SecondaryTouchEvent ev{};
	while (AndroidDS::PopSecondaryTouch(ev)) {
		if (!DualScreen::IsActive()) continue;
		Point vpt = DualScreen::MapBottomNormalizedToVirtual(ev.norm_x, ev.norm_y);
		constexpr int ACTION_DOWN = 0;
		constexpr int ACTION_UP = 1;
		constexpr int ACTION_MOVE = 2;
		constexpr int ACTION_CANCEL = 3;

		DualScreen::TrackpadTouchState tp_state{
			this->bot_trackpad_active,
			this->bot_trackpad_moved,
			this->bot_trackpad_pointer_id,
			this->bot_trackpad_start,
			this->bot_trackpad_last,
		};
		bool on_pad = (ev.action_masked == ACTION_DOWN) && IsPointOnActiveTrackpadPad(vpt.x, vpt.y);
		DualScreen::TrackpadTouchResult res = DualScreen::ProcessTrackpadTouch(
			tp_state, ev.action_masked, ev.pointer_id, ev.pointer_count, vpt, on_pad);
		this->bot_trackpad_active = tp_state.active;
		this->bot_trackpad_moved = tp_state.moved;
		this->bot_trackpad_pointer_id = tp_state.pointer_id;
		this->bot_trackpad_start = tp_state.start;
		this->bot_trackpad_last = tp_state.last;

		switch (res) {
			case DualScreen::TrackpadTouchResult::None:
				break;

			case DualScreen::TrackpadTouchResult::CursorMoved: {
				Point top_pt = DualScreen::GetTopCursorPos();
				_cursor.UpdateCursorPosition(top_pt.x, top_pt.y);
				DualScreen::InvalidateDualScreenControls();
				HandleMouseEvents();
				break;
			}

			case DualScreen::TrackpadTouchResult::TwoFingerClick: {
				Point top_pt = DualScreen::GetTopCursorPos();
				SendSyntheticMouseClick(top_pt.x, top_pt.y, true);
				break;
			}

			case DualScreen::TrackpadTouchResult::TapClick: {
				Point top_pt = DualScreen::GetTopCursorPos();
				SendSyntheticMouseClick(top_pt.x, top_pt.y, ConsumeRightClickAndInvalidate());
				break;
			}

			case DualScreen::TrackpadTouchResult::Passthrough:
				if (ev.action_masked == ACTION_DOWN) {
					if (DualScreen::IsTrackpadDragLocked()) {
						if (IsPointOnActiveTrackpadDragButton(vpt.x, vpt.y)) {
							ReleaseTrackpadDragLockAtTopCursor();
							break;
						}
						ReleaseTrackpadDragLockAtTopCursor();
					}
					_cursor.UpdateCursorPosition(vpt.x, vpt.y);
					if (ConsumeRightClickAndInvalidate()) {
						_input_pressed.Set(ControlInput::Right);
						_input_clicked.Set(ControlInput::Right);
					} else {
						_input_pressed.Set(ControlInput::Left);
					}
					HandleMouseEvents();
				} else if (ev.action_masked == ACTION_MOVE) {
					if (!DualScreen::IsTrackpadDragLocked()) {
						_cursor.UpdateCursorPosition(vpt.x, vpt.y);
						HandleMouseEvents();
					}
				} else if (ev.action_masked == ACTION_UP || ev.action_masked == ACTION_CANCEL) {
					if (!DualScreen::IsTrackpadDragLocked()) {
						_cursor.UpdateCursorPosition(vpt.x, vpt.y);
						_input_pressed.Reset({ControlInput::Left, ControlInput::Right});
						_input_clicked.Reset(ControlInput::Left);
						HandleMouseEvents();
					}
				}
				break;
		}
	}
#endif
}

void VideoDriver_SDL_DS::HandleGamepadButton(uint8_t button, bool pressed)
{
	switch (button) {
		case SDL_GAMEPAD_BUTTON_SOUTH: {
			if (DualScreen::IsActive() && DualScreen::GetScreenAtPoint(_cursor.pos.x, _cursor.pos.y) == DualScreen::ScreenTarget::Top) {
				Point top_pt = DualScreen::ClampPointToScreen(_cursor.pos.x, _cursor.pos.y, DualScreen::ScreenTarget::Top);
				_cursor.UpdateCursorPosition(top_pt.x, top_pt.y);
			}
			if (pressed) _input_pressed.Set(ControlInput::Left);
			else {
				_input_pressed.Reset(ControlInput::Left);
				_input_clicked.Reset(ControlInput::Left);
			}
			HandleMouseEvents();
			break;
		}
		case SDL_GAMEPAD_BUTTON_EAST:
			if (pressed && _thd.place_mode != HT_NONE) {
				ResetObjectToPlace();
			} else {
				if (pressed) {
					_input_pressed.Set(ControlInput::Right);
					_input_clicked.Set(ControlInput::Right);
				} else {
					_input_pressed.Reset(ControlInput::Right);
				}
				HandleMouseEvents();
			}
			break;
		case SDL_GAMEPAD_BUTTON_WEST:
			if (pressed) HandleKeypress(WKC_PAUSE, 0);
			break;
		case SDL_GAMEPAD_BUTTON_NORTH:
			if (pressed && !_networking && _game_mode != GameMode::Menu) {
				ChangeGameSpeed(_game_speed == 100);
			}
			break;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
			if (pressed) {
				if (Window *mw = GetMainWindow(); mw != nullptr) DoZoomInOutWindow(ZOOM_OUT, mw);
			}
			break;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
			if (pressed) {
				if (Window *mw = GetMainWindow(); mw != nullptr) DoZoomInOutWindow(ZOOM_IN, mw);
			}
			break;
		case SDL_GAMEPAD_BUTTON_BACK:
			if (pressed && DualScreen::IsActive()) {
				DualScreen::ToggleTrackpadMode();
				DualScreen::SyncTrackpadWindow();
			}
			break;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK:
			if (pressed) DualScreen::FocusOrToggleSmallMap();
			break;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
			if (pressed && DualScreen::IsActive()) DualScreen::SendActiveWindowToOtherScreen();
			break;
		case SDL_GAMEPAD_BUTTON_START:
			if (pressed) HandleKeypress(WKC_ESC, 0);
			break;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
			if (pressed) {
				const int step = (this->cfg_top_h >= 1000) ? 18 : 12;
				auto scr = DualScreen::GetScreenAtPoint(_cursor.pos.x, _cursor.pos.y);
				Point pt = DualScreen::ClampPointToScreen(_cursor.pos.x - step, _cursor.pos.y, scr);
				if (scr == DualScreen::ScreenTarget::Top) DualScreen::SetTopCursorPos(pt);
				_cursor.UpdateCursorPosition(pt.x, pt.y);
				HandleMouseEvents();
			}
			break;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
			if (pressed) {
				const int step = (this->cfg_top_h >= 1000) ? 18 : 12;
				auto scr = DualScreen::GetScreenAtPoint(_cursor.pos.x, _cursor.pos.y);
				Point pt = DualScreen::ClampPointToScreen(_cursor.pos.x + step, _cursor.pos.y, scr);
				if (scr == DualScreen::ScreenTarget::Top) DualScreen::SetTopCursorPos(pt);
				_cursor.UpdateCursorPosition(pt.x, pt.y);
				HandleMouseEvents();
			}
			break;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:
			if (pressed) {
				const int step = (this->cfg_top_h >= 1000) ? 18 : 12;
				int ny = std::max(0, _cursor.pos.y - step);
				auto scr = DualScreen::GetScreenAtPoint(_cursor.pos.x, ny);
				Point pt = DualScreen::ClampPointToScreen(_cursor.pos.x, ny, scr);
				if (scr == DualScreen::ScreenTarget::Top) DualScreen::SetTopCursorPos(pt);
				_cursor.UpdateCursorPosition(pt.x, pt.y);
				HandleMouseEvents();
			}
			break;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
			if (pressed) {
				const int step = (this->cfg_top_h >= 1000) ? 18 : 12;
				int ny = std::min(_screen.height - 1, _cursor.pos.y + step);
				auto scr = DualScreen::GetScreenAtPoint(_cursor.pos.x, ny);
				Point pt = DualScreen::ClampPointToScreen(_cursor.pos.x, ny, scr);
				if (scr == DualScreen::ScreenTarget::Top) DualScreen::SetTopCursorPos(pt);
				_cursor.UpdateCursorPosition(pt.x, pt.y);
				HandleMouseEvents();
			}
			break;
		default:
			break;
	}
}

void VideoDriver_SDL_DS::HandleSDLEvent(const SDL_Event &ev)
{
	switch (ev.type) {
		case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			this->HandleDesktopMouseEvent(ev);
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (ev.wheel.y > 0) _cursor.wheel--;
			else if (ev.wheel.y < 0) _cursor.wheel++;
			_cursor.v_wheel -= ev.wheel.y * 14.0f * _settings_client.gui.scrollwheel_multiplier;
			_cursor.h_wheel += ev.wheel.x * 14.0f * _settings_client.gui.scrollwheel_multiplier;
			_cursor.wheel_moved = true;
			break;

		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			if (ev.tfinger.touchID == SDL_MOUSE_TOUCHID) break;
			if (this->bot_window != nullptr && ev.tfinger.windowID == SDL_GetWindowID(this->bot_window)) {
				/* On desktop touchscreens touching the Bottom window directly simulates bottom touch. */
				Point vpt = DualScreen::MapBottomNormalizedToVirtual(ev.tfinger.x, ev.tfinger.y);
				_cursor.UpdateCursorPosition(vpt.x, vpt.y);
				if (ev.type == SDL_EVENT_FINGER_DOWN) {
					if (ConsumeRightClickAndInvalidate()) {
						_input_pressed.Set(ControlInput::Right);
						_input_clicked.Set(ControlInput::Right);
					} else {
						_input_pressed.Set(ControlInput::Left);
					}
				} else if (ev.type == SDL_EVENT_FINGER_UP || ev.type == SDL_EVENT_FINGER_CANCELED) {
					if (!DualScreen::IsTrackpadDragLocked()) {
						_input_pressed.Reset({ControlInput::Left, ControlInput::Right});
						_input_clicked.Reset(ControlInput::Left);
					}
				}
				HandleMouseEvents();
			} else {
				this->HandleTopTouchEvent(ev);
			}
			break;

		case SDL_EVENT_KEY_DOWN:
			if (ev.key.repeat == 0 && ev.key.key == SDLK_F9 && DualScreen::IsActive()) {
				DualScreen::ToggleTrackpadMode();
				DualScreen::SyncTrackpadWindow();
			} else if (ev.key.repeat == 0 && ev.key.key == SDLK_F10 && DualScreen::IsActive()) {
				DualScreen::SendActiveWindowToOtherScreen();
			} else if (ev.key.repeat == 0 && ev.key.key == SDLK_F11) {
				this->ReconfigureMode(!DualScreen::IsActive());
			} else if ((ev.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_F)) {
				if (ev.key.repeat == 0) ToggleFullScreen(!_fullscreen);
			} else {
				char32_t character;
				uint keycode = ConvertSdlKey(ev.key, &character);
				if (!this->edit_box_focused ||
					keycode == WKC_DELETE || keycode == WKC_NUM_ENTER ||
					keycode == WKC_LEFT || keycode == WKC_RIGHT || keycode == WKC_UP || keycode == WKC_DOWN ||
					keycode == WKC_HOME || keycode == WKC_END ||
					(keycode & WKC_META) || (keycode & WKC_CTRL) || (keycode & WKC_ALT) ||
					(keycode >= WKC_F1 && keycode <= WKC_F12) ||
					!IsValidChar(character, CS_ALPHANUMERAL)) {
					HandleKeypress(keycode, character);
				}
			}
			break;

		case SDL_EVENT_TEXT_INPUT: {
			if (!this->edit_box_focused) break;
			SDL_Keycode kc = SDL_GetKeyFromName(ev.text.text);
			uint keycode = ConvertSdlKeycode(kc);
			if (keycode == WKC_BACKQUOTE && FocusedWindowIsConsole()) {
				auto [len, c] = DecodeUtf8(ev.text.text);
				if (len > 0) HandleKeypress(keycode, c);
			} else {
				HandleTextInput(ev.text.text);
			}
			break;
		}

		case SDL_EVENT_GAMEPAD_ADDED:
			if (SDL_Gamepad *gp = SDL_OpenGamepad(ev.gdevice.which); gp != nullptr) {
				this->gamepads.push_back(gp);
			}
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			for (auto it = this->gamepads.begin(); it != this->gamepads.end(); ++it) {
				if (SDL_GetGamepadID(*it) == ev.gdevice.which) {
					SDL_CloseGamepad(*it);
					this->gamepads.erase(it);
					break;
				}
			}
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			this->HandleGamepadButton(ev.gbutton.button, true);
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			this->HandleGamepadButton(ev.gbutton.button, false);
			break;

		case SDL_EVENT_WINDOW_EXPOSED:
			this->MakeDirty(0, 0, _screen.width, _screen.height);
			break;

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		case SDL_EVENT_QUIT:
			HandleExitGameRequest();
			break;

		default:
			break;
	}
}

void VideoDriver_SDL_DS::UpdateGamepadInput(float dt_sec)
{
	if (this->gamepads.empty() || dt_sec <= 0.0f) return;

	float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f, lt = 0.0f, rt = 0.0f;
	for (SDL_Gamepad *gp : this->gamepads) {
		auto norm_axis = [](Sint16 v) -> float {
			float f = static_cast<float>(v) / 32767.0f;
			return (std::fabs(f) < 0.18f) ? 0.0f : f;
		};
		float cx = norm_axis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX));
		float cy = norm_axis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY));
		if (std::fabs(cx) > std::fabs(lx)) lx = cx;
		if (std::fabs(cy) > std::fabs(ly)) ly = cy;

		float crx = norm_axis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
		float cry = norm_axis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
		if (std::fabs(crx) > std::fabs(rx)) rx = crx;
		if (std::fabs(cry) > std::fabs(ry)) ry = cry;

		lt = std::max(lt, static_cast<float>(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f);
		rt = std::max(rt, static_cast<float>(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f);
	}

	if (lx != 0.0f || ly != 0.0f) {
		const float pan_speed = (this->cfg_top_h >= 1000) ? 1350.0f : 900.0f;
		PanMainViewportPixels(static_cast<int>(std::lround(lx * pan_speed * dt_sec)),
			static_cast<int>(std::lround(ly * pan_speed * dt_sec)));
	}

	if (rx != 0.0f || ry != 0.0f) {
		const float cursor_speed = (this->cfg_top_h >= 1000) ? 960.0f : 640.0f;
		int dx = static_cast<int>(std::lround(rx * cursor_speed * dt_sec));
		int dy = static_cast<int>(std::lround(ry * cursor_speed * dt_sec));
		Point cur_top = DualScreen::GetTopCursorPos();
		Point clamped = DualScreen::ClampPointToScreen(cur_top.x + dx, cur_top.y + dy, DualScreen::ScreenTarget::Top);
		DualScreen::SetTopCursorPos(clamped);
		_cursor.UpdateCursorPosition(clamped.x, clamped.y);
		HandleMouseEvents();
	}

	auto now = std::chrono::steady_clock::now();
	if ((lt > 0.5f || rt > 0.5f) && std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_zoom_time).count() >= 250) {
		if (Window *mw = GetMainWindow(); mw != nullptr) {
			if (rt > 0.5f) DoZoomInOutWindow(ZOOM_IN, mw);
			else if (lt > 0.5f) DoZoomInOutWindow(ZOOM_OUT, mw);
		}
		this->last_zoom_time = now;
	}
}

void VideoDriver_SDL_DS::InputLoop()
{
#ifdef __ANDROID__
	if (this->smoke_test_frames == 0) {
		bool has_sec = AndroidDS::HasSecondaryWindow();
		uint64_t sec_gen = AndroidDS::GetSecondaryWindowGeneration();
		if (has_sec != DualScreen::IsActive() && !this->force_single) {
			this->last_secondary_surface_gen = sec_gen;
			this->ReconfigureMode(has_sec);
		} else if (has_sec && DualScreen::IsActive() && sec_gen != this->last_secondary_surface_gen) {
			this->last_secondary_surface_gen = sec_gen;
			this->MakeDirty(0, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h);
		}
	}
#endif

	auto now = std::chrono::steady_clock::now();
	float dt_sec = std::clamp(std::chrono::duration<float>(now - this->last_input_time).count(), 0.0f, 0.05f);
	this->last_input_time = now;

	uint32_t mod = SDL_GetModState();
	const bool *key_states = SDL_GetKeyboardState(nullptr);
	bool old_ctrl = IsCtrlPressed();

	_input_pressed.Set(ControlInput::Ctrl, (mod & SDL_KMOD_CTRL) != 0);
	_input_pressed.Set(ControlInput::Shift, (mod & SDL_KMOD_SHIFT) != 0);
	this->fast_forward_key_pressed = key_states[SDL_SCANCODE_TAB] && (mod & SDL_KMOD_ALT) == 0;

	_dirkeys.Set(DirectionKey::Left, key_states[SDL_SCANCODE_LEFT]);
	_dirkeys.Set(DirectionKey::Up, key_states[SDL_SCANCODE_UP]);
	_dirkeys.Set(DirectionKey::Right, key_states[SDL_SCANCODE_RIGHT]);
	_dirkeys.Set(DirectionKey::Down, key_states[SDL_SCANCODE_DOWN]);

	if (old_ctrl != IsCtrlPressed()) HandleCtrlChanged();

	this->UpdateGamepadInput(dt_sec);

	if (this->smoke_test_frames > 0) {
		if (this->smoke_test_reconfigure) {
			int phase1_frame = std::max(3, this->smoke_test_frames / 3);
			int phase2_frame = std::max(6, (this->smoke_test_frames * 2) / 3);
			if (this->smoke_reconfig_phase == 0 && this->rendered_frames >= phase1_frame) {
				/* Exercise Trackpad open/close in Game/Editor before switching Dual -> Single. */
				if (DualScreen::IsActive() && (_game_mode == GameMode::Normal || _game_mode == GameMode::Editor)) {
					DualScreen::SetTrackpadMode(true);
					DualScreen::SyncTrackpadWindow();
					Rect pad_rect = DualScreen::GetTrackpadPadScreenRect();
					Rect drag_rect = DualScreen::GetTrackpadDragButtonScreenRect();
					if (FindWindowById(WindowClass::DsTrackpad, 0) == nullptr ||
							pad_rect.Width() <= 0 || pad_rect.Height() <= 0 ||
							drag_rect.Width() <= 0 || drag_rect.Height() <= 0) {
						this->smoke_reconfig_ok = false;
					}
					/* Closing DsTrackpad directly (e.g. Delete key) must automatically clear TrackpadMode. */
					CloseWindowById(WindowClass::DsTrackpad, 0);
					if (FindWindowById(WindowClass::DsTrackpad, 0) != nullptr || DualScreen::IsTrackpadMode()) {
						this->smoke_reconfig_ok = false;
					}
				}

				/* Place cursor on Bottom screen to verify Dual -> Single cursor clamping. */
				if (DualScreen::IsActive()) {
					_cursor.pos = { 400, this->cfg_top_h + 200 };
				}
				this->ReconfigureMode(false);
				if (DualScreen::IsActive() ||
						FindWindowById(WindowClass::BottomDesk, 0) != nullptr ||
						FindWindowById(WindowClass::DsActionBar, 0) != nullptr ||
						_cursor.pos.y >= _screen.height || _cursor.pos.x >= _screen.width) {
					this->smoke_reconfig_ok = false;
				}
				if (const Window *tb = FindWindowById(WindowClass::MainToolbar, 0); tb != nullptr && tb->top != 0) {
					this->smoke_reconfig_ok = false;
				}
				this->smoke_reconfig_phase = 1;
			} else if (this->smoke_reconfig_phase == 1 && this->rendered_frames >= phase2_frame) {
				this->ReconfigureMode(true);
				if (!DualScreen::IsActive() || FindWindowById(WindowClass::BottomDesk, 0) == nullptr) {
					this->smoke_reconfig_ok = false;
				}
				if (const Window *tb = FindWindowById(WindowClass::MainToolbar, 0); tb != nullptr && tb->top != DualScreen::GetBottomOriginY()) {
					this->smoke_reconfig_ok = false;
				}
				if (const Window *sg = FindWindowById(WindowClass::SelectGame, 0);
						sg != nullptr && DualScreen::GetScreenAtPoint(sg->left + sg->width / 2, sg->top + sg->height / 2) != DualScreen::ScreenTarget::Bottom) {
					this->smoke_reconfig_ok = false;
				}
				this->smoke_reconfig_phase = 2;
			}
		}

		this->smoke_has_desk = FindWindowById(WindowClass::BottomDesk, 0) != nullptr;
		this->smoke_has_ui = (FindWindowById(WindowClass::SelectGame, 0) != nullptr) || (FindWindowById(WindowClass::MainToolbar, 0) != nullptr);
	}
}

void VideoDriver_SDL_DS::OpenConnectedGamepads()
{
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (ids == nullptr) return;
	for (int i = 0; i < count; ++i) {
		if (SDL_Gamepad *gp = SDL_OpenGamepad(ids[i]); gp != nullptr) {
			this->gamepads.push_back(gp);
		}
	}
	SDL_free(ids);
}

void VideoDriver_SDL_DS::CloseAllGamepads()
{
	for (SDL_Gamepad *gp : this->gamepads) {
		SDL_CloseGamepad(gp);
	}
	this->gamepads.clear();
}

bool VideoDriver_SDL_DS::ChangeResolution(int w, int h)
{
	(void)w;
	(void)h;
	this->ReconfigureMode(DualScreen::IsActive());
	return true;
}

bool VideoDriver_SDL_DS::ToggleFullscreen(bool fullscreen)
{
	if (this->top_window == nullptr) return false;
	bool ok = SDL_SetWindowFullscreen(this->top_window, fullscreen);
	if (ok) _fullscreen = fullscreen;
	InvalidateWindowClassesData(WindowClass::GameOptions, 3);
	return ok;
}

bool VideoDriver_SDL_DS::AfterBlitterChange()
{
	this->AllocateBackingStore(this->cfg_top_w, this->cfg_top_h, this->cfg_bot_w, this->cfg_bot_h, DualScreen::IsActive());
	CopyPalette(this->local_palette, true);
	BlitterFactory::GetCurrentBlitter()->PostResize();
	MarkWholeScreenDirty();
	return true;
}

void VideoDriver_SDL_DS::ClaimMousePointer()
{
#ifdef __ANDROID__
	SDL_HideCursor();
#endif
}

void VideoDriver_SDL_DS::EditBoxGainedFocus()
{
	if (!this->edit_box_focused && this->top_window != nullptr) {
		SDL_StartTextInput(this->top_window);
		this->edit_box_focused = true;
	}
}

void VideoDriver_SDL_DS::EditBoxLostFocus()
{
	if (this->edit_box_focused && this->top_window != nullptr) {
		SDL_StopTextInput(this->top_window);
		this->edit_box_focused = false;
	}
}

std::vector<int> VideoDriver_SDL_DS::GetListOfMonitorRefreshRates()
{
	return { 60, 120 };
}

Dimension VideoDriver_SDL_DS::GetScreenSize() const
{
	return { static_cast<uint>(this->cfg_top_w), static_cast<uint>(this->cfg_top_h) };
}

void VideoDriver_SDL_DS::SetScreensaverInhibited(bool inhibited)
{
	if (inhibited) SDL_DisableScreenSaver();
	else SDL_EnableScreenSaver();
}

#endif /* WITH_SDL3 */
