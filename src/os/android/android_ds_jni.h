/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file android_ds_jni.h Android dual-screen JNI bridge for secondary Presentation surface and touch events. */

#ifndef OS_ANDROID_DS_JNI_H
#define OS_ANDROID_DS_JNI_H

#include <cstdint>
#include <string>

#ifdef __ANDROID__
struct ANativeWindow;
#endif

namespace AndroidDS {

struct SecondaryTouchEvent {
	int action_masked = 0; ///< MotionEvent.getActionMasked() value.
	int pointer_id = 0;    ///< Pointer ID.
	float norm_x = 0.0f;   ///< Normalized [0, 1] X coordinate on secondary surface.
	float norm_y = 0.0f;   ///< Normalized [0, 1] Y coordinate on secondary surface.
	int pointer_count = 1; ///< Total active pointer count.
};

std::string GetDataDir();
void SetDataDir(const std::string &dir);

void PushSecondaryTouch(const SecondaryTouchEvent &ev);
bool PopSecondaryTouch(SecondaryTouchEvent &out_ev);

#ifdef __ANDROID__
bool HasSecondaryWindow();
uint64_t GetSecondaryWindowGeneration();
void GetSecondaryPhysicalSize(int &width, int &height);
ANativeWindow *AcquireSecondaryWindow(int *out_phys_w = nullptr, int *out_phys_h = nullptr);
void ReleaseSecondaryWindow(ANativeWindow *window);
#endif

} // namespace AndroidDS

#endif /* OS_ANDROID_DS_JNI_H */
