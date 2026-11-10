cmake_minimum_required(VERSION 3.16)
project(timeline_engine VERSION 1.0.0 LANGUAGES CXX)

option(OWNCUT_TIMELINE_BUILD_TESTS "Build timeline_engine unit and integration tests" ON)
option(OWNCUT_TIMELINE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(OWNCUT_TIMELINE_ENABLE_FFMPEG "Probe and link FFmpeg libraries if installed" ON)
option(OWNCUT_TIMELINE_BUNDLE_JNI "Bundle android_jni.cpp inside timeline_engine static library" ON)

find_package(Threads REQUIRED)

set(OWNCUT_HAS_FFMPEG FALSE)
if(OWNCUT_TIMELINE_ENABLE_FFMPEG AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
        if(FFMPEG_FOUND)
            set(OWNCUT_HAS_FFMPEG TRUE)
        endif()
    endif()
endif()

set(TIMELINE_ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rational_time.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/memory_pool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/task_scheduler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/media_pool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timeline_model.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/command_stack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/edit_operations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/trim_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/retime_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/animation_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text_subtitle_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vision_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mobile_ux.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/owncut_adapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timeline_c_api.cpp"
)

if(ANDROID AND OWNCUT_TIMELINE_BUNDLE_JNI)
    list(APPEND TIMELINE_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bridge/android_jni.cpp")
endif()

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND TIMELINE_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bridge/ios_bridge.mm")
endif()

add_library(timeline_engine STATIC ${TIMELINE_ENGINE_SOURCES})
add_library(timeline_engine::timeline_engine ALIAS timeline_engine)

target_compile_features(timeline_engine PUBLIC cxx_std_20)
set_target_properties(timeline_engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(timeline_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bridge>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(timeline_engine PUBLIC Threads::Threads)

if(ANDROID)
    target_link_libraries(timeline_engine PUBLIC mediandk)
endif()

if(OWNCUT_HAS_FFMPEG)
    target_link_libraries(timeline_engine PUBLIC PkgConfig::FFMPEG)
    target_compile_definitions(timeline_engine PUBLIC OWNCUT_HAS_FFMPEG=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(timeline_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(OWNCUT_TIMELINE_SANITIZERS)
        target_compile_options(timeline_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(timeline_engine PUBLIC -fsanitize=address,undefined)
    endif()
endif()

if(OWNCUT_TIMELINE_BUILD_TESTS)
    enable_testing()

    function(owncut_add_timeline_test target_name source_rel)
        add_executable(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/${source_rel}")
        target_link_libraries(${target_name} PRIVATE timeline_engine)
        target_compile_features(${target_name} PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ${target_name} COMMAND ${target_name})
    endfunction()

    owncut_add_timeline_test(timeline_test_phase1_infra "tests/test_phase1_infra.cpp")
    owncut_add_timeline_test(timeline_test_phase2_timeline "tests/test_phase2_timeline.cpp")
    owncut_add_timeline_test(timeline_test_phase3_media_ai "tests/test_phase3_media_ai.cpp")
    owncut_add_timeline_test(timeline_test_phase4_e2e_bridges "tests/test_phase4_e2e_bridges.cpp")
    owncut_add_timeline_test(timeline_test_owncut_adapter "tests/test_owncut_adapter.cpp")
endif()
