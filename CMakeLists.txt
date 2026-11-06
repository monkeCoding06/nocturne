cmake_minimum_required(VERSION 3.20)
project(nocturne VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NOCTURNE_BUILD_TESTS "Build the automated test suite" ON)
option(NOCTURNE_BUILD_APP "Build the terminal application (needs FTXUI + miniaudio)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

if(NOCTURNE_BUILD_APP)
  set(FTXUI_BUILD_EXAMPLES OFF CACHE INTERNAL "")
  set(FTXUI_BUILD_DOCS OFF CACHE INTERNAL "")
  set(FTXUI_BUILD_TESTS OFF CACHE INTERNAL "")
  set(FTXUI_ENABLE_INSTALL OFF CACHE INTERNAL "")
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
    GIT_SHALLOW TRUE)
  # miniaudio is a single header; SOURCE_SUBDIR points at a directory without a
  # CMakeLists so that it is only downloaded, not built by its own project.
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio
    GIT_TAG 0.11.24
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-add)
  FetchContent_MakeAvailable(json ftxui miniaudio)
else()
  FetchContent_MakeAvailable(json)
endif()

set(NOCTURNE_WARNINGS "")
if(MSVC)
  list(APPEND NOCTURNE_WARNINGS /W4 /permissive- /utf-8)
else()
  list(APPEND NOCTURNE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Core: DSP, modules, patch graph, engine, input logic. No audio hardware, no TUI.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  src/dsp/*.cpp src/modules/*.cpp src/patch/*.cpp src/input/*.cpp
  src/audio/Engine.cpp src/audio/Rack.cpp src/audio/VoiceAllocator.cpp)

# Factory presets: presets/*.json are the single source of truth. They are
# compiled into the executable at build time (works from any cwd / install state).
file(GLOB PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/presets/*.json)
set(PRESET_GENERATED ${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp)
add_custom_command(
  OUTPUT ${PRESET_GENERATED}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
  COMMAND ${CMAKE_COMMAND} -DPRESET_DIR=${CMAKE_SOURCE_DIR}/presets -DOUT=${PRESET_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/EmbedPresets.cmake
  DEPENDS ${PRESET_FILES} ${CMAKE_SOURCE_DIR}/presets/index.txt ${CMAKE_SOURCE_DIR}/cmake/EmbedPresets.cmake
  COMMENT "Embedding presets/*.json"
  VERBATIM)
add_library(nocturne_core STATIC ${CORE_SOURCES} ${PRESET_GENERATED})
target_include_directories(nocturne_core PUBLIC src)
target_link_libraries(nocturne_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(nocturne_core PRIVATE ${NOCTURNE_WARNINGS})
if(NOT MSVC)
  find_package(Threads REQUIRED)
  target_link_libraries(nocturne_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
if(NOCTURNE_BUILD_APP)
  add_library(nocturne_device STATIC src/audio/AudioDevice.cpp src/audio/miniaudio_impl.c src/app/Session.cpp)
  target_include_directories(nocturne_device PUBLIC src)
  target_include_directories(nocturne_device SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
  target_link_libraries(nocturne_device PUBLIC nocturne_core)
  target_compile_options(nocturne_device PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${NOCTURNE_WARNINGS}>)
  if(UNIX AND NOT APPLE)
    target_link_libraries(nocturne_device PUBLIC dl m)
  endif()

  file(GLOB_RECURSE TUI_SOURCES CONFIGURE_DEPENDS src/tui/*.cpp)
  add_executable(nocturne src/main.cpp ${TUI_SOURCES})
  target_link_libraries(nocturne PRIVATE nocturne_device
    ftxui::screen ftxui::dom ftxui::component)
  target_compile_options(nocturne PRIVATE ${NOCTURNE_WARNINGS})
  if(MINGW)
    target_link_options(nocturne PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  install(TARGETS nocturne RUNTIME DESTINATION bin)
  install(DIRECTORY presets/ DESTINATION share/nocturne/presets)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NOCTURNE_BUILD_TESTS)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(nocturne_tests ${TEST_SOURCES})
  target_link_libraries(nocturne_tests PRIVATE nocturne_core)
  target_compile_definitions(nocturne_tests PRIVATE NOCTURNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(NOCTURNE_BUILD_APP)
    # Session (patch load/swap behaviour) lives in the device library; construction needs no audio hardware.
    target_link_libraries(nocturne_tests PRIVATE nocturne_device)
    target_compile_definitions(nocturne_tests PRIVATE NOCTURNE_HAS_SESSION=1)
  endif()
  target_compile_options(nocturne_tests PRIVATE ${NOCTURNE_WARNINGS})
  add_test(NAME nocturne_tests COMMAND nocturne_tests)
endif()
