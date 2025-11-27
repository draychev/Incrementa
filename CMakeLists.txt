cmake_minimum_required(VERSION 3.20)
project(WindowsQATrainer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(WindowsQATrainer WIN32
  src/main.cpp
)

target_compile_definitions(WindowsQATrainer PRIVATE UNICODE _UNICODE)

target_link_libraries(WindowsQATrainer PRIVATE)
