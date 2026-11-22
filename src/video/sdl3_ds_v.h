/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sdl3_ds_v.h SDL3 Dual-Screen video driver for AYN Thor (Android) and desktop dual-window simulator. */

#ifndef VIDEO_SDL3_DS_H
#define VIDEO_SDL3_DS_H

#include "video_driver.hpp"

#include <chrono>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Gamepad;
union SDL_Event;

/** Dual-screen SDL3 video driver supporting Android Presentation and dual-window desktop testing. */
class VideoDriver_SDL_DS : public VideoDriver {
public:
	VideoDriver_SDL_DS() : VideoDriver(false, false) {}

	std::optional<std::string_view> Start(const StringList &param) override;
	void Stop() override;
	void MakeDirty(int left, int top, int width, int height) override;
	void MainLoop() override;
	bool ChangeResolution(int w, int h) override;
	bool ToggleFullscreen(bool fullscreen) override;
	bool AfterBlitterChange() override;
	void ClaimMousePointer() override;
	void EditBoxGainedFocus() override;
	void EditBoxLostFocus() override;
	std::vector<int> GetListOfMonitorRefreshRates() override;
	std::string_view GetInfoString() const override { return this->driver_info; }
	void SetScreensaverInhibited(bool inhibited) override;
	std::string_view GetName() const override { return "sdl_ds"; }

protected:
	Dimension GetScreenSize() const override;
	void InputLoop() override;
	bool LockVideoBuffer() override;
	void UnlockVideoBuffer() override;
	void Paint() override;
	void CheckPaletteAnim() override;
	bool PollEvent() override;

private:
	struct FingerState {
		bool active = false;
		uint64_t id = 0;
		float norm_x = 0.0f;
		float norm_y = 0.0f;
		Point start_virt{};
		Point last_virt{};
		bool dragged = false;
		std::chrono::steady_clock::time_point down_time{};
	};

	SDL_Window *top_window = nullptr;
	SDL_Renderer *top_renderer = nullptr;
	SDL_Texture *top_texture = nullptr;

	SDL_Window *bot_window = nullptr;
	SDL_Renderer *bot_renderer = nullptr;
	SDL_Texture *bot_texture = nullptr;

	std::vector<SDL_Gamepad *> gamepads{};
	std::vector<uint32_t> framebuffer_32{};
	std::vector<uint8_t> framebuffer_8{};

	Palette local_palette{};
	Rect dirty_rect{};
	bool buffer_locked = false;
	bool edit_box_focused = false;
	std::string driver_info{};

	int cfg_top_w = 1280;
	int cfg_top_h = 720;
	int cfg_bot_w = 826;
	int cfg_bot_h = 720;
#ifndef __ANDROID__
	float desktop_scale = 0.60f;
#endif
	bool force_single = false;
	int smoke_test_frames = 0;
	bool smoke_test_reconfigure = false;
	int smoke_reconfig_phase = 0;
	bool smoke_reconfig_ok = true;
	bool smoke_has_desk = false;
	bool smoke_has_ui = false;
	int rendered_frames = 0;

	bool mouse_trackpad_dragging = false;
	bool mouse_trackpad_moved = false;
	Point mouse_trackpad_start{};
	Point mouse_trackpad_last{};
	bool mouse_left_converted_to_right = false;

	FingerState top_fingers[4]{};
	int top_active_fingers = 0;
	bool top_pinch_active = false;
	float top_prev_centroid_x = 0.0f;
	float top_prev_centroid_y = 0.0f;
	float top_prev_dist = 0.0f;

#ifdef __ANDROID__
	bool bot_trackpad_active = false;
	bool bot_trackpad_moved = false;
	int bot_trackpad_pointer_id = -1;
	Point bot_trackpad_start{};
	Point bot_trackpad_last{};
	uint64_t last_secondary_surface_gen = 0;
#endif

	std::chrono::steady_clock::time_point last_input_time{};
	std::chrono::steady_clock::time_point last_zoom_time{};

	bool AllocateBackingStore(int top_w, int top_h, int bot_w, int bot_h, bool dual_active);
	bool CreateDisplayOutputs(bool dual_active);
	void DestroySecondaryDesktopOutput();
	void DestroyAllOutputs();
	void ReconfigureMode(bool dual_active);
	void PresentTopRegion(const Rect &dirty);
	void PresentBottomRegion(const Rect &dirty);
	void Convert8bppDirtyTo32bpp(const Rect &dirty);
	void CheckSmokeTestCompletion();

	void HandleSDLEvent(const SDL_Event &ev);
	void HandleDesktopMouseEvent(const SDL_Event &ev);
	void HandleTopTouchEvent(const SDL_Event &ev);
	void DrainSecondaryTouches();
	void UpdateGamepadInput(float dt_sec);
	void HandleGamepadButton(uint8_t button, bool pressed);
	void OpenConnectedGamepads();
	void CloseAllGamepads();
};

/** Factory for the SDL3 Dual-Screen video driver. */
class FVideoDriver_SDL_DS : public DriverFactoryBase {
public:
	FVideoDriver_SDL_DS() : DriverFactoryBase(Driver::Type::Video, 12, "sdl_ds", "SDL3 Dual-Screen Video Driver") {}
	std::unique_ptr<Driver> CreateInstance() const override { return std::make_unique<VideoDriver_SDL_DS>(); }
};

#endif /* VIDEO_SDL3_DS_H */
