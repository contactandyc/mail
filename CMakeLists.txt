# SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Maintainer: Andy Curtis <contactandyc@gmail.com>

cmake_minimum_required(VERSION 3.20)

project(mail
  VERSION 0.1.1
  LANGUAGES C
)

if(CMAKE_PREFIX_PATH)
  include_directories("${CMAKE_PREFIX_PATH}/include")
  link_directories("${CMAKE_PREFIX_PATH}/lib")
endif()

# ── Variant selection for consuming dependencies ───────────────
set(A_BUILD_VARIANT "static" CACHE STRING
    "Library variant to link against (debug|memory|static|shared)")
set_property(CACHE A_BUILD_VARIANT PROPERTY STRINGS debug memory static shared)

if(MSVC)
  set(_A_DEBUG_OPTS /Zi /Od)
  set(_A_RELEASE_OPTS /O2 /DNDEBUG)
else()
  set(_A_DEBUG_OPTS -O0 -g)
  set(_A_RELEASE_OPTS -O3 -DNDEBUG)
endif()

include(GNUInstallDirs)

# ---- Global Dependencies ----
find_package(a_json_library CONFIG REQUIRED)
find_package(a_memory_library CONFIG REQUIRED)
find_package(a_curl_library CONFIG REQUIRED)
find_package(a_curl_gcloud_plugin CONFIG REQUIRED)
find_package(the_io_library CONFIG REQUIRED)
find_package(h2o_c_library CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
if(NOT TARGET sqlite3)
  add_library(sqlite3 ALIAS PkgConfig::PC_SQLITE3)
endif()

# ── Application Executables ───────────────────────────────────────────────────

add_executable(mail
  src/main.c
)

target_include_directories(mail PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(mail PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)

target_link_libraries(mail PRIVATE
  a_json_library::a_json_library
  a_memory_library::a_memory_library
  a_curl_library::a_curl_library
  a_curl_gcloud_plugin::a_curl_gcloud_plugin
  the_io_library::the_io_library
  h2o_c_library::h2o_c_library
  sqlite3
)

target_compile_options(mail PRIVATE
  $<$<CONFIG:Debug>:${_A_DEBUG_OPTS}>
  $<$<NOT:$<CONFIG:Debug>>:${_A_RELEASE_OPTS}>
)

install(TARGETS mail
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Extra project-specific targets


# Dynamically mount all subprojects (apps, examples, tests, etc.)
