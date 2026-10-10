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
# Engine-neutral script mod loader; src/di1/mods.cpp holds the DI1 Lua API profile.
set(SCRIPT_MODS src/mods/mod_index.cpp src/mods/lua_mods.cpp)
# Asset mods: ordinary ZIP packages, the game's archive layout and file redirection.
set(ASSET_MODS src/mods/zip.cpp src/mods/asset_mods.cpp src/mods/file_redirect.cpp src/third_party/puff/puff.c)
# Vendored zlib puff, kept byte-for-byte; its narrowing conversions are intentional.
set_source_files_properties(src/third_party/puff/puff.c PROPERTIES COMPILE_OPTIONS /wd4244)
# Startup-only discovery and archive building: optimized for size. The per-call paths (chunk and
# file lookups, hooks) stay in their own files at full speed.
set_source_files_properties(src/mods/mod_index.cpp src/mods/asset_mods.cpp PROPERTIES COMPILE_OPTIONS /O1)
set(MINHOOK src/minhook/buffer.c src/minhook/hook.c src/minhook/trampoline.c src/minhook/hde/hde32.c)
add_library(LiteralCrab SHARED ${DI1_COMMON} ${SCRIPT_MODS} ${ASSET_MODS} src/di1/engine.cpp src/di1/mods.cpp src/di1/adapters.cpp src/di1/asset_loader.cpp src/di1/camera.cpp src/di1/gui_screens.cpp src/di1/post_effects.cpp src/di1/main.cpp di1/exports.def)
set_target_properties(LiteralCrab PROPERTIES PREFIX "" SUFFIX ".asi")
target_include_directories(LiteralCrab PRIVATE include src)
target_compile_definitions(LiteralCrab PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS LITERALCRAB_VERSION="${PROJECT_VERSION}")
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
    add_executable(di1_mod_index_tests di1/tests/mod_index.cpp src/mods/mod_index.cpp)
    target_include_directories(di1_mod_index_tests PRIVATE src)
    target_compile_definitions(di1_mod_index_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(di1_mod_index_tests PRIVATE /W4 /utf-8 /permissive-)
    add_test(NAME di1_mod_index COMMAND di1_mod_index_tests)
    add_executable(di1_asset_mods_tests di1/tests/asset_mods.cpp src/mods/mod_index.cpp ${ASSET_MODS} ${MINHOOK})
    target_include_directories(di1_asset_mods_tests PRIVATE include src)
    target_compile_definitions(di1_asset_mods_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(di1_asset_mods_tests PRIVATE /W4 /utf-8 /permissive-)
    add_test(NAME di1_asset_mods COMMAND di1_asset_mods_tests)
    # The mod runtime is exercised against the reference Lua 5.1.5 interpreter, the version the
    # game embeds. Downloaded from lua.org at configure time and pinned by hash; tests only.
    option(CRABELOADER_LUA_TESTS "Test the script mod runtime against reference Lua 5.1.5" ON)
    if(CRABELOADER_LUA_TESTS)
        include(FetchContent)
        FetchContent_Declare(lua51
            URL https://www.lua.org/ftp/lua-5.1.5.tar.gz
            URL_HASH SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(lua51)
        set(LUA51_MODULES lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser
            lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib ldblib liolib lmathlib loslib
            ltablib lstrlib loadlib linit)
        list(TRANSFORM LUA51_MODULES PREPEND "${lua51_SOURCE_DIR}/src/")
        list(TRANSFORM LUA51_MODULES APPEND ".c")
        add_library(lua51_reference STATIC ${LUA51_MODULES})
        target_include_directories(lua51_reference PUBLIC "${lua51_SOURCE_DIR}/src")
        target_compile_definitions(lua51_reference PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(lua51_reference PRIVATE /W0)
        add_executable(di1_lua_mods_tests di1/tests/lua_mods.cpp ${SCRIPT_MODS})
        target_include_directories(di1_lua_mods_tests PRIVATE src)
        target_compile_definitions(di1_lua_mods_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(di1_lua_mods_tests PRIVATE /W4 /utf-8 /permissive-)
        target_link_libraries(di1_lua_mods_tests PRIVATE lua51_reference)
        add_test(NAME di1_lua_mods COMMAND di1_lua_mods_tests)
    endif()
    add_executable(di1_d3d9_smoke di1/tests/d3d9_smoke.cpp ${DI1_COMMON})
    target_include_directories(di1_d3d9_smoke PRIVATE include src)
    target_compile_definitions(di1_d3d9_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS LITERALCRAB_VERSION="${PROJECT_VERSION}")
    target_compile_options(di1_d3d9_smoke PRIVATE /W4 /utf-8 /permissive-)
    target_link_libraries(di1_d3d9_smoke PRIVATE d3d9 user32 gdi32 dwmapi delayimp)
    target_link_options(di1_d3d9_smoke PRIVATE /DELAYLOAD:d3d9.dll)
endif()
