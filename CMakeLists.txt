cmake_minimum_required(VERSION 3.22)
project(crollo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Box3D builds with the static MSVC runtime; use the same one everywhere so the link agrees.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# --- Box3D: the physics engine this game is built around ---
# Pinned to a known-good commit. Downloaded as an archive, which is much faster than a clone.
FetchContent_Declare(
	box3d
	URL https://github.com/erincatto/box3d/archive/9e5a4cde862fba95ff19f096b79567f3ea6c01fd.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# --- raylib: window, input, rendering and audio ---
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_OGG OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_MP3 OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_QOA OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_XM OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_MOD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	raylib
	URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Web build (emcmake): WebGL2 through raylib's OpenGL ES 3 path
if(EMSCRIPTEN)
	set(PLATFORM Web CACHE STRING "" FORCE)
	set(OPENGL_VERSION "ES 3.0" CACHE STRING "" FORCE)
endif()

FetchContent_MakeAvailable(box3d raylib)

add_executable(crollo
	src/main.cpp
	src/game.cpp
	src/game.h
	src/physics.cpp
	src/physics.h
	src/levels.cpp
	src/levels.h
	src/render.cpp
	src/render.h
	src/audio.cpp
	src/audio.h
	src/particles.cpp
	src/particles.h
	src/ui.cpp
	src/ui.h
	src/math_util.h
)

target_link_libraries(crollo PRIVATE box3d raylib)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(crollo PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(EMSCRIPTEN)
	# Produces crollo.js + crollo.wasm; the font is embedded in the wasm, so there is no data file.
	# Single threaded on purpose: no SharedArrayBuffer, so any static host works.
	set_target_properties(crollo PROPERTIES SUFFIX ".js")
	target_link_options(crollo PRIVATE
		-sUSE_GLFW=3
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2
		-sALLOW_MEMORY_GROWTH=1
		-sSTACK_SIZE=1048576
		-sFORCE_FILESYSTEM=1
		-sENVIRONMENT=web
		# miniaudio (raylib's audio) reads Module.HEAPF32 from its Web Audio callback
		-sEXPORTED_RUNTIME_METHODS=HEAPF32
		"--embed-file=${CMAKE_SOURCE_DIR}/assets/fonts/LilitaOne-Regular.ttf@/assets/fonts/LilitaOne-Regular.ttf"
	)
	# The page is written in fragment form (no html/head/body); wrap it into a standalone index.html.
	file(READ ${CMAKE_SOURCE_DIR}/web/crollo.html CROLLO_PAGE)
	file(WRITE ${CMAKE_BINARY_DIR}/index.html
		"<!doctype html>\n<html lang=\"it\">\n<head>\n<meta charset=\"utf-8\">\n"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n</head>\n<body>\n"
		"${CROLLO_PAGE}\n</body>\n</html>\n")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/web/crollo.html)
else()
	# Assets are looked up next to the executable, so copy them after each build.
	add_custom_command(TARGET crollo POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crollo>/assets
	)
endif()
