cmake_minimum_required(VERSION 3.20)
project(SimpleCad VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- External dependencies (only 2: GLFW + ImGui) ---
include(FetchContent)

# GLFW - cross-platform windowing
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui - immediate mode UI
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static lib (it has no CMakeLists.txt)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
if(APPLE)
    target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION)
endif()

# --- SimpleCad executable ---
add_executable(simplecad
    src/main.cpp
    src/input.cpp
    src/tool_edge_modifier.cpp
    src/extrude.cpp
    src/csg.cpp
    src/camera.cpp
    src/scene.cpp
    src/picking.cpp
    src/ui.cpp
    src/gizmo.cpp
    src/geometry.cpp
    src/sketch.cpp
    src/renderer.cpp
    src/undo.cpp
    src/save_load.cpp
    src/theme.cpp
    src/tool_move.cpp
)

if(APPLE)
    target_sources(simplecad PRIVATE src/platform_dialogs_mac.m)
endif()

target_include_directories(simplecad PRIVATE src)
target_link_libraries(simplecad PRIVATE glfw imgui)

# Platform OpenGL linking
if(APPLE)
    target_link_libraries(simplecad PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
    target_compile_definitions(simplecad PRIVATE GL_SILENCE_DEPRECATION)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(simplecad PRIVATE OpenGL::GL dl pthread)
elseif(WIN32)
    target_link_libraries(simplecad PRIVATE opengl32)
endif()

# Compiler warnings
target_compile_options(simplecad PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# --- Kernel tests -----------------------------------------------------------
# The geometry kernel is pure and its failures are silent, so it gets a test
# target.  No framework: build it and run it.
add_executable(kernel_tests
    tests/kernel_tests.cpp
    src/geometry.cpp
    src/csg.cpp
    src/sketch.cpp
)
target_include_directories(kernel_tests PRIVATE src)
target_compile_options(kernel_tests PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wno-unused-parameter>
)
