cmake_minimum_required(VERSION 3.16)

project(appgateway_l0test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(APPGW_TEST_ENABLE_WARNINGS "Enable additional compiler warnings for appgateway_l0test" ON)
option(APPGW_ENABLE_COVERAGE "Enable coverage flags for appgateway_l0test" ON)
option(APPNOTIF_ENABLE_COVERAGE "Enable coverage flags for appnotifications_l0test" ON)
option(APPACTIONS_ENABLE_COVERAGE "Enable coverage flags for appactions_l0test" ON)
option(APPGW_L0_ENABLE_APPGATEWAY "Build AppGateway L0 test target" ON)
option(APPGW_L0_ENABLE_APPGATEWAYCOMMON "Build AppGatewayCommon L0 test target" ON)
option(APPGW_L0_ENABLE_APPNOTIFICATIONS "Build AppNotifications L0 test target" ON)
option(APPGW_L0_ENABLE_APPACTIONS "Build AppActions L0 test target" ON)

if(NOT DEFINED PREFIX OR "${PREFIX}" STREQUAL "")
    if(DEFINED ENV{PREFIX} AND NOT "$ENV{PREFIX}" STREQUAL "")
        set(PREFIX "$ENV{PREFIX}")
    else()
        set(PREFIX "${CMAKE_SOURCE_DIR}/../../install/usr")
    endif()
endif()
set(PREFIX "${PREFIX}" CACHE STRING "Install prefix to locate Thunder/WPEFramework artifacts (include/lib/plugins)" FORCE)

message(STATUS "Using PREFIX='${PREFIX}'")

# ---------------------------------------------------------------------------
# Source lists
# ---------------------------------------------------------------------------
set(APPGW_APPGATEWAY_L0_SOURCES
    ${CMAKE_SOURCE_DIR}/../../AppGateway/Module.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGateway/AppGateway.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGateway/Resolver.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGateway/AppGatewayImplementation.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGateway/AppGatewayResponderImplementation.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGateway/AppGatewayTelemetry.cpp
    AppGateway/AppGatewayTest.cpp
    AppGateway/AppGateway_Init_DeinitTests.cpp
    AppGateway/AppGateway_JsonRpcResolveTests.cpp
    AppGateway/Resolver_Configure_And_ResolveTests.cpp
    AppGateway/Responder_BehaviorTests.cpp
    AppGateway/AppGatewayResponderImplementation_Tests.cpp
    AppGateway/Responder_HibernationTests.cpp
    AppGateway/ContextUtils_ConversionTests.cpp
    AppGateway/AppGatewayImplementation_BranchTests.cpp
    AppGateway/AppGatewayTelemetry_Tests.cpp
    AppGateway/AppGatewayTelemetry_DirectAccess_Tests.cpp
    common/L0Bootstrap.cpp
)
set(APPNOTIF_L0_SOURCES
    ${CMAKE_SOURCE_DIR}/../../AppNotifications/Module.cpp
    ${CMAKE_SOURCE_DIR}/../../AppNotifications/AppNotifications.cpp
    AppNotifications/AppNotificationsTestHelpers.cpp
    AppNotifications/AppNotificationsTest.cpp
    AppNotifications/AppNotifications_Init_DeinitTests.cpp
    AppNotifications/AppNotifications_SubscribeTests.cpp
    AppNotifications/AppNotifications_EmitTests.cpp
    AppNotifications/AppNotifications_SubscriberMapTests.cpp
    AppNotifications/AppNotifications_ThunderManagerTests.cpp
    AppNotifications/AppNotifications_ContextEqualityTests.cpp
    AppNotifications/AppNotifications_BoundaryTests.cpp
    common/L0Bootstrap.cpp
)

set(APPACTIONS_L0_SOURCES
    ${CMAKE_SOURCE_DIR}/../../AppActions/Module.cpp
    ${CMAKE_SOURCE_DIR}/../../AppActions/AppActions.cpp
    ${CMAKE_SOURCE_DIR}/../../AppActions/AppActionsImplementation.cpp
    AppActions/AppActionsTestHelpers.cpp
    AppActions/AppActionsTest.cpp
    AppActions/AppActions_Init_DeinitTests.cpp
    AppActions/AppActions_ActionStartTests.cpp
    AppActions/AppActions_NotificationTests.cpp
    AppActions/AppActions_ImplTests.cpp
    common/L0Bootstrap.cpp
)

# ---------------------------------------------------------------------------
# Declare targets
# ---------------------------------------------------------------------------
set(APPGW_L0_PRIMARY_TARGET "")

if(APPGW_L0_ENABLE_APPGATEWAY)
    add_executable(appgateway_l0test ${APPGW_APPGATEWAY_L0_SOURCES})
    set(APPGW_L0_PRIMARY_TARGET appgateway_l0test)
else()
    message(STATUS "APPGW_L0_ENABLE_APPGATEWAY=OFF; skipping appgateway_l0test target")
endif()

if(NOT APPGW_L0_ENABLE_APPGATEWAYCOMMON)
    message(STATUS "APPGW_L0_ENABLE_APPGATEWAYCOMMON=OFF; skipping appgatewaycommon_l0test target")
endif()

if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
    add_executable(appnotifications_l0test ${APPNOTIF_L0_SOURCES})
endif()

if(APPGW_L0_ENABLE_APPACTIONS)
    add_executable(appactions_l0test ${APPACTIONS_L0_SOURCES})
endif()

if(APPGW_L0_PRIMARY_TARGET STREQUAL "" AND NOT APPGW_L0_ENABLE_APPNOTIFICATIONS AND NOT APPGW_L0_ENABLE_APPACTIONS)
    message(FATAL_ERROR "No L0 target enabled. Enable at least one APPGW_L0_ENABLE_* option.")
endif()

# ---------------------------------------------------------------------------
# AppGateway target compile options / definitions / includes
# ---------------------------------------------------------------------------
if(APPGW_L0_ENABLE_APPGATEWAY)
    target_compile_definitions(appgateway_l0test PRIVATE
        MODULE_NAME=AppGateway_L0Test
        APPGATEWAY_MAJOR_VERSION=1
        APPGATEWAY_MINOR_VERSION=0
        APPGATEWAY_PATCH_VERSION=0
        ENABLE_DEBUG_FOR_CONNECTION="enableDebugForConnection"
        DISABLE_DEBUG_FOR_CONNECTION="disableDebugForConnection"
    )

    if(APPGW_ENABLE_COVERAGE)
        target_compile_options(appgateway_l0test PRIVATE -O0 -g --coverage)
        target_link_options(appgateway_l0test PRIVATE --coverage)
    endif()

    if(APPGW_TEST_ENABLE_WARNINGS)
        target_compile_options(appgateway_l0test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    target_include_directories(appgateway_l0test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/AppGateway
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_SOURCE_DIR}/../..
        ${CMAKE_SOURCE_DIR}/../../AppGateway
        ${CMAKE_SOURCE_DIR}/../../helpers
    )

    if(EXISTS "${PREFIX}/lib")
        target_link_directories(appgateway_l0test PRIVATE "${PREFIX}/lib")
    endif()
    if(EXISTS "${PREFIX}/lib/plugins")
        target_link_directories(appgateway_l0test PRIVATE "${PREFIX}/lib/plugins")
    endif()
endif()

# ---------------------------------------------------------------------------
# AppNotifications target compile options / definitions / includes
# ---------------------------------------------------------------------------
if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
    target_compile_definitions(appnotifications_l0test PRIVATE
        MODULE_NAME=AppNotifications_L0Test
        APPNOTIFICATIONS_MAJOR_VERSION=1
        APPNOTIFICATIONS_MINOR_VERSION=0
        APPNOTIFICATIONS_PATCH_VERSION=0
        DISABLE_SECURITY_TOKEN=1
        USE_THUNDER_R4=1
    )

    if(APPNOTIF_ENABLE_COVERAGE)
        target_compile_options(appnotifications_l0test PRIVATE -O0 -g --coverage)
        target_link_options(appnotifications_l0test PRIVATE --coverage)
    endif()

    if(APPGW_TEST_ENABLE_WARNINGS)
        target_compile_options(appnotifications_l0test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    target_include_directories(appnotifications_l0test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/AppNotifications
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_SOURCE_DIR}/../..
        ${CMAKE_SOURCE_DIR}/../../AppNotifications
        ${CMAKE_SOURCE_DIR}/../../helpers
    )

    if(EXISTS "${PREFIX}/lib")
        target_link_directories(appnotifications_l0test PRIVATE "${PREFIX}/lib")
    endif()
    if(EXISTS "${PREFIX}/lib/plugins")
        target_link_directories(appnotifications_l0test PRIVATE "${PREFIX}/lib/plugins")
    endif()
endif()

# ---------------------------------------------------------------------------
# AppActions target compile options / definitions / includes
# ---------------------------------------------------------------------------
if(APPGW_L0_ENABLE_APPACTIONS)
    target_compile_definitions(appactions_l0test PRIVATE
        MODULE_NAME=AppActions_L0Test
        APPACTIONS_MAJOR_VERSION=1
        APPACTIONS_MINOR_VERSION=0
        APPACTIONS_PATCH_VERSION=0
        DISABLE_SECURITY_TOKEN=1
        USE_THUNDER_R4=1
    )

    if(APPACTIONS_ENABLE_COVERAGE)
        target_compile_options(appactions_l0test PRIVATE -O0 -g --coverage)
        target_link_options(appactions_l0test PRIVATE --coverage)
    endif()

    if(APPGW_TEST_ENABLE_WARNINGS)
        target_compile_options(appactions_l0test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    target_include_directories(appactions_l0test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/AppActions
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_SOURCE_DIR}/../..
        ${CMAKE_SOURCE_DIR}/../../AppActions
        ${CMAKE_SOURCE_DIR}/../../helpers
    )

    if(EXISTS "${PREFIX}/lib")
        target_link_directories(appactions_l0test PRIVATE "${PREFIX}/lib")
    endif()
    if(EXISTS "${PREFIX}/lib/plugins")
        target_link_directories(appactions_l0test PRIVATE "${PREFIX}/lib/plugins")
    endif()
endif()

# ---------------------------------------------------------------------------
# Thunder / WPEFramework library discovery (shared for both targets)
# ---------------------------------------------------------------------------
# Prefer namespace-based package targets (repo convention), fallback to direct
# libs for standalone CI layouts.
set(APPGW_NAMESPACE "")
if(DEFINED NAMESPACE AND NOT "${NAMESPACE}" STREQUAL "")
    set(APPGW_NAMESPACE "${NAMESPACE}")
elseif(DEFINED ENV{NAMESPACE} AND NOT "$ENV{NAMESPACE}" STREQUAL "")
    set(APPGW_NAMESPACE "$ENV{NAMESPACE}")
endif()

set(APPGW_HAVE_CORE_TARGET FALSE)
set(APPGW_HAVE_MESSAGING_TARGET FALSE)
set(APPGW_HAVE_PLUGINS_TARGET FALSE)
set(APPGW_HAVE_WEBSOCKET_TARGET FALSE)

if(NOT "${APPGW_NAMESPACE}" STREQUAL "")
    find_package(${APPGW_NAMESPACE}Core QUIET)
    find_package(${APPGW_NAMESPACE}Messaging QUIET)
    find_package(${APPGW_NAMESPACE}Plugins QUIET)
    find_package(${APPGW_NAMESPACE}WebSocket QUIET)

    # Add namespace-derived link directories so both WPEFramework and Thunder
    # plugin layouts are searched without hardcoding the framework name.
    string(TOLOWER "${APPGW_NAMESPACE}" _appgw_ns_lower)
    if(APPGW_L0_ENABLE_APPGATEWAY)
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
            target_link_directories(appgateway_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
        endif()
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}")
            target_link_directories(appgateway_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}")
        endif()
    endif()
    if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
            target_link_directories(appnotifications_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
        endif()
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}")
            target_link_directories(appnotifications_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}")
        endif()
    endif()
    if(APPGW_L0_ENABLE_APPACTIONS)
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
            target_link_directories(appactions_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}/plugins")
        endif()
        if(EXISTS "${PREFIX}/lib/${_appgw_ns_lower}")
            target_link_directories(appactions_l0test PRIVATE "${PREFIX}/lib/${_appgw_ns_lower}")
        endif()
    endif()
    unset(_appgw_ns_lower)

    if(TARGET ${APPGW_NAMESPACE}Core::${APPGW_NAMESPACE}Core)
        if(APPGW_L0_ENABLE_APPGATEWAY)
            target_link_libraries(appgateway_l0test PRIVATE ${APPGW_NAMESPACE}Core::${APPGW_NAMESPACE}Core)
        endif()
        if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
            target_link_libraries(appnotifications_l0test PRIVATE ${APPGW_NAMESPACE}Core::${APPGW_NAMESPACE}Core)
        endif()
        if(APPGW_L0_ENABLE_APPACTIONS)
            target_link_libraries(appactions_l0test PRIVATE ${APPGW_NAMESPACE}Core::${APPGW_NAMESPACE}Core)
        endif()
        set(APPGW_HAVE_CORE_TARGET TRUE)
    endif()
    if(TARGET ${APPGW_NAMESPACE}Messaging::${APPGW_NAMESPACE}Messaging)
        if(APPGW_L0_ENABLE_APPGATEWAY)
            target_link_libraries(appgateway_l0test PRIVATE ${APPGW_NAMESPACE}Messaging::${APPGW_NAMESPACE}Messaging)
        endif()
        if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
            target_link_libraries(appnotifications_l0test PRIVATE ${APPGW_NAMESPACE}Messaging::${APPGW_NAMESPACE}Messaging)
        endif()
        if(APPGW_L0_ENABLE_APPACTIONS)
            target_link_libraries(appactions_l0test PRIVATE ${APPGW_NAMESPACE}Messaging::${APPGW_NAMESPACE}Messaging)
        endif()
        set(APPGW_HAVE_MESSAGING_TARGET TRUE)
    endif()
    if(TARGET ${APPGW_NAMESPACE}Plugins::${APPGW_NAMESPACE}Plugins)
        if(APPGW_L0_ENABLE_APPGATEWAY)
            target_link_libraries(appgateway_l0test PRIVATE ${APPGW_NAMESPACE}Plugins::${APPGW_NAMESPACE}Plugins)
        endif()
        if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
            target_link_libraries(appnotifications_l0test PRIVATE ${APPGW_NAMESPACE}Plugins::${APPGW_NAMESPACE}Plugins)
        endif()
        if(APPGW_L0_ENABLE_APPACTIONS)
            target_link_libraries(appactions_l0test PRIVATE ${APPGW_NAMESPACE}Plugins::${APPGW_NAMESPACE}Plugins)
        endif()
        set(APPGW_HAVE_PLUGINS_TARGET TRUE)
    endif()
    if(TARGET ${APPGW_NAMESPACE}WebSocket::${APPGW_NAMESPACE}WebSocket)
        if(APPGW_L0_ENABLE_APPGATEWAY)
            target_link_libraries(appgateway_l0test PRIVATE ${APPGW_NAMESPACE}WebSocket::${APPGW_NAMESPACE}WebSocket)
        endif()
        if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
            target_link_libraries(appnotifications_l0test PRIVATE ${APPGW_NAMESPACE}WebSocket::${APPGW_NAMESPACE}WebSocket)
        endif()
        if(APPGW_L0_ENABLE_APPACTIONS)
            target_link_libraries(appactions_l0test PRIVATE ${APPGW_NAMESPACE}WebSocket::${APPGW_NAMESPACE}WebSocket)
        endif()
        set(APPGW_HAVE_WEBSOCKET_TARGET TRUE)
    endif()
endif()

# Fallback include path discovery for environments without namespace package configs.
if(NOT (APPGW_HAVE_CORE_TARGET AND APPGW_HAVE_MESSAGING_TARGET AND APPGW_HAVE_PLUGINS_TARGET AND APPGW_HAVE_WEBSOCKET_TARGET))
    if(APPGW_L0_ENABLE_APPGATEWAY)
        target_include_directories(appgateway_l0test PRIVATE
            ${PREFIX}/include
            ${PREFIX}/include/WPEFramework
        )
    endif()
    if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
        target_include_directories(appnotifications_l0test PRIVATE
            ${PREFIX}/include
            ${PREFIX}/include/WPEFramework
        )
    endif()
    if(APPGW_L0_ENABLE_APPACTIONS)
        target_include_directories(appactions_l0test PRIVATE
            ${PREFIX}/include
            ${PREFIX}/include/WPEFramework
        )
    endif()

    if(CMAKE_PREFIX_PATH)
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            if(EXISTS "${_prefix}/include")
                if(APPGW_L0_ENABLE_APPGATEWAY)
                    target_include_directories(appgateway_l0test PRIVATE "${_prefix}/include")
                endif()
                if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
                    target_include_directories(appnotifications_l0test PRIVATE "${_prefix}/include")
                endif()
                if(APPGW_L0_ENABLE_APPACTIONS)
                    target_include_directories(appactions_l0test PRIVATE "${_prefix}/include")
                endif()
            endif()
            if(EXISTS "${_prefix}/include/WPEFramework")
                if(APPGW_L0_ENABLE_APPGATEWAY)
                    target_include_directories(appgateway_l0test PRIVATE "${_prefix}/include/WPEFramework")
                endif()
                if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
                    target_include_directories(appnotifications_l0test PRIVATE "${_prefix}/include/WPEFramework")
                endif()
                if(APPGW_L0_ENABLE_APPACTIONS)
                    target_include_directories(appactions_l0test PRIVATE "${_prefix}/include/WPEFramework")
                endif()
            endif()
        endforeach()
    endif()
endif()

if(NOT APPGW_HAVE_CORE_TARGET)
    find_library(WPEFRAMEWORK_CORE_LIB NAMES WPEFrameworkCore ThunderCore HINTS "${PREFIX}/lib")
endif()
if(NOT APPGW_HAVE_MESSAGING_TARGET)
    find_library(WPEFRAMEWORK_MESSAGING_LIB NAMES WPEFrameworkMessaging ThunderMessaging HINTS "${PREFIX}/lib")
endif()
if(NOT APPGW_HAVE_PLUGINS_TARGET)
    find_library(WPEFRAMEWORK_PLUGINS_LIB NAMES WPEFrameworkPlugins ThunderPlugins HINTS "${PREFIX}/lib")
endif()
if(NOT APPGW_HAVE_WEBSOCKET_TARGET)
    find_library(WPEFRAMEWORK_WEBSOCKET_LIB NAMES WPEFrameworkWebSocket ThunderWebSocket HINTS "${PREFIX}/lib")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(APPGW_L0_ENABLE_APPGATEWAY)
    if(DEFINED WPEFRAMEWORK_CORE_LIB AND WPEFRAMEWORK_CORE_LIB)
        target_link_libraries(appgateway_l0test PRIVATE ${WPEFRAMEWORK_CORE_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_MESSAGING_LIB AND WPEFRAMEWORK_MESSAGING_LIB)
        target_link_libraries(appgateway_l0test PRIVATE ${WPEFRAMEWORK_MESSAGING_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_PLUGINS_LIB AND WPEFRAMEWORK_PLUGINS_LIB)
        target_link_libraries(appgateway_l0test PRIVATE ${WPEFRAMEWORK_PLUGINS_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_WEBSOCKET_LIB AND WPEFRAMEWORK_WEBSOCKET_LIB)
        target_link_libraries(appgateway_l0test PRIVATE ${WPEFRAMEWORK_WEBSOCKET_LIB})
    endif()

    target_link_libraries(appgateway_l0test PRIVATE
        Threads::Threads
        m
        dl
    )
endif()

if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
    if(DEFINED WPEFRAMEWORK_CORE_LIB AND WPEFRAMEWORK_CORE_LIB)
        target_link_libraries(appnotifications_l0test PRIVATE ${WPEFRAMEWORK_CORE_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_MESSAGING_LIB AND WPEFRAMEWORK_MESSAGING_LIB)
        target_link_libraries(appnotifications_l0test PRIVATE ${WPEFRAMEWORK_MESSAGING_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_PLUGINS_LIB AND WPEFRAMEWORK_PLUGINS_LIB)
        target_link_libraries(appnotifications_l0test PRIVATE ${WPEFRAMEWORK_PLUGINS_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_WEBSOCKET_LIB AND WPEFRAMEWORK_WEBSOCKET_LIB)
        target_link_libraries(appnotifications_l0test PRIVATE ${WPEFRAMEWORK_WEBSOCKET_LIB})
    endif()

    target_link_libraries(appnotifications_l0test PRIVATE
        Threads::Threads
        m
        dl
    )
endif()

if(APPGW_L0_ENABLE_APPACTIONS)
    if(DEFINED WPEFRAMEWORK_CORE_LIB AND WPEFRAMEWORK_CORE_LIB)
        target_link_libraries(appactions_l0test PRIVATE ${WPEFRAMEWORK_CORE_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_MESSAGING_LIB AND WPEFRAMEWORK_MESSAGING_LIB)
        target_link_libraries(appactions_l0test PRIVATE ${WPEFRAMEWORK_MESSAGING_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_PLUGINS_LIB AND WPEFRAMEWORK_PLUGINS_LIB)
        target_link_libraries(appactions_l0test PRIVATE ${WPEFRAMEWORK_PLUGINS_LIB})
    endif()
    if(DEFINED WPEFRAMEWORK_WEBSOCKET_LIB AND WPEFRAMEWORK_WEBSOCKET_LIB)
        target_link_libraries(appactions_l0test PRIVATE ${WPEFRAMEWORK_WEBSOCKET_LIB})
    endif()

    target_link_libraries(appactions_l0test PRIVATE
        Threads::Threads
        m
        dl
        uuid
    )
endif()

# ---------------------------------------------------------------------------
# Runtime search paths (shared rpath dirs, applied to both targets)
# ---------------------------------------------------------------------------
set(APPGW_RPATH_DIRS
    "${PREFIX}/lib"
    "${PREFIX}/lib/plugins"
    "${PREFIX}/lib/wpeframework"
    "${PREFIX}/lib/wpeframework/plugins"
    "${PREFIX}/lib/thunder"
    "${PREFIX}/lib/thunder/plugins"
)

if(NOT "${APPGW_NAMESPACE}" STREQUAL "")
    string(TOLOWER "${APPGW_NAMESPACE}" APPGW_STORAGE_DIRECTORY)
    list(APPEND APPGW_RPATH_DIRS
        "${PREFIX}/lib/${APPGW_STORAGE_DIRECTORY}"
        "${PREFIX}/lib/${APPGW_STORAGE_DIRECTORY}/plugins"
    )
endif()

list(REMOVE_DUPLICATES APPGW_RPATH_DIRS)

set(APPGW_BUILD_RPATH "$ORIGIN")
foreach(_rpath_dir IN LISTS APPGW_RPATH_DIRS)
    set(APPGW_BUILD_RPATH "${APPGW_BUILD_RPATH};${_rpath_dir}")
endforeach()

set(APPGW_INSTALL_RPATH "${APPGW_RPATH_DIRS}")

if(APPGW_L0_ENABLE_APPGATEWAY)
    set_target_properties(appgateway_l0test PROPERTIES
        BUILD_RPATH "${APPGW_BUILD_RPATH}"
        INSTALL_RPATH "${APPGW_INSTALL_RPATH}"
    )
endif()

if(APPGW_L0_ENABLE_APPNOTIFICATIONS)
    set_target_properties(appnotifications_l0test PROPERTIES
        BUILD_RPATH "${APPGW_BUILD_RPATH}"
        INSTALL_RPATH "${APPGW_INSTALL_RPATH}"
    )
endif()

if(APPGW_L0_ENABLE_APPACTIONS)
    set_target_properties(appactions_l0test PROPERTIES
        BUILD_RPATH "${APPGW_BUILD_RPATH}"
        INSTALL_RPATH "${APPGW_INSTALL_RPATH}"
    )
endif()

# ---------------------------------------------------------------------------
# Utility targets
# ---------------------------------------------------------------------------
if(APPGW_L0_ENABLE_APPGATEWAY)
    add_custom_target(run
        DEPENDS appgateway_l0test
        COMMAND $<TARGET_FILE:appgateway_l0test>
        COMMENT "Running appgateway_l0test"
        VERBATIM
    )
endif()

if(APPGW_L0_ENABLE_APPACTIONS)
    add_custom_target(run_appactions
        DEPENDS appactions_l0test
        COMMAND $<TARGET_FILE:appactions_l0test>
        COMMENT "Running appactions_l0test"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# AppGatewayCommon L0 test
# ---------------------------------------------------------------------------
if(APPGW_L0_ENABLE_APPGATEWAYCOMMON)

set(APPGWC_L0_SOURCES
    ${CMAKE_SOURCE_DIR}/../../AppGatewayCommon/Module.cpp
    ${CMAKE_SOURCE_DIR}/../../AppGatewayCommon/AppGatewayCommon.cpp
    AppGatewayCommon/AppGatewayCommon_lifecycle_test.cpp
    AppGatewayCommon/AppGatewayCommon_routing_test.cpp
    AppGatewayCommon/AppGatewayCommon_setters_test.cpp
    AppGatewayCommon/AppGatewayCommon_events_test.cpp
    AppGatewayCommon/AppGatewayCommon_display_test.cpp
    AppGatewayCommon/AppGatewayCommon_main_test.cpp
    common/L0Bootstrap.cpp
)

add_executable(appgatewaycommon_l0test ${APPGWC_L0_SOURCES})

target_compile_definitions(appgatewaycommon_l0test PRIVATE
    MODULE_NAME=AppGatewayCommon_L0Test
    APPGATEWAYCOMMON_MAJOR_VERSION=1
    APPGATEWAYCOMMON_MINOR_VERSION=0
    APPGATEWAYCOMMON_PATCH_VERSION=0
    ENABLE_DEBUG_FOR_CONNECTION="enableDebugForConnection"
    DISABLE_DEBUG_FOR_CONNECTION="disableDebugForConnection"
    DISABLE_SECURITY_TOKEN
    USE_THUNDER_R4
    SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS=100
)

if(APPGW_ENABLE_COVERAGE)
    target_compile_options(appgatewaycommon_l0test PRIVATE -O0 -g --coverage)
    target_link_options(appgatewaycommon_l0test PRIVATE --coverage)
endif()

if(APPGW_TEST_ENABLE_WARNINGS)
    target_compile_options(appgatewaycommon_l0test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_include_directories(appgatewaycommon_l0test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/AppGatewayCommon
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../..
    ${CMAKE_SOURCE_DIR}/../../AppGatewayCommon
    ${CMAKE_SOURCE_DIR}/../../helpers
    ${PREFIX}/include
    ${PREFIX}/include/WPEFramework
)

if(CMAKE_PREFIX_PATH)
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        if(EXISTS "${_prefix}/include")
            target_include_directories(appgatewaycommon_l0test PRIVATE "${_prefix}/include")
        endif()
        if(EXISTS "${_prefix}/include/WPEFramework")
            target_include_directories(appgatewaycommon_l0test PRIVATE "${_prefix}/include/WPEFramework")
        endif()
    endforeach()
endif()

if(EXISTS "${PREFIX}/lib")
    target_link_directories(appgatewaycommon_l0test PRIVATE "${PREFIX}/lib")
endif()
if(EXISTS "${PREFIX}/lib/plugins")
    target_link_directories(appgatewaycommon_l0test PRIVATE "${PREFIX}/lib/plugins")
endif()
# Add namespace-derived link directories (same logic as appgateway/appnotifications targets).
foreach(_rdir IN LISTS APPGW_RPATH_DIRS)
    if(EXISTS "${_rdir}")
        target_link_directories(appgatewaycommon_l0test PRIVATE "${_rdir}")
    endif()
endforeach()

# Link Thunder/WPEFramework libraries: prefer namespace targets found earlier,
# fall back to find_library results for standalone CI layouts.
if(APPGW_HAVE_CORE_TARGET)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${APPGW_NAMESPACE}Core::${APPGW_NAMESPACE}Core)
elseif(WPEFRAMEWORK_CORE_LIB)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${WPEFRAMEWORK_CORE_LIB})
endif()
if(APPGW_HAVE_MESSAGING_TARGET)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${APPGW_NAMESPACE}Messaging::${APPGW_NAMESPACE}Messaging)
elseif(WPEFRAMEWORK_MESSAGING_LIB)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${WPEFRAMEWORK_MESSAGING_LIB})
endif()
if(APPGW_HAVE_PLUGINS_TARGET)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${APPGW_NAMESPACE}Plugins::${APPGW_NAMESPACE}Plugins)
elseif(WPEFRAMEWORK_PLUGINS_LIB)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${WPEFRAMEWORK_PLUGINS_LIB})
endif()
if(APPGW_HAVE_WEBSOCKET_TARGET)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${APPGW_NAMESPACE}WebSocket::${APPGW_NAMESPACE}WebSocket)
elseif(WPEFRAMEWORK_WEBSOCKET_LIB)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${WPEFRAMEWORK_WEBSOCKET_LIB})
endif()

find_library(UUID_LIB NAMES uuid)
if(NOT UUID_LIB)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(UUID QUIET uuid)
    endif()
endif()

target_link_libraries(appgatewaycommon_l0test PRIVATE
    Threads::Threads
    m
    dl
)

if(UUID_LIB)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${UUID_LIB})
elseif(UUID_FOUND)
    target_link_libraries(appgatewaycommon_l0test PRIVATE ${UUID_LINK_LIBRARIES})
    target_include_directories(appgatewaycommon_l0test PRIVATE ${UUID_INCLUDE_DIRS})
else()
    target_link_libraries(appgatewaycommon_l0test PRIVATE uuid)
endif()

set_target_properties(appgatewaycommon_l0test PROPERTIES
    BUILD_RPATH "${APPGW_BUILD_RPATH}"
    INSTALL_RPATH "${APPGW_INSTALL_RPATH}"
)

# Precompiled headers: cache expensive Thunder framework and STL headers
# to reduce per-TU parse time (requires CMake >= 3.16).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(appgatewaycommon_l0test PRIVATE
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

add_custom_target(run_appgatewaycommon
    DEPENDS appgatewaycommon_l0test
    COMMAND $<TARGET_FILE:appgatewaycommon_l0test>
    COMMENT "Running appgatewaycommon_l0test"
    VERBATIM
)

endif() # APPGW_L0_ENABLE_APPGATEWAYCOMMON

# ---------------------------------------------------------------------------
# Coverage target (collects data from all L0 test targets)
# ---------------------------------------------------------------------------

find_program(LCOV_EXECUTABLE lcov)
find_program(GENHTML_EXECUTABLE genhtml)

if(NOT LCOV_EXECUTABLE OR NOT GENHTML_EXECUTABLE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E echo "lcov/genhtml not found. Install lcov to generate coverage report."
        COMMENT "Coverage tools not found; skipping coverage data generation."
        VERBATIM
    )
else()
    add_custom_target(coverage
        COMMAND "${LCOV_EXECUTABLE}" -c -o coverage.info -d "${CMAKE_BINARY_DIR}"
        COMMAND "${GENHTML_EXECUTABLE}" -o coverage coverage.info
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/.."
        COMMENT "Generating coverage report with lcov/genhtml"
        VERBATIM
    )
endif()
