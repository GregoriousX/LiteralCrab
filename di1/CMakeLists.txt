if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The DI1 ASI requires Windows and MSVC")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Disney Infinity 1.0 is 32-bit. Configure with -A Win32")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(DI1_COMMON
    src/di1/common.cpp
    src/di1/imports.cpp
    src/di1/display.cpp
    src/di1/frame_pacer.cpp
    src/minhook/buffer.c
    src/minhook/hook.c
    src/minhook/trampoline.c
    src/minhook/hde/hde32.c
)
add_library(LiteralCrab SHARED ${DI1_COMMON} src/di1/engine.cpp src/di1/main.cpp di1/exports.def)
set_target_properties(LiteralCrab PROPERTIES PREFIX "" SUFFIX ".asi")
target_include_directories(LiteralCrab PRIVATE include src)
target_compile_definitions(LiteralCrab PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(LiteralCrab PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(LiteralCrab PRIVATE user32 gdi32)
add_custom_command(TARGET LiteralCrab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/di1/LiteralCrab.ini" "$<TARGET_FILE_DIR:LiteralCrab>/LiteralCrab.ini")

option(CRABELOADER_DI1_TESTS "Build DI1 policy and D3D9 integration tests" ON)
if(CRABELOADER_DI1_TESTS)
    enable_testing()
    add_executable(di1_policy_tests di1/tests/policy.cpp)
    target_include_directories(di1_policy_tests PRIVATE src)
    target_compile_definitions(di1_policy_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(di1_policy_tests PRIVATE /W4 /utf-8 /permissive-)
    add_test(NAME di1_policy COMMAND di1_policy_tests)
    add_executable(di1_frame_pacer_tests di1/tests/frame_pacer.cpp src/di1/frame_pacer.cpp)
    target_include_directories(di1_frame_pacer_tests PRIVATE src)
    target_compile_definitions(di1_frame_pacer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(di1_frame_pacer_tests PRIVATE /W4 /utf-8 /permissive-)
    add_test(NAME di1_frame_pacer COMMAND di1_frame_pacer_tests)
    add_executable(di1_d3d9_smoke di1/tests/d3d9_smoke.cpp ${DI1_COMMON})
    target_include_directories(di1_d3d9_smoke PRIVATE include src)
    target_compile_definitions(di1_d3d9_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(di1_d3d9_smoke PRIVATE /W4 /utf-8 /permissive-)
    target_link_libraries(di1_d3d9_smoke PRIVATE d3d9 user32 gdi32 dwmapi delayimp)
    target_link_options(di1_d3d9_smoke PRIVATE /DELAYLOAD:d3d9.dll)
endif()
