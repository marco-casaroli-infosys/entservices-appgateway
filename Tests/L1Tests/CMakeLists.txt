# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.8)

# ---------------------------------------------------------------------------
# Package discovery shims for L1Tests-only builds
#
# When configuring only Tests/L1Tests, plugin subdirectories (e.g. AppGateway)
# can call find_package(Plugins) / find_package(Definitions) when NAMESPACE is
# empty. Thunder's canonical package names are WPEFrameworkPlugins and
# WPEFrameworkDefinitions, so we provide local compatibility shims under
# Tests/L1Tests/cmake and ensure CMake can find them.
# ---------------------------------------------------------------------------
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake;${CMAKE_PREFIX_PATH}")

# -----------------------------------------------------------------------------
# L1Tests build products
#
# Historically this directory only built a shared object (L1TestsIN) that is
# loadable by Thunder. For coverage we additionally need a *runnable* test
# executable for AppGateway: AppGatewayL1Test.
# -----------------------------------------------------------------------------
set(PLUGIN_NAME L1TestsIN)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(APPGATEWAY_L1TEST_TARGET "")

# Standalone when configured directly with: cmake -S <repo>/Tests/L1Tests ...
set(_L1TESTS_STANDALONE_BUILD OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_L1TESTS_STANDALONE_BUILD ON)
endif()

# -----------------------------------------------------------------------------
# L1Tests-only configure support
#
# In the CI workflow we configure ONLY this directory:
#   cmake -S <repo>/Tests/L1Tests -B <build> ...
#
# Therefore, any plugin implementation we need for linkage must be pulled in
# explicitly from here.
# -----------------------------------------------------------------------------
#
# IMPORTANT:
# In the L1Tests-only build, the repository root CMakeLists.txt is NOT executed.
# Therefore, any global options/definitions normally set at repo root (like
# DISABLE_SECURITY_TOKEN) must be applied here *before* adding plugin
# subdirectories, otherwise plugin targets (e.g. AppGatewayCommon) will compile
# without the expected flags and can include SecurityAgent headers.
#
if(DISABLE_SECURITY_TOKEN)
    # Apply as a directory-level compile definition so all targets created under
    # this directory (including add_subdirectory()'d plugins) inherit it.
    add_compile_definitions(DISABLE_SECURITY_TOKEN)
endif()

if(USE_THUNDER_R4)
    # Keep Thunder interface selection consistent across tests and pulled-in
    # plugins (some helper headers use this define for interface differences).
    add_compile_definitions(USE_THUNDER_R4)
endif()

# Since that root file is not executed here, re-apply it
# explicitly so it matches the marker file path the L1 workflow/tests manage
# (/etc/rdkappmanagers) instead of permanently false.
if(NOT RDKAPPMANAGERS_PATH)
    set(RDKAPPMANAGERS_PATH "/etc/rdkappmanagers")
endif()
add_compile_definitions(RDKAPPMANAGERS_PATH="${RDKAPPMANAGERS_PATH}")

# ---------------------------------------------------------------------------
# ConfigGenerator integration (L1Tests-only builds)
#
# AppGateway/CMakeLists.txt calls `write_config(...)`, which is defined by
# WPEFramework's FindConfigGenerator.cmake module.
#
# In the act/CI flow, the repo-local ./install tree can be present but not
# readable (e.g. "find: './install': Permission denied"). Therefore, we must not
# rely solely on the installed location and should also fall back to an in-repo
# copy (ThunderTools) when available.
# ---------------------------------------------------------------------------
if(NOT COMMAND write_config)
    # (1) Prefer the installed CMake package when readable/present.
    #     In normal CI: -DCMAKE_PREFIX_PATH=<repo>/install/usr
    find_package(ConfigGenerator QUIET)

    # (2) Fallback: include FindConfigGenerator.cmake from in-repo ThunderTools.
    #     This helps when ./install is not readable under act.
    if(NOT COMMAND write_config)
        set(_repo_find_config_generator_candidates
            "${CMAKE_CURRENT_LIST_DIR}/../../build/ThunderTools/FindConfigGenerator.cmake"
            "${CMAKE_CURRENT_LIST_DIR}/../../ThunderTools/cmake/FindConfigGenerator.cmake.in"
        )

        foreach(_cand IN LISTS _repo_find_config_generator_candidates)
            if(EXISTS "${_cand}")
                include("${_cand}")
                break()
            endif()
        endforeach()
    endif()

    # (3) Last resort: search using prefix hints (CMAKE_PREFIX_PATH is a list).
    if(NOT COMMAND write_config)
        set(_wpefw_module_hints "")

        if(DEFINED CMAKE_INSTALL_PREFIX AND NOT "${CMAKE_INSTALL_PREFIX}" STREQUAL "")
            list(APPEND _wpefw_module_hints "${CMAKE_INSTALL_PREFIX}/include/WPEFramework/Modules")
        endif()

        foreach(_p IN LISTS CMAKE_PREFIX_PATH)
            if(NOT "${_p}" STREQUAL "")
                list(APPEND _wpefw_module_hints "${_p}/include/WPEFramework/Modules")
            endif()
        endforeach()

        find_path(_WPEFW_MODULES_DIR
            NAMES FindConfigGenerator.cmake
            HINTS ${_wpefw_module_hints}
        )

        if(_WPEFW_MODULES_DIR AND EXISTS "${_WPEFW_MODULES_DIR}/FindConfigGenerator.cmake")
            include("${_WPEFW_MODULES_DIR}/FindConfigGenerator.cmake")
        else()
            message(FATAL_ERROR "Could not locate FindConfigGenerator.cmake to provide write_config() for L1Tests-only builds.")
        endif()
    endif()
endif()

if(_L1TESTS_STANDALONE_BUILD)
    if(PLUGIN_APPGATEWAY)
        # AppGateway/CMakeLists.txt defines the implementation library target
        # which AppGatewayL1Test must link against to resolve symbols.
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../AppGateway ${CMAKE_BINARY_DIR}/AppGateway)

        if(TARGET AppGateway)
            set(APPGATEWAY_L1TEST_TARGET AppGateway)
        elseif(TARGET ${NAMESPACE}AppGateway)
            set(APPGATEWAY_L1TEST_TARGET ${NAMESPACE}AppGateway)
        else()
            message(FATAL_ERROR "AppGateway target was not created for the L1 tests-only build")
        endif()
    endif()

    if(PLUGIN_APPGATEWAYCOMMON)
        # Also allow building/staging AppGatewayCommon plugin shared library from
        # the same L1Tests-only build when requested.
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../AppGatewayCommon ${CMAKE_BINARY_DIR}/AppGatewayCommon)

        # Reduce the SystemDelegate proactive Subscribe timeout from 2000ms to
        # 100ms during L1 tests. No Thunder daemon runs in the test environment
        # so every Subscribe call exhausts its full timeout; 4 calls × 2000ms
        # adds ~8s of dead wait per Initialize() cycle.
        target_compile_definitions(${NAMESPACE}AppGatewayCommon PRIVATE
            SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS=100
        )
    endif()

    if(PLUGIN_APPNOTIFICATIONS)
        # AppNotifications/CMakeLists.txt defines the implementation library target
        # which AppNotificationsL1Test must link against to resolve symbols.
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../AppNotifications ${CMAKE_BINARY_DIR}/AppNotifications)
    endif()

    if(PLUGIN_APPACTIONS)
        # AppActions/CMakeLists.txt defines the implementation library target
        # which AppActionsL1Test must link against to resolve symbols.
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../AppActions ${CMAKE_BINARY_DIR}/AppActions)
    endif()
endif()

# -----------------------------------------------------------------------------
# Thunder interface selection
#
# The L1Tests build (and especially the mocks in Tests/mocks) must match the
# WPEFramework headers available via CMAKE_PREFIX_PATH / install/usr/include.
#
# In Thunder R4 the PluginHost::IShell interface changed (method signatures and
# return types), and ServiceMock has an #ifdef USE_THUNDER_R4 split accordingly.
# If USE_THUNDER_R4 is not propagated as a compile definition, compilation fails
# with "marked override but does not override".
# -----------------------------------------------------------------------------
option(USE_THUNDER_R4 "Build L1 tests against Thunder (WPEFramework) R4 headers" OFF)

# Keep an easy-to-consume variable for consistent use below.
set(_L1TESTS_USE_THUNDER_R4 ${USE_THUNDER_R4})

# ---------------------------------------------------------------------------
# L1Tests-only build support
#
# When configuring only Tests/L1Tests, consumers typically set:
#   -DCMAKE_PREFIX_PATH=<repo>/install/usr
#
# But this CMakeLists historically asked for "${NAMESPACE}Plugins", which becomes
# "Plugins" when NAMESPACE is empty, while the canonical Thunder package is
# WPEFrameworkPlugins. Bridge that gap for standalone L1Tests-only builds.
# ---------------------------------------------------------------------------
if(NOT DEFINED NAMESPACE)
    set(NAMESPACE "")
endif()

if("${NAMESPACE}" STREQUAL "")
    find_package(WPEFrameworkPlugins REQUIRED)
    find_package(WPEFrameworkDefinitions REQUIRED)

    if(NOT TARGET Plugins::Plugins AND TARGET WPEFrameworkPlugins::WPEFrameworkPlugins)
        add_library(Plugins::Plugins ALIAS WPEFrameworkPlugins::WPEFrameworkPlugins)
    endif()

    if(NOT TARGET Definitions::Definitions AND TARGET WPEFrameworkDefinitions::WPEFrameworkDefinitions)
        add_library(Definitions::Definitions ALIAS WPEFrameworkDefinitions::WPEFrameworkDefinitions)
    endif()
else()
    find_package(${NAMESPACE}Plugins REQUIRED)
    find_package(${NAMESPACE}Definitions REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Shared-object (legacy) test module sources
#   NOTE: Per request, keep this limited to test_UtilsFile.cpp for now.
# -----------------------------------------------------------------------------
#set(TEST_SRC
#    tests/test_UtilsFile.cpp
#)

set(TEST_LIB
    ${NAMESPACE}Plugins::${NAMESPACE}Plugins
)

set(TEST_INC ../../helpers)

# -----------------------------------------------------------------------------
# AppGateway-specific L1 tests (executable)
# -----------------------------------------------------------------------------
set(APPGATEWAY_L1TEST_SRC "")
set(APPGATEWAY_L1TEST_INC "")
set(APPGATEWAY_L1TEST_LIB "")

set(APPGATEWAYCOMMON_L1TEST_SRC "")
set(APPGATEWAYCOMMON_L1TEST_INC "")
set(APPGATEWAYCOMMON_L1TEST_LIB "")

set(APPNOTIFICATIONS_L1TEST_SRC "")
set(APPNOTIFICATIONS_L1TEST_INC "")
set(APPNOTIFICATIONS_L1TEST_LIB "")

if(PLUGIN_APPGATEWAY)
    message(STATUS "AppGateway L1 tests: ENABLED (PLUGIN_APPGATEWAY=ON)")

    list(APPEND APPGATEWAY_L1TEST_SRC tests/test_AppGateway.cpp)

    list(APPEND APPGATEWAY_L1TEST_INC
        ../../helpers
        ../../AppGateway
        ../../AppGatewayCommon
    )

    list(APPEND APPGATEWAY_L1TEST_LIB
        ${APPGATEWAY_L1TEST_TARGET}
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    )
else()
    message(STATUS "AppGateway L1 tests: DISABLED (set -DPLUGIN_APPGATEWAY=ON to enable)")
endif()

if(PLUGIN_APPGATEWAYCOMMON)
    message(STATUS "AppGatewayCommon L1 tests: ENABLED (PLUGIN_APPGATEWAYCOMMON=ON)")

    list(APPEND APPGATEWAYCOMMON_L1TEST_SRC
        AppGatewayCommon/AppGatewayCommon_core_test.cpp
        AppGatewayCommon/AppGatewayCommon_appdelegate_test.cpp
        AppGatewayCommon/AppGatewayCommon_usersettings_test.cpp
        AppGatewayCommon/AppGatewayCommon_system_test.cpp
        AppGatewayCommon/AppGatewayCommon_network_test.cpp
        AppGatewayCommon/AppGatewayCommon_lifecycle_test.cpp
        AppGatewayCommon/AppGatewayCommon_events_test.cpp
        AppGatewayCommon/AppGatewayCommon_display_test.cpp
        AppGatewayCommon/AppGatewayCommon_branding_test.cpp
    )

    list(APPEND APPGATEWAYCOMMON_L1TEST_INC
        ../../helpers
        ../../AppGatewayCommon
    )

    list(APPEND APPGATEWAYCOMMON_L1TEST_LIB
        ${NAMESPACE}AppGatewayCommon
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    )
else()
    message(STATUS "AppGatewayCommon L1 tests: DISABLED (set -DPLUGIN_APPGATEWAYCOMMON=ON to enable)")
endif()

if(PLUGIN_APPNOTIFICATIONS)
    message(STATUS "AppNotifications L1 tests: ENABLED (PLUGIN_APPNOTIFICATIONS=ON)")

    list(APPEND APPNOTIFICATIONS_L1TEST_SRC tests/test_AppNotifications.cpp)

    list(APPEND APPNOTIFICATIONS_L1TEST_INC
        ../../helpers
        ../../AppNotifications
    )

    list(APPEND APPNOTIFICATIONS_L1TEST_LIB
        ${NAMESPACE}AppNotifications
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    )
else()
    message(STATUS "AppNotifications L1 tests: DISABLED (set -DPLUGIN_APPNOTIFICATIONS=ON to enable)")
endif()

set(APPACTIONS_L1TEST_SRC "")
set(APPACTIONS_L1TEST_INC "")
set(APPACTIONS_L1TEST_LIB "")

if(PLUGIN_APPACTIONS)
    message(STATUS "AppActions L1 tests: ENABLED (PLUGIN_APPACTIONS=ON)")

    list(APPEND APPACTIONS_L1TEST_SRC AppActions/AppActions_test.cpp)

    list(APPEND APPACTIONS_L1TEST_INC
        ../../helpers
        ../../AppActions
    )

    list(APPEND APPACTIONS_L1TEST_LIB
        ${NAMESPACE}AppActions
        ${NAMESPACE}AppActionsImplementation
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    )
else()
    message(STATUS "AppActions L1 tests: DISABLED (set -DPLUGIN_APPACTIONS=ON to enable)")
endif()

#########################################################################################
# add_plugin_test_ex: Macro to add plugin tests, it will append to TEST_SRC, TEST_INC,
#                     and TEST_LIB. Args are positional.
#                     arg1: test file list as **string**
#                     arg2: include dir list as **string**
#                     arg3: plugin libs to link list as **string**
#
# sample invocation :
#    add_plugin_test_ex(PLUGIN_NAME
#                       "test/file1.cpp;test/file2.cpp"
#                       "../../inc_dir1;../../inc_dir2"
#                       "${NAMESPACE}PluginName;${NAMESPACE}PluginNameImplementation")
#########################################################################################
macro(add_plugin_test_ex plugin_opt plugin_test_sources_str plugin_includes_str plugin_libs_str)
    if(${plugin_opt})
        message(STATUS "${plugin_opt}=ON")

        string(REPLACE ";" ";" srclist "${plugin_test_sources_str}")
        string(REPLACE ";" ";" inclist "${plugin_includes_str}")
        string(REPLACE ";" ";" liblist "${plugin_libs_str}")

        foreach(item IN LISTS srclist)
            list(APPEND TEST_SRC ${item})
        endforeach()

        foreach(item IN LISTS inclist)
            list(APPEND TEST_INC ${item})
        endforeach()

        foreach(item IN LISTS liblist)
            list(APPEND TEST_LIB ${item})
        endforeach()
    else()
        message(STATUS "${plugin_opt}=OFF")
    endif()
endmacro()

macro(add_plugin_test plugin_name test_files)
    string(TOUPPER "${plugin_name}" plugin_option)
    set(plugin_opt "PLUGIN_${plugin_option}")

    add_plugin_test_ex(${plugin_opt} "${test_files}" "../../${plugin_name}" "${NAMESPACE}${plugin_name}")
endmacro()

# -----------------------------------------------------------------------------
# Legacy shared object
#
# In "L1Tests-only" configurations we may intentionally have no legacy test
# sources enabled (TEST_SRC empty). CMake errors if we create a library target
# without sources ("No SOURCES given to target"). Guard it so AppGatewayL1Test
# can still be built and coverage can run.
# -----------------------------------------------------------------------------
if(TEST_SRC)
    add_library(${MODULE_NAME} SHARED ${TEST_SRC})

    if(RDK_SERVICES_L1_TEST)
        target_link_options(${MODULE_NAME} PRIVATE "-Wl,-wrap,opendir")
    endif(RDK_SERVICES_L1_TEST)

    target_link_directories(${MODULE_NAME} PUBLIC
        ${CMAKE_INSTALL_PREFIX}/lib
        ${CMAKE_INSTALL_PREFIX}/lib/wpeframework/plugins
    )

    target_link_libraries(${MODULE_NAME} ${TEST_LIB})

    target_include_directories(${MODULE_NAME}
        PUBLIC
        ${TEST_INC}
        ${CMAKE_INSTALL_PREFIX}/include
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
    )

    target_compile_definitions(${MODULE_NAME} PRIVATE
        UNIT_TEST
        DISABLE_SECURITY_TOKEN
    )

    if(_L1TESTS_USE_THUNDER_R4)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_THUNDER_R4)
    endif()

    install(TARGETS ${MODULE_NAME} DESTINATION lib)
else()
    message(STATUS "Skipping ${MODULE_NAME} (no legacy L1 test sources enabled)")
endif()

# Always write the L1Tests plugin config JSON used by the test runtime.
write_config(${PLUGIN_NAME})

# -----------------------------------------------------------------------------
# AppGatewayL1Test executable (required by the workflow/script)
# -----------------------------------------------------------------------------
if(PLUGIN_APPGATEWAY OR PLUGIN_APPGATEWAYCOMMON OR PLUGIN_APPNOTIFICATIONS OR PLUGIN_APPACTIONS)
    find_package(Threads REQUIRED)

    # Prefer CONFIG mode so imported targets (GTest::gtest, GTest::gmock, etc.) are defined.
    # In some environments CMake's module-mode FindGTest is picked up first and only sets
    # variables like GTEST_LIBRARIES (leading to "Found GTest: <path>/libgtest.a" but
    # missing GTest:: targets). L1Tests links using imported targets, so we must ensure
    # they exist.
    find_package(GTest REQUIRED CONFIG)

    # Fallback compatibility: if we still don't have the imported targets (e.g. if a toolchain
    # injects a FindGTest-only environment), create them from the legacy variables.
    if(NOT TARGET GTest::gtest)
        find_package(GTest REQUIRED) # may populate GTEST_LIBRARIES / GTEST_INCLUDE_DIRS

        if(GTEST_LIBRARIES)
            add_library(GTest::gtest INTERFACE IMPORTED)
            set_target_properties(GTest::gtest PROPERTIES
                INTERFACE_LINK_LIBRARIES "${GTEST_LIBRARIES}"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIRS}"
            )
        endif()

        if(GTEST_MAIN_LIBRARIES)
            add_library(GTest::gtest_main INTERFACE IMPORTED)
            set_target_properties(GTest::gtest_main PROPERTIES
                INTERFACE_LINK_LIBRARIES "${GTEST_MAIN_LIBRARIES};GTest::gtest"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIRS}"
            )
        endif()

        if(GMOCK_LIBRARIES)
            add_library(GTest::gmock INTERFACE IMPORTED)
            set_target_properties(GTest::gmock PROPERTIES
                INTERFACE_LINK_LIBRARIES "${GMOCK_LIBRARIES};GTest::gtest"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIRS}"
            )
        endif()

        if(GMOCK_MAIN_LIBRARIES)
            add_library(GTest::gmock_main INTERFACE IMPORTED)
            set_target_properties(GTest::gmock_main PROPERTIES
                INTERFACE_LINK_LIBRARIES "${GMOCK_MAIN_LIBRARIES};GTest::gmock"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIRS}"
            )
        endif()

        if(NOT TARGET GTest::gtest OR NOT TARGET GTest::gmock OR NOT TARGET GTest::gtest_main)
            message(FATAL_ERROR
                "GTest imported targets not available. "
                "Ensure GTestConfig.cmake is discoverable via CMAKE_PREFIX_PATH "
                "(e.g. -DCMAKE_PREFIX_PATH=<repo>/install/usr)."
            )
        endif()
    endif()

    if(PLUGIN_APPGATEWAY)
        add_executable(AppGatewayL1Test ${APPGATEWAY_L1TEST_SRC})

        target_include_directories(AppGatewayL1Test
            PRIVATE
            ${APPGATEWAY_L1TEST_INC}

        ${CMAKE_INSTALL_PREFIX}/include

        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks

            ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
        )

        target_link_libraries(AppGatewayL1Test PRIVATE
            ${APPGATEWAY_L1TEST_LIB}
            GTest::gmock
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )

        target_compile_definitions(AppGatewayL1Test PRIVATE
            UNIT_TEST
            DISABLE_SECURITY_TOKEN
        )

        if(_L1TESTS_USE_THUNDER_R4)
            target_compile_definitions(AppGatewayL1Test PRIVATE USE_THUNDER_R4)
        endif()

        install(TARGETS AppGatewayL1Test RUNTIME DESTINATION bin)
    endif()

    if(PLUGIN_APPGATEWAYCOMMON)
        add_executable(AppGatewayCommonL1Test ${APPGATEWAYCOMMON_L1TEST_SRC})

        target_include_directories(AppGatewayCommonL1Test
            PRIVATE
            ${APPGATEWAYCOMMON_L1TEST_INC}

            ${CMAKE_INSTALL_PREFIX}/include

            ${CMAKE_CURRENT_SOURCE_DIR}/../mocks

            ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
        )

        target_link_libraries(AppGatewayCommonL1Test PRIVATE
            ${APPGATEWAYCOMMON_L1TEST_LIB}
            GTest::gmock
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )

        target_compile_definitions(AppGatewayCommonL1Test PRIVATE
            UNIT_TEST
            DISABLE_SECURITY_TOKEN
            MODULE_NAME=AppGatewayCommonL1Test
        )

        if(_L1TESTS_USE_THUNDER_R4)
            target_compile_definitions(AppGatewayCommonL1Test PRIVATE USE_THUNDER_R4)
        endif()

        install(TARGETS AppGatewayCommonL1Test RUNTIME DESTINATION bin)

        # Precompiled headers: cache expensive gtest/gmock and Thunder headers
        # to reduce per-TU parse time (requires CMake >= 3.16).
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
            target_precompile_headers(AppGatewayCommonL1Test PRIVATE
                <gmock/gmock.h>
                <gtest/gtest.h>
                <core/core.h>
                <plugins/plugins.h>
                <string>
                <map>
                <mutex>
                <memory>
                <functional>
                <unordered_map>
            )
        endif()
    endif()

    if(PLUGIN_APPNOTIFICATIONS)
        add_executable(AppNotificationsL1Test ${APPNOTIFICATIONS_L1TEST_SRC})

        target_include_directories(AppNotificationsL1Test
            PRIVATE
            ${APPNOTIFICATIONS_L1TEST_INC}

            ${CMAKE_INSTALL_PREFIX}/include

            ${CMAKE_SOURCE_DIR}/Tests/mocks
            ${CMAKE_SOURCE_DIR}/../mocks

            ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
        )

        target_link_libraries(AppNotificationsL1Test PRIVATE
            ${APPNOTIFICATIONS_L1TEST_LIB}
            GTest::gmock
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )

        target_compile_definitions(AppNotificationsL1Test PRIVATE
            UNIT_TEST
            DISABLE_SECURITY_TOKEN
        )

        if(_L1TESTS_USE_THUNDER_R4)
            target_compile_definitions(AppNotificationsL1Test PRIVATE USE_THUNDER_R4)
        endif()

        install(TARGETS AppNotificationsL1Test RUNTIME DESTINATION bin)
    endif()

    if(PLUGIN_APPACTIONS)
        add_executable(AppActionsL1Test ${APPACTIONS_L1TEST_SRC})

        target_include_directories(AppActionsL1Test
            PRIVATE
            ${APPACTIONS_L1TEST_INC}

            ${CMAKE_INSTALL_PREFIX}/include

            ${CMAKE_SOURCE_DIR}/Tests/mocks
            ${CMAKE_SOURCE_DIR}/../mocks

            ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
        )

        target_link_libraries(AppActionsL1Test PRIVATE
            ${APPACTIONS_L1TEST_LIB}
            GTest::gmock
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )

        target_compile_definitions(AppActionsL1Test PRIVATE
            UNIT_TEST
            DISABLE_SECURITY_TOKEN
            MODULE_NAME=AppActionsL1Test
            APPACTIONS_MAJOR_VERSION=1
            APPACTIONS_MINOR_VERSION=0
            APPACTIONS_PATCH_VERSION=0
        )

        if(_L1TESTS_USE_THUNDER_R4)
            target_compile_definitions(AppActionsL1Test PRIVATE USE_THUNDER_R4)
        endif()

        install(TARGETS AppActionsL1Test RUNTIME DESTINATION bin)
    endif()
endif()
