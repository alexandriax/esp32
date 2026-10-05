cmake_minimum_required(VERSION 3.24)
project(moss_lhp_probe C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT IS_DIRECTORY "${LHP_UPSTREAM_SOURCE}/include/libwebsockets")
  message(FATAL_ERROR "Use probe.py build to provide the checksum-verified upstream source")
endif()
# File-only host probe. The future target needs a separately verified TLS build.
foreach(feature SSL SHARED MINIMAL_EXAMPLES SYS_ASYNC_DNS SYS_NTPCLIENT
    UDP HTTP3 DRIVERS OTA SVG GIF CACHE_BLOB CACHE_NSCOOKIEJAR
    DISKCACHE HL MD JSONRPC STUB LEJP_CONF DIR SYS_SMD CONMON WOL
    HTTP_BASIC_AUTH HTTP_DIGEST_AUTH HTTP_UNCOMMON_HEADERS)
  set(LWS_WITH_${feature} OFF CACHE BOOL "Narrow offline LHP probe" FORCE)
endforeach()
foreach(feature STATIC LHP LHP_UA_FLASH DLO LWSAC LEJP SECURE_STREAMS
    FILE_OPS UPNG JPEG GZINFLATE HTTP2)
  set(LWS_WITH_${feature} ON CACHE BOOL "Required by LHP probe" FORCE)
endforeach()
set(LWS_WITH_SECURE_STREAMS_STATIC_POLICY_ONLY OFF CACHE BOOL "LHP dynamic URL policy" FORCE)
set(LWS_ROLE_WS OFF CACHE BOOL "No websockets" FORCE)
set(LWS_WITHOUT_SERVER ON CACHE BOOL "Client only" FORCE)
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "No upstream apps or device tests" FORCE)
set(LWS_WITHOUT_EXTENSIONS ON CACHE BOOL "No websocket extensions" FORCE)
set(LWS_WITH_NETWORK ON CACHE BOOL "Secure Streams file transport dependencies" FORCE)
set(LWS_WITH_SYS_STATE ON CACHE BOOL "Secure Streams dependency" FORCE)
set(LWS_MAX_SMP 1 CACHE STRING "One service thread" FORCE)
set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Probe optimization" FORCE)
# This upstream pin has unused variables in client-only configurations.
# Preserve diagnostics without making those upstream warnings fatal.
set(DISABLE_WERROR ON CACHE BOOL "Upstream client-only warning" FORCE)
add_subdirectory("${LHP_UPSTREAM_SOURCE}" upstream EXCLUDE_FROM_ALL)
add_executable(moss-lhp-probe main.c network_guard.c)
target_include_directories(moss-lhp-probe PRIVATE
  "${LHP_UPSTREAM_SOURCE}/include" "${CMAKE_CURRENT_BINARY_DIR}/upstream"
  "${LHP_UPSTREAM_SOURCE}/contrib/mcufont/fonts")
target_link_libraries(moss-lhp-probe PRIVATE websockets)
target_compile_options(moss-lhp-probe PRIVATE -Wall -Wextra -Werror)

# Run the same embedded-fixture engine used by the C6 image through the actual
# RGB565 stripe adapter. This is a host lifecycle test, not device emulation.
add_executable(moss-lhp-target-test ../tests/target_engine_test.cpp
  ../target/lhp_engine.c network_guard.c)
set_target_properties(moss-lhp-target-test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
target_include_directories(moss-lhp-target-test PRIVATE
  "${LHP_UPSTREAM_SOURCE}/include" "${CMAKE_CURRENT_BINARY_DIR}/upstream"
  "${LHP_UPSTREAM_SOURCE}/contrib/mcufont/fonts")
target_link_libraries(moss-lhp-target-test PRIVATE websockets)
target_compile_options(moss-lhp-target-test PRIVATE -Wall -Wextra -Werror)
# Tests intentionally use assert even when the measured library is MinSizeRel.
target_compile_options(moss-lhp-target-test PRIVATE -UNDEBUG)
