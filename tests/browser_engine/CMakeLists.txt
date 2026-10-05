cmake_minimum_required(VERSION 3.24)
project(moss_browser_engine_test C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Match the ESP port: these public structs and upstream walkers share the macro.
add_compile_definitions(LWS_DLO_STACK_DEPTH=16)
# Never modify the caller's shared upstream checkout. Test exactly the source
# edits used by the target build in a disposable, marker-owned private copy.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/lhp_source_patches.py"
    --source "${LHP_UPSTREAM_SOURCE}"
    --copy-to "${CMAKE_CURRENT_BINARY_DIR}/moss-lhp-source"
    COMMAND_ERROR_IS_FATAL ANY)
set(LHP_UPSTREAM_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/moss-lhp-source")
# Reuse the pinned experiment's narrow upstream feature configuration. Its
# executables are excluded; only the real LHP library is a dependency here.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../experiments/lhp_browser/host"
    probe EXCLUDE_FROM_ALL)
add_executable(browser-engine-test browser_engine_test.cpp
    ../../firmware/sloth_pet/browser_engine.c
    ../../experiments/lhp_browser/host/network_guard.c)
target_include_directories(browser-engine-test PRIVATE
    ../../firmware/sloth_pet "${LHP_UPSTREAM_SOURCE}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/probe/upstream"
    "${LHP_UPSTREAM_SOURCE}/contrib/mcufont/fonts")
target_compile_definitions(browser-engine-test PRIVATE BROWSER_ENGINE_TEST_CLOCK=1)
target_compile_options(browser-engine-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(browser-engine-test PRIVATE websockets)
enable_testing()
add_test(NAME browser-engine COMMAND browser-engine-test)

# Exercise the genuine upstream arena API through the configured allocator,
# including rejection before the first chunk and when growing an existing arena.
add_executable(browser-arena-test browser_arena_test.cpp)
target_include_directories(browser-arena-test PRIVATE
    "${LHP_UPSTREAM_SOURCE}/include" "${CMAKE_CURRENT_BINARY_DIR}/probe/upstream")
target_compile_options(browser-arena-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(browser-arena-test PRIVATE websockets)
add_test(NAME browser-arena COMMAND browser-arena-test)

# Visual QA uses the same production parser/renderer and actual chrome rows.
add_executable(browser-page-preview ../../tools/browser_page_preview.cpp
    ../../firmware/sloth_pet/browser_engine.c
    ../../firmware/sloth_pet/browser_ui.cpp
    ../../firmware/sloth_pet/keyboard.cpp
    ../../experiments/lhp_browser/host/network_guard.c)
target_include_directories(browser-page-preview PRIVATE
    ../../firmware/sloth_pet "${LHP_UPSTREAM_SOURCE}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/probe/upstream"
    "${LHP_UPSTREAM_SOURCE}/contrib/mcufont/fonts")
target_compile_options(browser-page-preview PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(browser-page-preview PRIVATE websockets)

add_executable(browser-assets-test browser_assets_test.cpp
    ../../firmware/sloth_pet/browser_ui.cpp
    ../../firmware/sloth_pet/browser_reader.cpp ../../firmware/sloth_pet/keyboard.cpp
    ../../firmware/sloth_pet/browser_assets.cpp ../../firmware/sloth_pet/browser_asset_decode.cpp
    ../../firmware/sloth_pet/browser_memory.cpp ../../firmware/sloth_pet/browser_url.cpp
    ../../firmware/sloth_pet/browser_engine.c ../../experiments/lhp_browser/host/network_guard.c)
target_include_directories(browser-assets-test PRIVATE ../../firmware/sloth_pet
    "${LHP_UPSTREAM_SOURCE}/include" "${CMAKE_CURRENT_BINARY_DIR}/probe/upstream"
    "${LHP_UPSTREAM_SOURCE}/contrib/mcufont/fonts")
target_compile_definitions(browser-assets-test PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures")
target_compile_options(browser-assets-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(browser-assets-test PRIVATE websockets)
add_test(NAME browser-assets COMMAND browser-assets-test)
