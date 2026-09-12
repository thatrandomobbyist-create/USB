cmake_minimum_required(VERSION 3.16)
project(UnoGPIO LANGUAGES CXX)

add_library(UnoGPIO UnoGPIO/UnoGPIO.cpp)
target_include_directories(UnoGPIO PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/UnoGPIO)
target_compile_features(UnoGPIO PUBLIC cxx_std_17)

if (WIN32)
  add_executable(WindowsExample examples/WindowsExample/main.cpp)
  target_link_libraries(WindowsExample PRIVATE UnoGPIO)
endif()
