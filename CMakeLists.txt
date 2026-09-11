# CMakeLists.txt — builds the Notepad++ "Project Manager" plugin as
# NppProjectManager.dll.
#
# This produces a native Windows DLL, so it must be built on Windows (or
# cross-compiled with MinGW-w64) — it cannot be built from this repo's own
# checkout location on a non-Windows machine. See README.md for exact
# build steps with Visual Studio.

cmake_minimum_required(VERSION 3.15)
project(NppProjectManager LANGUAGES CXX RC)

if (NOT WIN32)
	message(FATAL_ERROR
		"NppProjectManager is a Notepad++ plugin: it links against the Win32 "
		"API and can only be built targeting Windows (native MSVC, or a "
		"MinGW-w64 cross-compiler). Configure with a Windows toolchain.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(NppProjectManager SHARED
	src/PluginEntry.cpp
	src/PluginDefinition.cpp
	src/ProjectManager.cpp
	src/Project.cpp
	src/ProjectPanel.cpp
	src/TabContextMenu.cpp
	src/PluginResource.rc
	sdk/DockingFeature/StaticDialog.cpp
)

target_include_directories(NppProjectManager PRIVATE
	src
	sdk
	sdk/DockingFeature
)

target_compile_definitions(NppProjectManager PRIVATE
	UNICODE
	_UNICODE
	WIN32_LEAN_AND_MEAN
	NOMINMAX
	_WIN32_WINNT=0x0601   # Windows 7+: guarantees SetWindowSubclass (comctl32) is declared
)

target_link_libraries(NppProjectManager PRIVATE
	comctl32
	shell32
	shlwapi
	comdlg32
	ole32
)

set_target_properties(NppProjectManager PROPERTIES
	PREFIX ""
	SUFFIX ".dll"
)

# Notepad++ plugins must match the bitness of the Notepad++ install they
# load into (x86 vs x64). With the Visual Studio generator, pick the
# platform at configure time: cmake -A x64 .. (or -A Win32 for the 32-bit
# build). With a MinGW-w64 toolchain, use the x86_64-w64-mingw32-* or
# i686-w64-mingw32-* compilers to get the matching bitness.
