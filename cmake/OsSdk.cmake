function(jy_app_find_latest_os_sdk_cache OUT_VAR)
    set(_os_sdk_cache_root "${PROJECT_ROOT}/.os_sdk_cache")
    if(NOT IS_DIRECTORY "${_os_sdk_cache_root}")
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB _os_sdk_cache_candidates
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${_os_sdk_cache_root}/*")

    set(_valid_os_sdk_cache_entries)
    foreach(_os_sdk_cache_candidate IN LISTS _os_sdk_cache_candidates)
        if(IS_DIRECTORY "${_os_sdk_cache_candidate}" AND
                IS_DIRECTORY "${_os_sdk_cache_candidate}/os_sdk" AND
                EXISTS "${_os_sdk_cache_candidate}/os_sdk/manifest.json" AND
                EXISTS "${_os_sdk_cache_candidate}/.archive.sha256")
            file(TIMESTAMP
                "${_os_sdk_cache_candidate}"
                _os_sdk_cache_timestamp
                "%Y%m%d%H%M%S"
                UTC)
            list(APPEND _valid_os_sdk_cache_entries
                "${_os_sdk_cache_timestamp}|${_os_sdk_cache_candidate}")
        endif()
    endforeach()

    if(NOT _valid_os_sdk_cache_entries)
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    list(SORT _valid_os_sdk_cache_entries)
    list(REVERSE _valid_os_sdk_cache_entries)
    list(GET _valid_os_sdk_cache_entries 0 _latest_os_sdk_cache_entry)
    string(REGEX REPLACE "^[^|]*\\|" "" _latest_os_sdk_cache_dir "${_latest_os_sdk_cache_entry}")
    set(${OUT_VAR} "${_latest_os_sdk_cache_dir}" PARENT_SCOPE)
endfunction()

function(jy_app_prepare_os_sdk)
    set(JY_APP_OS_SDK_ARCHIVE "" CACHE STRING "Path to jy_os_sdk 7z archive")
    set(JY_APP_OS_SDK_SEVEN_ZIP "7z" CACHE FILEPATH "7-Zip executable used to extract jy_os_sdk archives")
    set(JY_APP_OS_SDK_APP_TAG "FLA>" CACHE STRING "App log prefix used by floatair_dbg.h")

    if(JY_APP_OS_SDK_ARCHIVE)
        file(TO_CMAKE_PATH "${JY_APP_OS_SDK_ARCHIVE}" JY_APP_OS_SDK_ARCHIVE_NORMALIZED)
        if(IS_ABSOLUTE "${JY_APP_OS_SDK_ARCHIVE_NORMALIZED}")
            set(JY_APP_OS_SDK_ARCHIVE_ABS "${JY_APP_OS_SDK_ARCHIVE_NORMALIZED}")
        else()
            set(JY_APP_OS_SDK_ARCHIVE_ABS "${PROJECT_ROOT}/${JY_APP_OS_SDK_ARCHIVE_NORMALIZED}")
        endif()
        get_filename_component(JY_APP_OS_SDK_ARCHIVE_ABS
            "${JY_APP_OS_SDK_ARCHIVE_ABS}"
            ABSOLUTE)

        if(NOT EXISTS "${JY_APP_OS_SDK_ARCHIVE_ABS}")
            message(FATAL_ERROR "JY_APP_OS_SDK_ARCHIVE does not exist: ${JY_APP_OS_SDK_ARCHIVE_ABS}")
        endif()

        set_property(DIRECTORY APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS "${JY_APP_OS_SDK_ARCHIVE_ABS}")

        file(SHA256 "${JY_APP_OS_SDK_ARCHIVE_ABS}" JY_APP_OS_SDK_SHA256)
        string(SUBSTRING "${JY_APP_OS_SDK_SHA256}" 0 16 JY_APP_OS_SDK_CACHE_KEY)
        set(JY_APP_OS_SDK_CACHE_DIR "${PROJECT_ROOT}/.os_sdk_cache/${JY_APP_OS_SDK_CACHE_KEY}")

        if(IS_ABSOLUTE "${JY_APP_OS_SDK_SEVEN_ZIP}" AND EXISTS "${JY_APP_OS_SDK_SEVEN_ZIP}")
            set(JY_APP_OS_SDK_SEVEN_ZIP_EXE "${JY_APP_OS_SDK_SEVEN_ZIP}")
        else()
            find_program(JY_APP_OS_SDK_SEVEN_ZIP_EXE
                NAMES "${JY_APP_OS_SDK_SEVEN_ZIP}" 7z 7za
                HINTS
                    "C:/Program Files/7-Zip"
                    "C:/Program Files (x86)/7-Zip")
        endif()
        if(NOT JY_APP_OS_SDK_SEVEN_ZIP_EXE)
            message(FATAL_ERROR "Could not find 7-Zip executable. Set JY_APP_OS_SDK_SEVEN_ZIP to 7z.exe.")
        endif()

        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/os_sdk_extract.py"
                    --archive "${JY_APP_OS_SDK_ARCHIVE_ABS}"
                    --dest "${JY_APP_OS_SDK_CACHE_DIR}"
                    --sha256 "${JY_APP_OS_SDK_SHA256}"
                    --seven-zip "${JY_APP_OS_SDK_SEVEN_ZIP_EXE}"
            WORKING_DIRECTORY "${PROJECT_ROOT}"
            RESULT_VARIABLE JY_APP_OS_SDK_EXTRACT_RESULT
        )
        if(NOT JY_APP_OS_SDK_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract OS SDK archive: ${JY_APP_OS_SDK_ARCHIVE_ABS}")
        endif()
    else()
        jy_app_find_latest_os_sdk_cache(JY_APP_OS_SDK_CACHE_DIR)
    endif()

    if(NOT JY_APP_OS_SDK_CACHE_DIR)
        message(FATAL_ERROR
            "JY_APP_OS_SDK_ARCHIVE is not set and no valid OS SDK cache exists under "
            "${PROJECT_ROOT}/.os_sdk_cache. "
            "Pass -DJY_APP_OS_SDK_ARCHIVE=/path/to/jy_os_sdk_<branch>_<tag>_<count>_g<hash>_dev.7z once.")
    endif()

    set(JY_APP_OS_SDK_ROOT "${JY_APP_OS_SDK_CACHE_DIR}/os_sdk")
    message(STATUS "Using OS SDK cache: ${JY_APP_OS_SDK_CACHE_DIR}")

    set(JY_APP_OS_SDK_OVERRIDE_DIR "${CMAKE_BINARY_DIR}/os_sdk_overrides")
    file(MAKE_DIRECTORY "${JY_APP_OS_SDK_OVERRIDE_DIR}")
    string(CONFIGURE [=[
#pragma once

#include_next "floatair_dbg.h"

#undef APP_TAG
#define APP_TAG "@JY_APP_OS_SDK_APP_TAG@"

#ifdef __cplusplus
extern "C" {
#endif
const char* app_router_get_app(void);
#ifdef __cplusplus
}
#endif

#define JY_APP_FLOATAIR_LOG(priority, color, fmt, args...)                                               \
    ({                                                                                                   \
        const char* _floatair_current_app = app_router_get_app();                                        \
        syslog(priority, color APP_TAG "[%s] " fmt " {%s:%d}" ANSI_COLOR_RESET,                        \
               (_floatair_current_app != NULL && _floatair_current_app[0] != '\0')                       \
                   ? _floatair_current_app                                                               \
                   : "N/A",                                                                             \
               ##args, __FUNCTION__, __LINE__);                                                          \
    })

#undef floatair_err
#undef floatair_warn
#undef floatair_info
#undef floatair_dbg
#define floatair_err(fmt, args...)  JY_APP_FLOATAIR_LOG(LOG_ERR, ANSI_COLOR_BRIGHT_RED, fmt, ##args)
#define floatair_warn(fmt, args...) JY_APP_FLOATAIR_LOG(LOG_WARNING, ANSI_COLOR_RED, fmt, ##args)
#define floatair_info(fmt, args...) JY_APP_FLOATAIR_LOG(LOG_INFO, ANSI_COLOR_BRIGHT_GREEN, fmt, ##args)
#define floatair_dbg(fmt, args...)  JY_APP_FLOATAIR_LOG(LOG_DEBUG, ANSI_COLOR_BRIGHT_BLACK, fmt, ##args)
]=] _os_sdk_dbg_override_content @ONLY)
    file(WRITE
        "${JY_APP_OS_SDK_OVERRIDE_DIR}/floatair_dbg.h"
        "${_os_sdk_dbg_override_content}")

    string(CONFIGURE [=[
#pragma once

#include "floatair_dbg.h"
#include_next "floatair_osal.h"

#undef APP_TAG
#define APP_TAG "@JY_APP_OS_SDK_APP_TAG@"
]=] _os_sdk_osal_override_content @ONLY)
    file(WRITE
        "${JY_APP_OS_SDK_OVERRIDE_DIR}/floatair_osal.h"
        "${_os_sdk_osal_override_content}")

    set(JY_APP_OS_SDK_FLOATAIR_DIR "${JY_APP_OS_SDK_ROOT}/floatair" PARENT_SCOPE)
    set(NUTTX_INCLUDE_DIR "${JY_APP_OS_SDK_ROOT}/nuttx" PARENT_SCOPE)
    set(JY_APP_OS_SDK_VENDOR_DIR "${JY_APP_OS_SDK_ROOT}/vendor" PARENT_SCOPE)
    set(JY_APP_OS_SDK_OVERRIDE_DIR "${JY_APP_OS_SDK_OVERRIDE_DIR}" PARENT_SCOPE)
    set(JY_APP_OS_SDK_APP_TAG "${JY_APP_OS_SDK_APP_TAG}" PARENT_SCOPE)

    foreach(required_os_sdk_dir IN ITEMS
            "${JY_APP_OS_SDK_ROOT}/floatair"
            "${JY_APP_OS_SDK_ROOT}/nuttx"
            "${JY_APP_OS_SDK_ROOT}/vendor")
        if(NOT IS_DIRECTORY "${required_os_sdk_dir}")
            message(FATAL_ERROR "Required OS SDK directory missing: ${required_os_sdk_dir}")
        endif()
    endforeach()
endfunction()
