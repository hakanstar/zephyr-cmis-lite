# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_cmis_lite)

set(CMIS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib/cmis_core)

target_sources(app PRIVATE
  src/main.c
  src/module.c
  src/i2c_target.c
  ${CMIS_CORE_DIR}/src/cmis_core.c
)
target_include_directories(app PRIVATE ${CMIS_CORE_DIR}/include)
