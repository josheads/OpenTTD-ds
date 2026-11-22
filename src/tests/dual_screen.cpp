/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file dual_screen.cpp Unit tests for dual-screen geometry, window routing, clamping, and input state. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../dual_screen.h"
#include "../gfx_func.h"

#include <limits>

#include "../safeguards.h"

namespace {

struct DualScreenTestGuard {
	int saved_screen_w;
	int saved_screen_h;

	DualScreenTestGuard(int screen_w = 1280, int screen_h = 720)
		: saved_screen_w(_screen.width), saved_screen_h(_screen.height)
	{
		DualScreen::Reset();
		_screen.width = screen_w;
		_screen.height = screen_h;
	}

	~DualScreenTestGuard()
	{
		DualScreen::Reset();
		_screen.width = this->saved_screen_w;
		_screen.height = this->saved_screen_h;
	}
};

} // namespace

TEST_CASE("DualScreen - Single-screen vs dual-screen geometry and virtual dimensions", "[dual_screen]")
{
	DualScreenTestGuard guard(1024, 768);

	SECTION("Inactive single-screen defaults to _screen dimensions")
	{
		CHECK_FALSE(DualScreen::IsActive());
		CHECK(DualScreen::GetTopWidth() == 1024);
		CHECK(DualScreen::GetTopHeight() == 768);
		CHECK(DualScreen::GetBottomWidth() == 1024);
		CHECK(DualScreen::GetBottomHeight() == 0);
		CHECK(DualScreen::GetBottomOriginY() == 0);
		CHECK(DualScreen::GetVirtualWidth() == 1024);
		CHECK(DualScreen::GetVirtualHeight() == 768);

		Rect top = DualScreen::GetTopScreenRect();
		Rect bot = DualScreen::GetBottomScreenRect();
		CHECK(top.left == 0);
		CHECK(top.top == 0);
		CHECK(top.right == 1023);
		CHECK(top.bottom == 767);
		CHECK(bot.left == top.left);
		CHECK(bot.top == top.top);
		CHECK(bot.right == top.right);
		CHECK(bot.bottom == top.bottom);
	}

	SECTION("Active AYN Thor logical dual-screen geometry (1280x720 + 826x720)")
	{
		DualScreen::SetGeometry(
			DualScreen::DEFAULT_TOP_WIDTH,
			DualScreen::DEFAULT_TOP_HEIGHT,
			DualScreen::DEFAULT_BOTTOM_WIDTH,
			DualScreen::DEFAULT_BOTTOM_HEIGHT,
			true);

		CHECK(DualScreen::IsActive());
		CHECK(DualScreen::GetTopWidth() == 1280);
		CHECK(DualScreen::GetTopHeight() == 720);
		CHECK(DualScreen::GetBottomWidth() == 826);
		CHECK(DualScreen::GetBottomHeight() == 720);
		CHECK(DualScreen::GetBottomOriginY() == 720);
		CHECK(DualScreen::GetVirtualWidth() == 1280);
		CHECK(DualScreen::GetVirtualHeight() == 1440);

		auto cfg = DualScreen::GetConfig();
		CHECK(cfg.active);
		CHECK(cfg.top_w == 1280);
		CHECK(cfg.top_h == 720);
		CHECK(cfg.bot_w == 826);
		CHECK(cfg.bot_h == 720);
	}

	SECTION("Wider bottom display dictates virtual width")
	{
		DualScreen::SetGeometry(800, 600, 1024, 768, true);

		CHECK(DualScreen::IsActive());
		CHECK(DualScreen::GetVirtualWidth() == 1024);
		CHECK(DualScreen::GetVirtualHeight() == 1368);
		CHECK(DualScreen::GetBottomOriginY() == 600);
	}

	SECTION("Non-positive dimensions are clamped to at least 1px")
	{
		DualScreen::SetGeometry(0, -100, -50, 0, true);

		CHECK(DualScreen::IsActive());
		CHECK(DualScreen::GetTopWidth() == 1);
		CHECK(DualScreen::GetTopHeight() == 1);
		CHECK(DualScreen::GetBottomWidth() == 1);
		CHECK(DualScreen::GetBottomHeight() == 1);
		CHECK(DualScreen::GetVirtualWidth() == 1);
		CHECK(DualScreen::GetVirtualHeight() == 2);
	}
}

TEST_CASE("DualScreen - Top/bottom rect bounds and dead-zone exclusion", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 1440);
	DualScreen::SetGeometry(1280, 720, 826, 720, true);

	SECTION("Inclusive screen rectangles and screen-at-point hit testing")
	{
		Rect top = DualScreen::GetTopScreenRect();
		Rect bot = DualScreen::GetBottomScreenRect();

		CHECK(top.left == 0);
		CHECK(top.top == 0);
		CHECK(top.right == 1279);
		CHECK(top.bottom == 719);
		CHECK(top.Width() == 1280);
		CHECK(top.Height() == 720);

		CHECK(bot.left == 0);
		CHECK(bot.top == 720);
		CHECK(bot.right == 825);
		CHECK(bot.bottom == 1439);
		CHECK(bot.Width() == 826);
		CHECK(bot.Height() == 720);

		Rect top_via_target = DualScreen::GetScreenRect(DualScreen::ScreenTarget::Top);
		Rect bot_via_target = DualScreen::GetScreenRect(DualScreen::ScreenTarget::Bottom);
		CHECK(top_via_target.right == top.right);
		CHECK(top_via_target.bottom == top.bottom);
		CHECK(bot_via_target.top == bot.top);
		CHECK(bot_via_target.right == bot.right);

		CHECK(DualScreen::GetScreenAtPoint(0, 0) == DualScreen::ScreenTarget::Top);
		CHECK(DualScreen::GetScreenAtPoint(1279, 719) == DualScreen::ScreenTarget::Top);
		CHECK(DualScreen::GetScreenAtPoint(0, 720) == DualScreen::ScreenTarget::Bottom);
		CHECK(DualScreen::GetScreenAtPoint(825, 1439) == DualScreen::ScreenTarget::Bottom);
		CHECK(DualScreen::GetScreenAtPoint(1100, 900) == DualScreen::ScreenTarget::Bottom);
	}

	SECTION("ClampPointToScreen excludes bottom dead zone and out-of-range coordinates")
	{
		/* Dead zone on bottom display is x in [826, 1279], y in [720, 1439]. */
		CHECK(DualScreen::ClampPointToScreen(1100, 900, DualScreen::ScreenTarget::Bottom) == Point{ 825, 900 });
		CHECK(DualScreen::ClampPointToScreen(826, 720, DualScreen::ScreenTarget::Bottom) == Point{ 825, 720 });
		CHECK(DualScreen::ClampPointToScreen(-20, 100, DualScreen::ScreenTarget::Bottom) == Point{ 0, 720 });
		CHECK(DualScreen::ClampPointToScreen(2000, 2000, DualScreen::ScreenTarget::Bottom) == Point{ 825, 1439 });
		CHECK(DualScreen::ClampPointToScreen(400, 1000, DualScreen::ScreenTarget::Bottom) == Point{ 400, 1000 });

		/* Top display bounds [0..1279] x [0..719]. */
		CHECK(DualScreen::ClampPointToScreen(-10, -5, DualScreen::ScreenTarget::Top) == Point{ 0, 0 });
		CHECK(DualScreen::ClampPointToScreen(1500, 900, DualScreen::ScreenTarget::Top) == Point{ 1279, 719 });
		CHECK(DualScreen::ClampPointToScreen(640, 360, DualScreen::ScreenTarget::Top) == Point{ 640, 360 });
	}

	SECTION("ClampWindowToScreen keeps windows fully inside target screen and out of dead zone")
	{
		/* Window pushed into the bottom dead zone (x=900, width=200) must end at right=825 -> left=626. */
		Point p_dead = DualScreen::ClampWindowToScreen(900, 800, 200, 150, DualScreen::ScreenTarget::Bottom);
		CHECK(p_dead == Point{ 626, 800 });
		CHECK(p_dead.x + 200 - 1 == 825);

		/* Window pushed past bottom edge (y=1400, height=150) must end at bottom=1439 -> top=1290. */
		Point p_bot = DualScreen::ClampWindowToScreen(100, 1400, 200, 150, DualScreen::ScreenTarget::Bottom);
		CHECK(p_bot == Point{ 100, 1290 });
		CHECK(p_bot.y + 150 - 1 == 1439);

		/* Window above bottom origin or negative left clamps to bottom origin (0, 720). */
		CHECK(DualScreen::ClampWindowToScreen(-50, 200, 200, 150, DualScreen::ScreenTarget::Bottom) == Point{ 0, 720 });

		/* Exact-fit and oversized windows clamp to screen top-left. */
		CHECK(DualScreen::ClampWindowToScreen(50, 800, 826, 720, DualScreen::ScreenTarget::Bottom) == Point{ 0, 720 });
		CHECK(DualScreen::ClampWindowToScreen(50, 800, 1100, 900, DualScreen::ScreenTarget::Bottom) == Point{ 0, 720 });

		/* Valid interior window stays untouched. */
		CHECK(DualScreen::ClampWindowToScreen(120, 850, 300, 200, DualScreen::ScreenTarget::Bottom) == Point{ 120, 850 });

		/* Degenerate zero/negative window sizes still clamp within inclusive bounds. */
		Point p_zero = DualScreen::ClampWindowToScreen(900, 1500, 0, 0, DualScreen::ScreenTarget::Bottom);
		CHECK(p_zero.x <= 825);
		CHECK(p_zero.y <= 1439);

		/* Top display clamping. */
		CHECK(DualScreen::ClampWindowToScreen(1200, 650, 300, 200, DualScreen::ScreenTarget::Top) == Point{ 980, 520 });
		CHECK(DualScreen::ClampWindowToScreen(-10, -20, 300, 200, DualScreen::ScreenTarget::Top) == Point{ 0, 0 });
	}

	SECTION("GetWorkArea respects action bar height and never inverts")
	{
		Rect top_work = DualScreen::GetWorkArea(DualScreen::ScreenTarget::Top);
		CHECK(top_work.left == 0);
		CHECK(top_work.top == 0);
		CHECK(top_work.right == 1279);
		CHECK(top_work.bottom == 719);

		DualScreen::SetActionBarHeight(30);
		CHECK(DualScreen::GetActionBarHeight() == 30);
		Rect bot_work = DualScreen::GetWorkArea(DualScreen::ScreenTarget::Bottom);
		CHECK(bot_work.left == 0);
		CHECK(bot_work.top == 720);
		CHECK(bot_work.right == 825);
		CHECK(bot_work.bottom == 1409);

		/* Negative height clamps to 0. */
		DualScreen::SetActionBarHeight(-15);
		CHECK(DualScreen::GetActionBarHeight() == 0);

		/* Oversized action bar height never produces bottom < top. */
		DualScreen::SetActionBarHeight(5000);
		Rect collapsed = DualScreen::GetWorkArea(DualScreen::ScreenTarget::Bottom);
		CHECK(collapsed.bottom >= collapsed.top);
	}
}

TEST_CASE("DualScreen - Window class default screen routing", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 720);

	const WindowClass bottom_classes[] = {
		WindowClass::MainToolbar,
		WindowClass::Statusbar,
		WindowClass::SmallMap,
		WindowClass::SelectGame,
		WindowClass::BottomDesk,
		WindowClass::DsActionBar,
		WindowClass::DsTrackpad,
		WindowClass::BuildToolbar,
		WindowClass::DropdownMenu,
		WindowClass::News,
		WindowClass::ErrorMessage,
		WindowClass::NetworkChat,
		WindowClass::GameOptions,
		WindowClass::VehicleView,
		WindowClass::TownView,
		WindowClass::ToolTips,
	};

	SECTION("Dual-screen active routes MainWindow to Top and UI windows to Bottom")
	{
		DualScreen::SetGeometry(1280, 720, 826, 720, true);

		CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::MainWindow) == DualScreen::ScreenTarget::Top);
		for (WindowClass wc : bottom_classes) {
			CAPTURE(static_cast<int>(wc));
			CHECK(DualScreen::GetDefaultScreenForWindow(wc) == DualScreen::ScreenTarget::Bottom);
		}
	}

	SECTION("Single-screen inactive routes all windows to Top")
	{
		CHECK_FALSE(DualScreen::IsActive());
		CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::MainWindow) == DualScreen::ScreenTarget::Top);
		for (WindowClass wc : bottom_classes) {
			CAPTURE(static_cast<int>(wc));
			CHECK(DualScreen::GetDefaultScreenForWindow(wc) == DualScreen::ScreenTarget::Top);
		}
	}
}

TEST_CASE("DualScreen - Normalized touch coordinate mapping", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 720);

	SECTION("Dual-screen active maps Top and Bottom normalized coordinates accurately")
	{
		DualScreen::SetGeometry(1280, 720, 826, 720, true);

		/* Top display corners and center. */
		CHECK(DualScreen::MapTopNormalizedToVirtual(0.0f, 0.0f) == Point{ 0, 0 });
		CHECK(DualScreen::MapTopNormalizedToVirtual(1.0f, 1.0f) == Point{ 1279, 719 });
		Point top_mid = DualScreen::MapTopNormalizedToVirtual(0.5f, 0.5f);
		CHECK(top_mid.x >= 639);
		CHECK(top_mid.x <= 640);
		CHECK(top_mid.y >= 359);
		CHECK(top_mid.y <= 360);

		/* Bottom display corners and center (y offset by top_h = 720). */
		CHECK(DualScreen::MapBottomNormalizedToVirtual(0.0f, 0.0f) == Point{ 0, 720 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(1.0f, 1.0f) == Point{ 825, 1439 });
		Point bot_mid = DualScreen::MapBottomNormalizedToVirtual(0.5f, 0.5f);
		CHECK(bot_mid.x >= 412);
		CHECK(bot_mid.x <= 413);
		CHECK(bot_mid.y >= 1079);
		CHECK(bot_mid.y <= 1080);

		/* Out-of-range inputs clamp cleanly without entering dead zone. */
		CHECK(DualScreen::MapTopNormalizedToVirtual(-0.5f, -1.0f) == Point{ 0, 0 });
		CHECK(DualScreen::MapTopNormalizedToVirtual(2.0f, 1.5f) == Point{ 1279, 719 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(-0.5f, -1.0f) == Point{ 0, 720 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(2.0f, 1.5f) == Point{ 825, 1439 });

		/* Non-finite inputs do not produce out-of-bounds coordinates. */
		float nan = std::numeric_limits<float>::quiet_NaN();
		float inf = std::numeric_limits<float>::infinity();
		CHECK(DualScreen::MapTopNormalizedToVirtual(nan, inf) == Point{ 0, 0 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(nan, -inf) == Point{ 0, 720 });
	}

	SECTION("Inactive single-screen falls back to Top screen bounds for bottom mapping")
	{
		_screen.width = 1000;
		_screen.height = 600;
		CHECK_FALSE(DualScreen::IsActive());

		CHECK(DualScreen::MapTopNormalizedToVirtual(1.0f, 1.0f) == Point{ 999, 599 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(0.0f, 0.0f) == Point{ 0, 0 });
		CHECK(DualScreen::MapBottomNormalizedToVirtual(1.0f, 1.0f) == Point{ 999, 599 });
	}
}

TEST_CASE("DualScreen - Trackpad and right-click mode state toggles and consumption", "[dual_screen]")
{
	DualScreenTestGuard guard;

	SECTION("Trackpad mode set, toggle, and reset")
	{
		CHECK_FALSE(DualScreen::IsTrackpadMode());

		DualScreen::SetTrackpadMode(true);
		CHECK(DualScreen::IsTrackpadMode());

		DualScreen::ToggleTrackpadMode();
		CHECK_FALSE(DualScreen::IsTrackpadMode());

		DualScreen::ToggleTrackpadMode();
		CHECK(DualScreen::IsTrackpadMode());

		DualScreen::Reset();
		CHECK_FALSE(DualScreen::IsTrackpadMode());
	}

	SECTION("Right-click mode set, toggle, one-shot consumption, and reset")
	{
		CHECK_FALSE(DualScreen::IsRightClickMode());
		CHECK_FALSE(DualScreen::ConsumeRightClickMode());

		DualScreen::SetRightClickMode(true);
		CHECK(DualScreen::IsRightClickMode());
		CHECK(DualScreen::ConsumeRightClickMode());
		CHECK_FALSE(DualScreen::IsRightClickMode());
		CHECK_FALSE(DualScreen::ConsumeRightClickMode());

		DualScreen::ToggleRightClickMode();
		CHECK(DualScreen::IsRightClickMode());
		DualScreen::ToggleRightClickMode();
		CHECK_FALSE(DualScreen::IsRightClickMode());

		DualScreen::ToggleRightClickMode();
		CHECK(DualScreen::IsRightClickMode());
		DualScreen::Reset();
		CHECK_FALSE(DualScreen::IsRightClickMode());
		CHECK_FALSE(DualScreen::ConsumeRightClickMode());
	}
}

TEST_CASE("DualScreen - Dynamic dual-screen to single-screen to dual-screen reconfiguration", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 720);

	/* Phase 1: Initial dual-screen (logical 1280x720 + 826x720). */
	DualScreen::SetGeometry(1280, 720, 826, 720, true);
	_screen.width = DualScreen::GetVirtualWidth();
	_screen.height = DualScreen::GetVirtualHeight();

	REQUIRE(DualScreen::IsActive());
	CHECK(DualScreen::GetVirtualWidth() == 1280);
	CHECK(DualScreen::GetVirtualHeight() == 1440);
	CHECK(DualScreen::GetBottomScreenRect().top == 720);
	CHECK(DualScreen::GetBottomScreenRect().right == 825);
	CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::MainToolbar) == DualScreen::ScreenTarget::Bottom);
	CHECK(DualScreen::GetScreenAtPoint(400, 900) == DualScreen::ScreenTarget::Bottom);

	/* Phase 2: Reconfigure down to single-screen (e.g., secondary display detached or F11 toggle). */
	DualScreen::SetGeometry(1280, 720, 826, 720, false);
	_screen.width = 1280;
	_screen.height = 720;

	REQUIRE_FALSE(DualScreen::IsActive());
	CHECK(DualScreen::GetVirtualWidth() == 1280);
	CHECK(DualScreen::GetVirtualHeight() == 720);
	CHECK(DualScreen::GetBottomHeight() == 0);
	CHECK(DualScreen::GetBottomOriginY() == 0);
	CHECK(DualScreen::GetBottomScreenRect().top == 0);
	CHECK(DualScreen::GetBottomScreenRect().right == 1279);
	CHECK(DualScreen::GetBottomScreenRect().bottom == 719);
	CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::MainToolbar) == DualScreen::ScreenTarget::Top);
	CHECK(DualScreen::GetScreenAtPoint(400, 900) == DualScreen::ScreenTarget::Top);

	/* Phase 3: Reconfigure back to dual-screen at native AYN Thor 1920x1080 + 1240x1080. */
	DualScreen::SetGeometry(1920, 1080, 1240, 1080, true);
	_screen.width = DualScreen::GetVirtualWidth();
	_screen.height = DualScreen::GetVirtualHeight();

	REQUIRE(DualScreen::IsActive());
	CHECK(DualScreen::GetTopWidth() == 1920);
	CHECK(DualScreen::GetTopHeight() == 1080);
	CHECK(DualScreen::GetBottomWidth() == 1240);
	CHECK(DualScreen::GetBottomHeight() == 1080);
	CHECK(DualScreen::GetBottomOriginY() == 1080);
	CHECK(DualScreen::GetVirtualWidth() == 1920);
	CHECK(DualScreen::GetVirtualHeight() == 2160);
	CHECK(DualScreen::GetBottomScreenRect().top == 1080);
	CHECK(DualScreen::GetBottomScreenRect().right == 1239);
	CHECK(DualScreen::GetBottomScreenRect().bottom == 2159);
	CHECK(DualScreen::ClampPointToScreen(1800, 1500, DualScreen::ScreenTarget::Bottom) == Point{ 1239, 1500 });
	CHECK(DualScreen::MapBottomNormalizedToVirtual(1.0f, 1.0f) == Point{ 1239, 2159 });
	CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::MainWindow) == DualScreen::ScreenTarget::Top);
	CHECK(DualScreen::GetDefaultScreenForWindow(WindowClass::SmallMap) == DualScreen::ScreenTarget::Bottom);
}

TEST_CASE("DualScreen - Top cursor tracking and GUI scale calculation", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 720);

	SECTION("Top cursor position initializes centered, clamps to Top screen, and adapts on geometry change")
	{
		DualScreen::SetGeometry(1280, 720, 826, 720, true);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 640, 360 });

		DualScreen::SetTopCursorPos({ 500, 300 });
		CHECK(DualScreen::GetTopCursorPos() == Point{ 500, 300 });

		/* Out-of-bounds or Bottom-screen coordinates clamp cleanly inside Top screen. */
		DualScreen::SetTopCursorPos({ 2000, 1100 });
		CHECK(DualScreen::GetTopCursorPos() == Point{ 1279, 719 });
		DualScreen::SetTopCursorPos({ -50, -25 });
		CHECK(DualScreen::GetTopCursorPos() == Point{ 0, 0 });

		/* Shrinking Top display clamps the preserved cursor position immediately. */
		DualScreen::SetTopCursorPos({ 1200, 700 });
		DualScreen::SetGeometry(800, 600, 640, 480, true);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 799, 599 });

		DualScreen::Reset();
		CHECK(DualScreen::GetTopCursorPos() == Point{ 640, 360 });
	}

	SECTION("GUI automatic and maximum scales use Bottom Command Desk dimensions when dual-screen is active")
	{
		/* Single-screen 1280x720: xs=2.0, ys=1.5 -> auto 150%, max 200%. */
		CHECK_FALSE(DualScreen::IsActive());
		CHECK(GetAutomaticScale() == 150);
		CHECK(GetMaximumScale() == 200);

		/* Dual-screen logical 1280x720 + 826x720 (_screen=1280x1440): must use Bottom 826x720 -> auto 125%, max 150%. */
		DualScreen::SetGeometry(1280, 720, 826, 720, true);
		_screen.width = DualScreen::GetVirtualWidth();
		_screen.height = DualScreen::GetVirtualHeight();
		CHECK(GetAutomaticScale() == 125);
		CHECK(GetMaximumScale() == 150);

		/* Dual-screen native AYN Thor 1920x1080 + 1240x1080: Bottom 1240x1080 -> auto 175%, max 250%. */
		DualScreen::SetGeometry(1920, 1080, 1240, 1080, true);
		_screen.width = DualScreen::GetVirtualWidth();
		_screen.height = DualScreen::GetVirtualHeight();
		CHECK(GetAutomaticScale() == 175);
		CHECK(GetMaximumScale() == 250);
	}
}

TEST_CASE("DualScreen - Multi-touch bottom trackpad pointer-ID handoff and gestures", "[dual_screen]")
{
	DualScreenTestGuard guard(1280, 1440);
	DualScreen::SetGeometry(1280, 720, 826, 720, true);
	DualScreen::SetTrackpadMode(true);
	DualScreen::SetTopCursorPos({ 500, 300 });

	constexpr int ACTION_DOWN = 0;
	constexpr int ACTION_UP = 1;
	constexpr int ACTION_MOVE = 2;
	constexpr int ACTION_CANCEL = 3;
	constexpr int ACTION_POINTER_DOWN = 5;
	constexpr int ACTION_POINTER_UP = 6;

	SECTION("1-finger slide moves Top cursor at 1.5x gain and suppresses tap on lift")
	{
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 100, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.active);
		CHECK_FALSE(st.moved);
		CHECK(st.pointer_id == 0);

		/* Move 20px right, 10px down -> Top cursor moves (+30, +15) -> (530, 315). */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 120, 910 }, true) == DualScreen::TrackpadTouchResult::CursorMoved);
		CHECK(st.moved);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 530, 315 });

		/* Lifting after drag must not fire a tap click. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 0, 1, { 120, 910 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK_FALSE(st.active);
		CHECK(st.pointer_id == -1);
	}

	SECTION("1-finger tap within 8px jitter threshold fires TapClick")
	{
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 200, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 204, 904 }, true) == DualScreen::TrackpadTouchResult::CursorMoved);
		CHECK_FALSE(st.moved);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 0, 1, { 204, 904 }, true) == DualScreen::TrackpadTouchResult::TapClick);
		CHECK_FALSE(st.active);
		CHECK(st.pointer_id == -1);
	}

	SECTION("Pointer-ID handoff when primary finger lifts while sliding causes zero cursor jump")
	{
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 100, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 120, 900 }, true) == DualScreen::TrackpadTouchResult::CursorMoved);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 530, 300 });

		/* Second finger touches down far across the pad at (600, 1200). */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 1, 2, { 600, 1200 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == 0);

		/* Primary finger (id=0) lifts -> pointer_id clears to -1, no right-click because already moved. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 0, 2, { 120, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.active);
		CHECK(st.pointer_id == -1);

		/* Next ACTION_MOVE comes from remaining finger (id=1) at (602, 1204): MUST re-anchor with ZERO jump. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 1, 1, { 602, 1204 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == 1);
		CHECK(st.last == Point{ 602, 1204 });
		CHECK(DualScreen::GetTopCursorPos() == Point{ 530, 300 });

		/* Subsequent slide of finger 1 by (+10, -20) moves Top cursor by (+15, -30) -> (545, 270). */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 1, 1, { 612, 1184 }, true) == DualScreen::TrackpadTouchResult::CursorMoved);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 545, 270 });

		/* Chained handoff: finger 2 touches, finger 1 lifts, finger 2 takes over with zero jump. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 2, 2, { 50, 800 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 1, 2, { 612, 1184 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == -1);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 2, 1, { 52, 802 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == 2);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 545, 270 });

		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 2, 1, { 52, 802 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK_FALSE(st.active);
		CHECK(st.pointer_id == -1);
	}

	SECTION("2-finger tap right-click works regardless of which finger lifts first, with zero jump on remaining finger")
	{
		/* Case A: primary finger 0 lifts first. */
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 150, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 1, 2, { 450, 1100 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 0, 2, { 150, 900 }, true) == DualScreen::TrackpadTouchResult::TwoFingerClick);
		CHECK(st.pointer_id == -1);
		CHECK(st.moved);

		/* Remaining finger 1 moves before lifting -> zero jump and no extra TapClick. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 1, 1, { 452, 1102 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 500, 300 });
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 1, 1, { 452, 1102 }, true) == DualScreen::TrackpadTouchResult::None);

		/* Case B: secondary finger 1 lifts first; primary finger 0 stays anchored. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 150, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 1, 2, { 450, 1100 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 1, 2, { 450, 1100 }, true) == DualScreen::TrackpadTouchResult::TwoFingerClick);
		CHECK(st.pointer_id == 0);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 160, 900 }, true) == DualScreen::TrackpadTouchResult::CursorMoved);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 515, 300 });
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 0, 1, { 160, 900 }, true) == DualScreen::TrackpadTouchResult::None);
	}

	SECTION("3-finger touch never triggers spurious 2-finger right-click or 1-finger tap")
	{
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 100, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 1, 2, { 104, 902 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_DOWN, 2, 3, { 300, 1000 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.moved);

		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 0, 3, { 100, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == -1);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 1, 2, { 104, 902 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 500, 300 });
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_POINTER_UP, 2, 2, { 300, 1000 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 1, 1, { 104, 902 }, true) == DualScreen::TrackpadTouchResult::None);
	}

	SECTION("Dropped ACTION_POINTER_UP, ACTION_CANCEL, off-pad touch, and mid-gesture Trackpad disable")
	{
		/* Dropped ACTION_POINTER_UP: sudden new pointer_id on ACTION_MOVE still re-anchors cleanly. */
		DualScreen::TrackpadTouchState st{};
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 100, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 5, 1, { 700, 1300 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.pointer_id == 5);
		CHECK(DualScreen::GetTopCursorPos() == Point{ 500, 300 });

		/* ACTION_CANCEL cancels without TapClick. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_CANCEL, 5, 1, { 700, 1300 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK_FALSE(st.active);
		CHECK(st.pointer_id == -1);

		/* Touch outside pad passes through to direct bottom touch. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 100, 1400 }, false) == DualScreen::TrackpadTouchResult::Passthrough);
		CHECK_FALSE(st.active);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 105, 1400 }, false) == DualScreen::TrackpadTouchResult::Passthrough);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_UP, 0, 1, { 105, 1400 }, false) == DualScreen::TrackpadTouchResult::Passthrough);

		/* Disabling TrackpadMode mid-touch clears state and switches to Passthrough. */
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_DOWN, 0, 1, { 200, 900 }, true) == DualScreen::TrackpadTouchResult::None);
		CHECK(st.active);
		DualScreen::SetTrackpadMode(false);
		CHECK(DualScreen::ProcessTrackpadTouch(st, ACTION_MOVE, 0, 1, { 250, 950 }, true) == DualScreen::TrackpadTouchResult::Passthrough);
		CHECK_FALSE(st.active);
		CHECK(st.pointer_id == -1);
	}
}
