cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(common_tests)

# Code under test: the socket-free parts of app/src/common
set(APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(OPEN1722_DIR ${APP_DIR}/../lib/open1722)
if(NOT EXISTS ${OPEN1722_DIR}/include/avtp/acf/Gbb.h)
  message(FATAL_ERROR "Open1722 not found in lib/open1722. Run: git submodule update --init")
endif()

file(GLOB test_sources src/*.c)

target_sources(app PRIVATE ${test_sources} ${APP_DIR}/src/common/gbb.c)
target_include_directories(app PRIVATE ${APP_DIR}/src/common ${OPEN1722_DIR}/include)
