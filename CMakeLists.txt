cmake_minimum_required(VERSION 3.16)

project(netft_cli VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(NETFT_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(Threads REQUIRED)
find_package(CURL 7.63.0 REQUIRED)

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/NetftCore.cmake)

add_executable(netft src/main.cpp)
target_compile_features(netft PRIVATE cxx_std_17)
target_link_libraries(netft PRIVATE netft_core)
netft_enable_warnings(netft)
netft_enable_sanitizers(netft)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME sync_core
    COMMAND "${Python3_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/test_sync_core.py" -q
  )
  set_tests_properties(sync_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
