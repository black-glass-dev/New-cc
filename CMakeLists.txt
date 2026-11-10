cmake_minimum_required(VERSION 3.20)
project(owncut_engine VERSION 0.1.0 LANGUAGES CXX)

option(OWNCUT_BUILD_TESTS "Build portable engine unit tests" ON)
option(OWNCUT_ENABLE_TIMELINE_ENGINE "Build modular C++20 timeline_engine" ON)

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

if(OWNCUT_ENABLE_TIMELINE_ENGINE)
    set(OWNCUT_TIMELINE_BUILD_TESTS ${OWNCUT_BUILD_TESTS})
    set(OWNCUT_TIMELINE_BUNDLE_JNI OFF)
    add_subdirectory(modules/timeline_engine)
endif()

if(ANDROID)
    set(OWNCUT_ANDROID_SOURCES
        platform/android/jni/editor_jni.cpp
        platform/android/jni/color_jni.cpp)
    if(OWNCUT_ENABLE_TIMELINE_ENGINE)
        list(APPEND OWNCUT_ANDROID_SOURCES modules/timeline_engine/bridge/android_jni.cpp)
    endif()
    add_library(owncut_android SHARED ${OWNCUT_ANDROID_SOURCES})
    target_link_libraries(owncut_android PRIVATE owncut_engine)
    if(OWNCUT_ENABLE_TIMELINE_ENGINE)
        target_link_libraries(owncut_android PRIVATE timeline_engine::timeline_engine)
    endif()
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
    if(OWNCUT_ENABLE_TIMELINE_ENGINE)
        add_executable(owncut_timeline_integration_tests tests/unit/timeline_integration_tests.cpp)
        target_link_libraries(owncut_timeline_integration_tests PRIVATE owncut_engine timeline_engine::timeline_engine)
        add_test(NAME owncut_timeline_integration_tests COMMAND owncut_timeline_integration_tests)
    endif()
endif()
