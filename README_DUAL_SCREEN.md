# OpenTTD Dual Screen for AYN Thor

This port adapts OpenTTD to run natively in dual-screen mode on the **AYN Thor** Android handheld (and includes a two-window desktop simulator on Windows for local development and testing).

## Dual-Screen Architecture

- **Top Screen — Clean World Map (`1920x1080` physical, `1280x720` logical at crisp 1.5× scaling):**
  - Unobstructed main game viewport with full map pan, zoom, and direct touch/cursor interaction.
  - Hosted by `OpenTTDActivity` (extending SDL3 `SDLActivity`).
- **Bottom Screen — Command Desk (`1240x1080` physical, `826x720` logical at crisp 1.5× scaling):**
  - Hosts the docked main toolbar, status bar, Quick Action Bar, docked minimap (`SmallMap`) backdrop, and newly spawned build/management/menu windows.
  - Hosted on Android via a non-focusable immersive `SecondaryScreenPresentation` (`FLAG_NOT_FOCUSABLE | FLAG_KEEP_SCREEN_ON`) so bottom-screen touches never steal hardware gamepad or keyboard focus from the top screen.
  - Dynamically detects secondary display hotplug/sleep transitions and gracefully falls back to single-screen mode if the bottom screen is unavailable.

---

## Controls Reference

### Top Screen Touch
| Gesture | Action |
| --- | --- |
| 1-finger tap | Left-click (or Right-click when `R-Click` toggle is active) |
| 1-finger long press (≥ 500 ms) | Right-click |
| 1-finger drag (no build tool active) | Pan main map viewport |
| 1-finger drag (build tool active) | Drag-build (tracks, roads, landscaping, demolish) |
| 2-finger pinch | Zoom main map in / out |
| 2-finger drag | Pan main map viewport |

### Bottom Screen Touch & Trackpad Mode
- **Direct Touch (default):** Tap and drag toolbars, menus, minimap, and management windows directly. Enable the `R-Click` action bar button to turn the next tap into a one-shot right-click.
- **Trackpad Mode** (toggle via `Trackpad` button, gamepad `Back/Select`, or `F9` on desktop):
  - **Slide 1 finger on pad:** Relative cursor movement on the **Top** screen.
  - **1-finger tap on pad:** Left-click at the Top screen cursor (or Right-click if `R-Click` is toggled).
  - **2-finger tap on pad (Android):** Right-click at the Top screen cursor.
  - **Trackpad bar buttons:** `L-Click`, `R-Click`, `Drag Lock` (locks left mouse button down for drag-building while steering), `Zoom -`, `Zoom +`, `Exit Pad`.

### Bottom Quick Action Bar
| Button | Action |
| --- | --- |
| `Map` | Exits trackpad mode, clears bottom popups, and focuses the docked minimap |
| `Trackpad` | Toggles Bottom Trackpad Mode |
| `Swap Scr` | Moves the topmost movable popup window between Top and Bottom screens |
| `R-Click` | One-shot sticky right-click modifier for the next tap/click |
| `Clear UI` | Closes non-essential popup windows on the Bottom screen |
| `Keybd` | Toggles the on-screen virtual keyboard |

### Gamepad Controls (AYN Thor Built-In / USB / Bluetooth)
| Input | Action |
| --- | --- |
| Left Stick | Pan Top map viewport |
| Right Stick | Move Top screen cursor |
| D-Pad | Fine 12 px cursor nudge |
| `A` (South) | Left-click (hold to drag) |
| `B` (East) | Cancel active build tool, or Right-click when no tool is selected |
| `X` (West) | Pause / Resume (`WKC_PAUSE`) |
| `Y` (North) | Toggle Fast Forward |
| `LB` / `LT` | Zoom Out |
| `RB` / `RT` | Zoom In |
| `Back` / `Select` | Toggle Bottom Trackpad Mode |
| `L3` (Left Stick Click) | Focus / dock Minimap |
| `R3` (Right Stick Click) | Swap active popup window to the other screen |
| `Start` | Escape / close top modal (`WKC_ESC`) |

### Desktop Simulator Hotkeys
| Key | Action |
| --- | --- |
| `F9` | Toggle Bottom Trackpad Mode |
| `F10` | Swap active popup window between Top and Bottom windows |
| `F11` | Toggle Dual-Screen / Single-Screen mode live |
| `Alt+Enter` / `Alt+F` | Toggle Top window fullscreen |

---

## Local Windows Dual-Screen Simulator Build

No `vcpkg` installation is required — with `-DOPTION_FETCH_DEPS=ON`, CMake automatically fetches SDL3 (`release-3.4.20`), zlib, liblzma, and libpng.

### Prerequisites
- Visual Studio 2022 (Desktop development with C++ / MSVC x64 toolchain)
- CMake 3.22+ and Ninja
- Python 3.10+ (for fetching OpenGFX/OpenSFX base sets)
- Git

### Build, Fetch Assets, and Test
Open a **Developer PowerShell for VS 2022** (or initialize MSVC x64 via `Enter-VsDevShell -Arch amd64`) in the repo root (if using `cmd.exe` / x64 Native Tools Command Prompt, put the `cmake` configure flags on one line):

```powershell
# 1. Configure with Dual-Screen enabled and auto-fetched dependencies
cmake -S . -B build-win -GNinja -DCMAKE_BUILD_TYPE=Release -DOPTION_DUAL_SCREEN=ON -DOPTION_FETCH_DEPS=ON

# 2. Build OpenTTD and the Catch2 unit test suite
cmake --build build-win --target openttd openttd_test

# 3. Download and extract OpenGFX 8.0 and OpenSFX 1.0.3
python scripts/fetch_basesets.py --output build-win/baseset

# 4. Run the unit tests (including dual-screen geometry & routing tests)
.\build-win\openttd_test.exe --reporter console
```

### Launch the Dual-Screen Simulator
```powershell
.\build-win\openttd.exe -v sdl_ds
```

Optional driver parameters can be passed as comma-separated `key=value` pairs after `sdl_ds:`:
```powershell
# Example: larger simulator windows (75% scale)
.\build-win\openttd.exe -v sdl_ds:scale=0.75

# Example: custom logical resolutions or single-screen test
.\build-win\openttd.exe -v sdl_ds:top_w=1280,top_h=720,bot_w=826,bot_h=720,scale=0.60
.\build-win\openttd.exe -v sdl_ds:single

# Automated smoke test (renders N frames and logs SMOKE_TEST_OK before exiting)
.\build-win\openttd.exe -v sdl_ds:smoke_test_frames=30
```

---

## Building the Android APK via GitHub Actions

The workflow in [.github/workflows/android-build.yml](.github/workflows/android-build.yml) builds both the **Android `arm64-v8a` APKs** (`build-android`) and validates the **Windows dual-screen build & tests** (`build-windows`).

### 1. Push to GitHub
```bash
git remote add origin https://github.com/<your-username>/<your-repo>.git
git push -u origin main
```
Pushing to `main` (or opening a pull request against `main`) automatically triggers the workflow. You can also start a build manually anytime via **Actions → OpenTTD Dual Screen (Android & Windows) → Run workflow**.

### 2. Download the APK Artifact
Once the `build-android` job finishes, open the workflow run and download one of the artifacts:
- **`openttd-ds-android-release`** — Optimized `app-release.apk` (recommended for playing on AYN Thor).
- **`openttd-ds-android-debug`** — Debug `app-debug.apk` with native debug symbols enabled.

> **Note on APK signing:** By default, `app-release.apk` is signed with the debug keystore so it can be sideloaded directly onto your AYN Thor without extra setup. To sign release APKs with your own keystore in CI, add these GitHub Actions repository secrets:
> - `ANDROID_KEYSTORE_BASE64` (base64-encoded `.jks`/`.keystore` file)
> - `ANDROID_KEYSTORE_PASSWORD`
> - `ANDROID_KEY_ALIAS`
> - `ANDROID_KEY_PASSWORD`

---

## Installing & Running on AYN Thor

### Install via ADB (Recommended)
1. Enable **Developer Options** and **USB Debugging** on your AYN Thor.
2. Connect the device via USB and install the APK:
   ```bash
   adb install -r app-release.apk
   ```

### Install Directly on Device
1. Copy `app-release.apk` to your AYN Thor Downloads folder.
2. Tap the APK in your Android file manager and allow installation from that source if prompted.

### First Launch & Storage Location
- On first launch (and automatically after app updates), `OpenTTDActivity` extracts bundled OpenTTD language files, OpenGFX 8.0 graphics, OpenSFX 1.0.3 sound effects, and AI/Game Script compatibility files into app external storage:
  ```text
  /sdcard/Android/data/com.openttd.ds/files/
  ```
- User saves (`save/`), configuration (`openttd.cfg`), and downloaded NewGRFs (`content_download/`) are stored in the same directory and preserved across app updates.
- Make sure the AYN Thor bottom display is enabled before launching (or turn it on while running — `OpenTTDActivity` listens for display hotplug events and switches between single- and dual-screen mode automatically).

### Debugging on Device
```bash
adb logcat -s OpenTTDActivity SDL
```

---

## Local Android Build (Optional)

If you have JDK 17, Android SDK (Platform 35, Build-Tools 35.0.0, NDK `27.2.12479018`, CMake `3.22.1`), Ninja, and Gradle 8.10.2 installed locally on Linux/macOS:

```bash
# 1. Build host codegen tools (strgen, settingsgen)
cmake -S . -B build-tools -GNinja -DCMAKE_BUILD_TYPE=Release -DOPTION_TOOLS_ONLY=ON
cmake --build build-tools --target tools

# 2. Generate runtime data assets
cmake -S . -B build-data -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DOPTION_DEDICATED=ON -DOPTION_FETCH_DEPS=OFF \
  -DHOST_BINARY_DIR="$(pwd)/build-tools"
cmake --build build-data --target language_files baseset_files ai_compat_files gs_compat_files

mkdir -p android/app/src/main/assets/openttd_data/{lang,baseset,ai,game}
cp -r build-data/lang/. android/app/src/main/assets/openttd_data/lang/
cp -r build-data/baseset/. android/app/src/main/assets/openttd_data/baseset/
cp -r build-data/ai/. android/app/src/main/assets/openttd_data/ai/
cp -r build-data/game/. android/app/src/main/assets/openttd_data/game/

# 3. Fetch OpenGFX and OpenSFX into the bundled assets
python3 scripts/fetch_basesets.py --output android/app/src/main/assets/openttd_data/baseset

# 4. Build Debug and Release APKs
gradle -p android :app:assembleDebug :app:assembleRelease \
  -PhostBinaryDir="$(pwd)/build-tools"
```
