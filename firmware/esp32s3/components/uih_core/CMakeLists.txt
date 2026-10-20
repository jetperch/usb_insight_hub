# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

# Pure C99, no ESP-IDF dependencies: also built by test/host.
file(GLOB srcs ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
