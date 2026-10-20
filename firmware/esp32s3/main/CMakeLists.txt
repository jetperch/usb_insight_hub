# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS main.c
                       REQUIRES uih_core uih_drivers
                       PRIV_REQUIRES esp_app_format esp_system freertos)

execute_process(COMMAND git rev-parse --short=7 HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE UIH_GIT OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_rc ERROR_QUIET)
if(NOT git_rc EQUAL 0)
    set(UIH_GIT "unknown")
endif()
# Modified or untracked firmware files mark the build dirty ("+").
execute_process(COMMAND git status --porcelain -- ${CMAKE_CURRENT_LIST_DIR}/..
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(dirty)
    set(UIH_GIT "${UIH_GIT}+")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE UIH_GIT_HASH="${UIH_GIT}")

# Reconfigure (and refresh UIH_GIT) whenever HEAD or the index changes.
execute_process(COMMAND git rev-parse --absolute-git-dir
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE git_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(git_dir)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${git_dir}/HEAD ${git_dir}/index)
endif()
