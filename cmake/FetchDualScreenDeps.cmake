# Fetch missing build dependencies (SDL3, zlib, liblzma, libpng, freetype) when not provided by system/vcpkg.

if(NOT OPTION_FETCH_DEPS OR OPTION_TOOLS_ONLY)
    return()
endif()

include(FetchContent)

if(OPENTTD_DEPS_CACHE_DIR)
    file(TO_CMAKE_PATH "${OPENTTD_DEPS_CACHE_DIR}" _openttd_deps_cache_norm)
    foreach(_dep_pair
        "SDL3:SDL3"
        "ZLIB:zlib"
        "XZ:xz"
        "LIBPNG:libpng"
        "FREETYPE:freetype"
    )
        string(REPLACE ":" ";" _dep_parts "${_dep_pair}")
        list(GET _dep_parts 0 _dep_uc)
        list(GET _dep_parts 1 _dep_dir)
        if(EXISTS "${_openttd_deps_cache_norm}/${_dep_dir}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_${_dep_uc} "${_openttd_deps_cache_norm}/${_dep_dir}" CACHE PATH "" FORCE)
        endif()
    endforeach()
endif()

# Isolate third-party FetchContent subprojects from OpenTTD's strict directory-level compiler flags.
get_directory_property(_openttd_saved_compile_options COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

# Save caller's BUILD_SHARED_LIBS and PIC state so third-party subprojects can be controlled cleanly.
set(_openttd_saved_build_shared "${BUILD_SHARED_LIBS}")
if(DEFINED BUILD_SHARED_LIBS)
    set(_openttd_had_build_shared TRUE)
else()
    set(_openttd_had_build_shared FALSE)
endif()

set(_openttd_saved_pic "${CMAKE_POSITION_INDEPENDENT_CODE}")
if(DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set(_openttd_had_pic TRUE)
else()
    set(_openttd_had_pic FALSE)
endif()
if(ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# 1. SDL3 (used on Android and for the local dual-screen simulator on desktop)
if(NOT OPTION_DEDICATED AND (ANDROID OR OPTION_DUAL_SCREEN) AND NOT SDL3_FOUND)
    message(STATUS "Fetching SDL3 (release-3.4.20) via FetchContent...")
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
        set(_openttd_sdl_shared ON)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(_openttd_sdl_shared OFF)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS ${_openttd_sdl_shared})

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.20
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    if(NOT TARGET SDL3::SDL3)
        if(TARGET SDL3-shared)
            add_library(SDL3::SDL3 ALIAS SDL3-shared)
        elseif(TARGET SDL3-static)
            add_library(SDL3::SDL3 ALIAS SDL3-static)
        endif()
    endif()
    set(SDL3_FOUND TRUE)
endif()

# Ensure all compression / image / font static libs build statically.
set(BUILD_SHARED_LIBS OFF)

# Prevent third-party FetchContent subprojects (zlib, libpng, freetype) from registering
# top-level install/export rules (libpng's install(TARGETS ${PNG_BIN_TARGETS}) fails when
# PNG_SHARED=OFF and PNG_TOOLS=OFF if SKIP_INSTALL_ALL is not set).
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

# 2. ZLIB (Android NDK provides zlib; desktop builds without vcpkg fetch it)
if(NOT ANDROID AND NOT ZLIB_FOUND)
    message(STATUS "Fetching zlib (v1.3.1) via FetchContent...")
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(zlib)

    # Place zlib.h alongside generated zconf.h in zlib_BINARY_DIR so find_package(ZLIB) inside libpng succeeds.
    configure_file("${zlib_SOURCE_DIR}/zlib.h" "${zlib_BINARY_DIR}/zlib.h" COPYONLY)

    # Keep ZLIB_LIBRARY non-cached so subsequent CMake reconfigures do not mistake
    # the in-tree target name for a pre-installed library before FetchContent runs.
    unset(ZLIB_LIBRARY CACHE)
    unset(ZLIB_LIBRARIES CACHE)
    set(ZLIB_ROOT "${zlib_BINARY_DIR}")
    set(ZLIB_INCLUDE_DIR "${zlib_BINARY_DIR}")
    set(ZLIB_INCLUDE_DIRS "${zlib_BINARY_DIR};${zlib_SOURCE_DIR}")
    set(ZLIB_LIBRARY zlibstatic)
    set(ZLIB_LIBRARIES zlibstatic)
    set(ZLIB_FOUND TRUE)

    if(TARGET zlibstatic)
        target_include_directories(zlibstatic INTERFACE
            "$<BUILD_INTERFACE:${zlib_BINARY_DIR}>"
            "$<BUILD_INTERFACE:${zlib_SOURCE_DIR}>"
        )
        if(NOT TARGET ZLIB::ZLIB)
            add_library(ZLIB::ZLIB ALIAS zlibstatic)
        endif()
    endif()
endif()

# 3. LibLZMA (xz)
if(NOT LIBLZMA_FOUND AND NOT LibLZMA_FOUND)
    message(STATUS "Fetching xz/liblzma (v5.6.3) via FetchContent...")
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_SCRIPTS OFF CACHE BOOL "" FORCE)
    set(XZ_DOC OFF CACHE BOOL "" FORCE)
    set(XZ_NLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_NLS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        xz
        GIT_REPOSITORY https://github.com/tukaani-project/xz.git
        GIT_TAG        v5.6.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(xz)

    if(TARGET liblzma)
        target_include_directories(liblzma INTERFACE "$<BUILD_INTERFACE:${xz_SOURCE_DIR}/src/liblzma/api>")
    endif()
    if(NOT TARGET LibLZMA::LibLZMA AND TARGET liblzma)
        add_library(LibLZMA::LibLZMA ALIAS liblzma)
    endif()
    set(LIBLZMA_FOUND TRUE)
    set(LibLZMA_FOUND TRUE)
endif()

# 4. libpng
if(NOT PNG_FOUND)
    message(STATUS "Fetching libpng (v1.6.44) via FetchContent...")
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
    set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
    set(PNG_HARDWARE_OPTIMIZATIONS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        libpng
        GIT_REPOSITORY https://github.com/pnggroup/libpng.git
        GIT_TAG        v1.6.44
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libpng)

    if(NOT TARGET PNG::PNG)
        if(TARGET png_static)
            add_library(PNG::PNG ALIAS png_static)
        elseif(TARGET png)
            add_library(PNG::PNG ALIAS png)
        endif()
    endif()
    unset(PNG_LIBRARY CACHE)
    unset(PNG_LIBRARIES CACHE)
    unset(PNG_PNG_INCLUDE_DIR CACHE)
    set(PNG_LIBRARY PNG::PNG)
    set(PNG_LIBRARIES PNG::PNG)
    set(PNG_FOUND TRUE)
endif()

# 5. FreeType (enabled on Android for scalable UI text rendering without Fontconfig)
if(ANDROID AND NOT OPTION_DEDICATED AND NOT Freetype_FOUND AND NOT FREETYPE_FOUND)
    message(STATUS "Fetching FreeType (VER-2-13-3) via FetchContent...")
    set(DISABLE_FORCE_DEBUG_POSTFIX ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)

    # FreeType's CMakeLists.txt checks if(PNG_FOUND) / if(ZLIB_FOUND) even when FT_DISABLE_* is ON.
    set(_openttd_ft_saved_zlib_found "${ZLIB_FOUND}")
    set(_openttd_ft_saved_png_found "${PNG_FOUND}")
    set(ZLIB_FOUND FALSE)
    set(PNG_FOUND FALSE)
    set(BZIP2_FOUND FALSE)
    set(HARFBUZZ_FOUND FALSE)
    set(HarfBuzz_FOUND FALSE)
    set(BROTLIDEC_FOUND FALSE)

    FetchContent_Declare(
        freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(freetype)

    set(ZLIB_FOUND "${_openttd_ft_saved_zlib_found}")
    set(PNG_FOUND "${_openttd_ft_saved_png_found}")

    if(NOT TARGET Freetype::Freetype AND TARGET freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(Freetype_FOUND TRUE)
    set(FREETYPE_FOUND TRUE)
endif()

if(_openttd_had_build_shared)
    set(BUILD_SHARED_LIBS "${_openttd_saved_build_shared}")
else()
    unset(BUILD_SHARED_LIBS)
endif()

if(_openttd_had_pic)
    set(CMAKE_POSITION_INDEPENDENT_CODE "${_openttd_saved_pic}")
else()
    unset(CMAKE_POSITION_INDEPENDENT_CODE)
endif()

set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_openttd_saved_compile_options}")
