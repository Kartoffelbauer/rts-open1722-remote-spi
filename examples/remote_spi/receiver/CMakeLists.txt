cmake_minimum_required(VERSION 3.20.0)

# Network and debug settings shared with the other board, merged on top of prj.conf
list(APPEND EXTRA_CONF_FILE
  ${CMAKE_CURRENT_LIST_DIR}/../common/network.conf
  ${CMAKE_CURRENT_LIST_DIR}/../common/debug.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote_spi_receiver)

# Open1722 is header-only (all functions are static inline), so only its include folder is needed
set(OPEN1722_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../lib/open1722)
if(NOT EXISTS ${OPEN1722_DIR}/include/avtp/acf/Gbb.h)
  message(FATAL_ERROR "Open1722 not found in lib/open1722. Run: git submodule update --init")
endif()

target_sources(app PRIVATE src/main.c ../common/remote_spi.c)
target_include_directories(app PRIVATE ../common ${OPEN1722_DIR}/include)
