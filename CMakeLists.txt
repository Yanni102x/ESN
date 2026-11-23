cmake_minimum_required(VERSION 3.25)
project(ESN VERSION 1.0.0 LANGUAGES C CXX)

enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

# 单一静态 CRT：不依赖 vcruntime，便携即用
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

set(SOURCES
  src/main.cpp
  src/common.cpp
  src/store.cpp
  src/note_window.cpp
  src/reminder_picker.cpp
  src/settings_window.cpp
  src/app.cpp
  third_party/yyjson/yyjson.c
  res/esn.rc
)

add_executable(esn WIN32 ${SOURCES})

# 产物名固定大写：ESN.exe（Windows 不区分大小写，但资源管理器按写入时的大小写显示）
set_target_properties(esn PROPERTIES OUTPUT_NAME "ESN")

target_include_directories(esn PRIVATE src third_party third_party/yyjson)
target_compile_definitions(esn PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _CRT_SECURE_NO_WARNINGS)

target_link_libraries(esn PRIVATE
  user32 gdi32 shell32 comctl32 shlwapi ole32 uuid psapi
  d2d1 dwrite dwmapi uxtheme)

if(MSVC)
  target_compile_options(esn PRIVATE /W4 /permissive- /utf-8 /Gy /Gw
    $<$<CONFIG:Release>:/O2>)
  # manifest 由 res/esn.rc 以 RT_MANIFEST(1) 嵌入，禁止链接器再生成一份
  target_link_options(esn PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
  set_property(TARGET esn PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
else()
  target_compile_options(esn PRIVATE -Wall -Wextra -municode)
  target_link_options(esn PRIVATE -municode -mwindows -static -s)
endif()
