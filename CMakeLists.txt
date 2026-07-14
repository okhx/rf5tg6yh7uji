cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /FS")

project(grape VERSION 1.0.0)

set(ARC_FEATURE_FULL ON CACHE BOOL "" FORCE)

file(GLOB IMGUI_VENDOR_CANDIDATES LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/*/lib/imgui_lib")
list(LENGTH IMGUI_VENDOR_CANDIDATES IMGUI_VENDOR_COUNT)
if (NOT IMGUI_VENDOR_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one vendored imgui_lib directory")
endif()
list(GET IMGUI_VENDOR_CANDIDATES 0 IMGUI_VENDOR_DIR)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp

    src/bot/bot.cpp
    src/bot/updater.cpp
    src/bot/scheduler.cpp

    src/assist/autoclicker.cpp
    src/assist/hitboxes.cpp

    src/hooks/CCActionManager.cpp
    src/hooks/CCDirector.cpp
    src/hooks/CCScheduler.cpp
    src/hooks/EnhancedGameObject.cpp
    src/hooks/FMODAudioEngine.cpp
    src/hooks/GJBaseGameLayer.cpp
    src/hooks/GameObject.cpp
    src/hooks/LevelEditorLayer.cpp
    src/hooks/PauseLayer.cpp
    src/hooks/PlayLayer.cpp
    src/hooks/PlayerObject.cpp
    src/hooks/ShaderLayer.cpp
    src/hooks/VideoOptionsLayer.cpp

    src/ui/manager.cpp
    src/ui/hook.cpp
    src/ui/theme.cpp

    ${IMGUI_VENDOR_DIR}/imgui.cpp
    ${IMGUI_VENDOR_DIR}/imgui_draw.cpp
    ${IMGUI_VENDOR_DIR}/imgui_tables.cpp
    ${IMGUI_VENDOR_DIR}/imgui_widgets.cpp
    ${IMGUI_VENDOR_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl3.cpp

    src/replay/replay.cpp
    src/replay/system.cpp

    src/physics/collisions.cpp
    src/physics/gjbasegamelayer.cpp
    src/physics/gravity.cpp
    src/physics/object.cpp
    src/physics/player.cpp

    src/label/label.cpp

    src/checkpoint/fix.cpp
    src/checkpoint/checkpoint.cpp

    src/trajectory/trajectory.cpp

    src/render/renderer.cpp
    src/render/texture.cpp
    src/render/dsp.cpp
    src/render/pass.cpp

    src/util/crash_log.cpp

    src/hooks/GJEffectManager.cpp
    src/ui/touch_overlay.cpp
)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_win32.cpp
        src/util/midhook.cpp
    )
endif()

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-dollar-in-identifier-extension)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    src include
    ${IMGUI_VENDOR_DIR}
    ${IMGUI_VENDOR_DIR}/backends
)

# Geode/cocos already provides and initializes GLEW. Prevent Dear ImGui's
# OpenGL backend from layering its private GL3W loader over the same symbols.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:stephenberry/glaze#main")
target_link_libraries(${PROJECT_NAME} glaze::glaze)

if (WIN32)
    CPMAddPackage("gh:zyantific/zydis#bffbb61")
    target_link_libraries(${PROJECT_NAME} Zydis)

    CPMAddPackage("gh:cursey/safetyhook#main")
    target_link_libraries(${PROJECT_NAME} safetyhook)
endif()

CPMAddPackage("gh:peonii/slc#main")

if (WIN32)
    target_link_libraries(${PROJECT_NAME} libslc dbghelp)
else()
    target_link_libraries(${PROJECT_NAME} libslc)
endif()
target_compile_options(GeodeBindings PRIVATE -Wno-overloaded-virtual -Wno-unused-variable)

setup_geode_mod(${PROJECT_NAME})

# Geode enables -Wpedantic and -Werror as part of setup_geode_mod. Its
# lifecycle helpers intentionally use '$' in their macro names, which Apple
# Clang diagnoses as an extension when compiling iOS targets.
if (APPLE)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wno-error=dollar-in-identifier-extension
    )

    # Dear ImGui is vendored source. Apple Clang diagnoses a few declarations
    # that are conditionally unused in its upstream implementation; do not
    # let Geode's global -Werror reject those third-party translation units.
    set_source_files_properties(
        ${IMGUI_VENDOR_DIR}/imgui.cpp
        ${IMGUI_VENDOR_DIR}/imgui_draw.cpp
        ${IMGUI_VENDOR_DIR}/imgui_tables.cpp
        ${IMGUI_VENDOR_DIR}/imgui_widgets.cpp
        ${IMGUI_VENDOR_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_VENDOR_DIR}/backends/imgui_impl_opengl3.cpp
        PROPERTIES COMPILE_OPTIONS
        "-Wno-error=unused-variable;-Wno-error=unused-parameter"
    )
endif()
