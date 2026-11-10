cmake_minimum_required(VERSION 3.20)
project(owncut_engine VERSION 0.1.0 LANGUAGES CXX)

option(OWNCUT_BUILD_TESTS "Build portable engine unit tests" ON)

add_library(owncut_engine
    engine/color/src/color_gpu.cpp
    engine/color/src/color.cpp
    engine/session/src/session_registry.cpp
    engine/session/src/session_coordinator.cpp
    engine/graph/src/graph.cpp
    engine/core/src/time.cpp
    engine/timeline/src/timeline.cpp
    engine/commands/src/command_history.cpp
    engine/project/src/project.cpp)
target_include_directories(owncut_engine PUBLIC
    engine/color/include
    engine/session/include
    engine/graph/include
    engine/core/include
    engine/timeline/include
    engine/commands/include
    engine/project/include)
target_compile_features(owncut_engine PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(owncut_engine PUBLIC Threads::Threads)
set_target_properties(owncut_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(owncut_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(ANDROID)
    add_library(owncut_android SHARED platform/android/jni/editor_jni.cpp platform/android/jni/color_jni.cpp)
    target_link_libraries(owncut_android PRIVATE owncut_engine)
    # Standalone color reference module, Android APK only (CPU path). Host CI does not build it.
    set(COLOR_BUILD_TESTS OFF)
    set(COLOR_BUILD_VULKAN OFF)
    set(COLOR_WARNINGS_AS_ERRORS OFF)
    add_subdirectory(modules/color_engine)
    target_sources(owncut_android PRIVATE platform/android/jni/color_module_jni.cpp)
    target_link_libraries(owncut_android PRIVATE color::engine)
    target_compile_features(owncut_android PRIVATE cxx_std_20)
endif()

if(OWNCUT_BUILD_TESTS)
    enable_testing()
    add_executable(owncut_color_gpu_tests tests/unit/color_gpu_tests.cpp)
    target_link_libraries(owncut_color_gpu_tests PRIVATE owncut_engine)
    add_test(NAME owncut_color_gpu_tests COMMAND owncut_color_gpu_tests)
    add_executable(owncut_grade_project_tests tests/unit/grade_project_tests.cpp)
    target_link_libraries(owncut_grade_project_tests PRIVATE owncut_engine)
    add_test(NAME owncut_grade_project_tests COMMAND owncut_grade_project_tests)
    add_executable(owncut_color_tests tests/unit/color_tests.cpp)
    target_link_libraries(owncut_color_tests PRIVATE owncut_engine)
    add_test(NAME owncut_color_tests COMMAND owncut_color_tests)
    add_executable(owncut_core_tests tests/unit/core_tests.cpp)
    target_link_libraries(owncut_core_tests PRIVATE owncut_engine)
    add_test(NAME owncut_core_tests COMMAND owncut_core_tests)
    add_executable(owncut_session_tests tests/unit/session_tests.cpp)
    target_link_libraries(owncut_session_tests PRIVATE owncut_engine Threads::Threads)
    add_test(NAME owncut_session_tests COMMAND owncut_session_tests)
    add_executable(owncut_checkpoint_tests tests/unit/checkpoint_tests.cpp)
    target_link_libraries(owncut_checkpoint_tests PRIVATE owncut_engine Threads::Threads)
    add_test(NAME owncut_checkpoint_tests COMMAND owncut_checkpoint_tests)
endif()
