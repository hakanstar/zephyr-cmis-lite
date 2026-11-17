# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cmis_core_test)

set(CMIS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/cmis_core)

target_sources(app PRIVATE
  src/test_cmis_core.c
  ${CMIS_CORE_DIR}/src/cmis_core.c
)
target_include_directories(app PRIVATE ${CMIS_CORE_DIR}/include)
