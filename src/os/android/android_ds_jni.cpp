/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file android_ds_jni.cpp Android dual-screen JNI bridge implementation. */

#include "../../stdafx.h"
#include "android_ds_jni.h"

#ifdef __ANDROID__
#include <android/native_window_jni.h>
#include <jni.h>
#endif

#include <deque>
#include <mutex>

#include "../../safeguards.h"

namespace AndroidDS {

namespace {

std::mutex _data_dir_mutex;
std::string _data_dir;

std::mutex _touch_mutex;
std::deque<SecondaryTouchEvent> _touch_queue;

#ifdef __ANDROID__
std::mutex _window_mutex;
ANativeWindow *_secondary_window = nullptr;
int _secondary_phys_w = 0;
int _secondary_phys_h = 0;
uint64_t _secondary_window_gen = 0;
#endif

} // namespace

std::string GetDataDir()
{
	std::lock_guard<std::mutex> lock(_data_dir_mutex);
	return _data_dir;
}

void SetDataDir(const std::string &dir)
{
	std::lock_guard<std::mutex> lock(_data_dir_mutex);
	_data_dir = dir;
}

void PushSecondaryTouch(const SecondaryTouchEvent &ev)
{
	std::lock_guard<std::mutex> lock(_touch_mutex);
	if (_touch_queue.size() >= 256) {
		_touch_queue.pop_front();
	}
	_touch_queue.push_back(ev);
}

bool PopSecondaryTouch(SecondaryTouchEvent &out_ev)
{
	std::lock_guard<std::mutex> lock(_touch_mutex);
	if (_touch_queue.empty()) return false;
	out_ev = _touch_queue.front();
	_touch_queue.pop_front();
	return true;
}

#ifdef __ANDROID__

bool HasSecondaryWindow()
{
	std::lock_guard<std::mutex> lock(_window_mutex);
	return _secondary_window != nullptr && _secondary_phys_w > 0 && _secondary_phys_h > 0;
}

uint64_t GetSecondaryWindowGeneration()
{
	std::lock_guard<std::mutex> lock(_window_mutex);
	return _secondary_window_gen;
}

void GetSecondaryPhysicalSize(int &width, int &height)
{
	std::lock_guard<std::mutex> lock(_window_mutex);
	width = _secondary_phys_w;
	height = _secondary_phys_h;
}

ANativeWindow *AcquireSecondaryWindow(int *out_phys_w, int *out_phys_h)
{
	std::lock_guard<std::mutex> lock(_window_mutex);
	if (_secondary_window == nullptr) return nullptr;
	ANativeWindow_acquire(_secondary_window);
	if (out_phys_w != nullptr) *out_phys_w = _secondary_phys_w;
	if (out_phys_h != nullptr) *out_phys_h = _secondary_phys_h;
	return _secondary_window;
}

void ReleaseSecondaryWindow(ANativeWindow *window)
{
	if (window != nullptr) {
		ANativeWindow_release(window);
	}
}

static void SetSecondaryWindow(ANativeWindow *window, int width, int height)
{
	std::lock_guard<std::mutex> lock(_window_mutex);
	if (_secondary_window != nullptr) {
		ANativeWindow_release(_secondary_window);
		_secondary_window = nullptr;
	}
	_secondary_window = window;
	_secondary_phys_w = width;
	_secondary_phys_h = height;
	++_secondary_window_gen;
}

#endif /* __ANDROID__ */

} // namespace AndroidDS

#ifdef __ANDROID__

extern "C" {

JNIEXPORT void JNICALL
Java_com_openttd_ds_OpenTTDNative_nativeSetDataDir(JNIEnv *env, jclass, jstring path)
{
	if (path == nullptr) return;
	const char *utf = env->GetStringUTFChars(path, nullptr);
	if (utf != nullptr) {
		AndroidDS::SetDataDir(utf);
		env->ReleaseStringUTFChars(path, utf);
	}
}

JNIEXPORT void JNICALL
Java_com_openttd_ds_OpenTTDNative_nativeOnSecondarySurfaceChanged(JNIEnv *env, jclass, jobject surface, jint width, jint height)
{
	if (surface == nullptr || width <= 0 || height <= 0) {
		AndroidDS::SetSecondaryWindow(nullptr, 0, 0);
		return;
	}
	ANativeWindow *nw = ANativeWindow_fromSurface(env, surface);
	AndroidDS::SetSecondaryWindow(nw, static_cast<int>(width), static_cast<int>(height));
}

JNIEXPORT void JNICALL
Java_com_openttd_ds_OpenTTDNative_nativeOnSecondarySurfaceDestroyed(JNIEnv *, jclass)
{
	AndroidDS::SetSecondaryWindow(nullptr, 0, 0);
}

JNIEXPORT void JNICALL
Java_com_openttd_ds_OpenTTDNative_nativeOnSecondaryTouch(JNIEnv *, jclass, jint actionMasked, jint pointerId, jfloat normX, jfloat normY, jint pointerCount)
{
	AndroidDS::SecondaryTouchEvent ev{};
	ev.action_masked = static_cast<int>(actionMasked);
	ev.pointer_id = static_cast<int>(pointerId);
	ev.norm_x = static_cast<float>(normX);
	ev.norm_y = static_cast<float>(normY);
	ev.pointer_count = static_cast<int>(pointerCount);
	AndroidDS::PushSecondaryTouch(ev);
}

} // extern "C"

#endif /* __ANDROID__ */
