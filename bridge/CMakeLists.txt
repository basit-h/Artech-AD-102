cmake_minimum_required(VERSION 3.15)
project(artech_bridge_x86 C)

if(NOT WIN32)
  message(FATAL_ERROR "The Artech SDK bridge is Windows-only.")
endif()

add_executable(artech_bridge_x86 ../src/artech_bridge.c)
target_include_directories(artech_bridge_x86 PRIVATE ../src)
target_compile_definitions(artech_bridge_x86 PRIVATE WIN32_LEAN_AND_MEAN)

if(MSVC)
  set_property(TARGET artech_bridge_x86 PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(artech_bridge_x86 PRIVATE /W4 /WX /wd"4100")
endif()
