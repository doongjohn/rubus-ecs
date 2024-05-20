cmake_minimum_required(VERSION 3.27)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

include("cmake/sanitizer.cmake")

project(
  RubusEcs
  VERSION 0.1.0
  DESCRIPTION ""
  HOMEPAGE_URL ""
)

if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "-fdiagnostics-color=always -fansi-escape-codes")
endif()

add_library(rubus-ecs "")

set_property(TARGET rubus-ecs PROPERTY CXX_STANDARD 20)
set_property(TARGET rubus-ecs PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)
use_sanitizer(rubus-ecs)

target_sources(
  rubus-ecs
  PRIVATE
    src/rubus-ecs/ecs.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      src
    FILES
      src/rubus-ecs/ecs.hpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    rubus-ecs
    PRIVATE
      -Wall
      -Wextra
  )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(
    rubus-ecs
    PRIVATE
      /W3
      /sdl
  )
endif()

include("cmake/example.cmake")
