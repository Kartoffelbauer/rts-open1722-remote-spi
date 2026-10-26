cmake_minimum_required(VERSION 3.20.0)

# Board role, picked at build time: west build ... -- -DROLE=requester (or responder)
set(ROLE "" CACHE STRING "Board role: requester or responder")
if(NOT ROLE MATCHES "^(requester|responder)$")
  message(FATAL_ERROR "Set the board role: west build ... -- -DROLE=requester (or -DROLE=responder)")
endif()

list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/${ROLE}.conf)
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}_${ROLE}.overlay)
  list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}_${ROLE}.overlay)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote_spi)

# Open1722 is used header-only (all functions are static inline)
set(OPEN1722_DIR ${CMAKE_CURRENT_LIST_DIR}/../lib/open1722)
if(NOT EXISTS ${OPEN1722_DIR}/include/avtp/acf/Gbb.h)
  message(FATAL_ERROR "Open1722 not found in lib/open1722. Run: git submodule update --init")
endif()

file(GLOB common_sources src/common/*.c)
file(GLOB role_sources src/${ROLE}/*.c)

target_sources(app PRIVATE ${common_sources} ${role_sources})
target_include_directories(app PRIVATE src/common ${OPEN1722_DIR}/include)
