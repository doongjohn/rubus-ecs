add_executable(rubus-ecs-example "")

set_property(TARGET rubus-ecs-example PROPERTY EXCLUDE_FROM_ALL true)
set_property(TARGET rubus-ecs-example PROPERTY CXX_STANDARD 20)
set_property(TARGET rubus-ecs-example PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)
use_sanitizer(rubus-ecs-example)

target_sources(
  rubus-ecs-example
  PRIVATE
    example/main.cpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    rubus-ecs-example
    PRIVATE
      -Wall
      -Wextra
  )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(
    rubus-ecs-example
    PRIVATE
      /W3
      /sdl
  )
endif()

target_link_libraries(
  rubus-ecs-example
  PRIVATE
    rubus-ecs
)
