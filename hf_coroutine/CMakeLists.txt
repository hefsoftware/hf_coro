cmake_minimum_required(VERSION 3.9)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

project(hf_coroutine)

add_library(hf_coroutine STATIC
  hf_coroutine.h
  hf_awaitable.h
  hf_common.h
)

target_link_libraries(hf_coroutine Qt${QT_VERSION_MAJOR}::Core)
