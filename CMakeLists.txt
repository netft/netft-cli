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

add_library(netft_cli_options src/cli/options.cpp)
target_compile_features(netft_cli_options PUBLIC cxx_std_17)
target_include_directories(netft_cli_options PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
netft_enable_warnings(netft_cli_options)
netft_enable_sanitizers(netft_cli_options)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(GTest REQUIRED)

  add_executable(netft_options_test test/unit/options_test.cpp)
  target_compile_features(netft_options_test PRIVATE cxx_std_17)
  target_link_libraries(netft_options_test PRIVATE netft_cli_options GTest::gtest_main)
  netft_enable_warnings(netft_options_test)
  netft_enable_sanitizers(netft_options_test)

  add_test(NAME netft_options COMMAND netft_options_test)

  add_test(
    NAME sync_core
    COMMAND "${Python3_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/test_sync_core.py" -q
  )
  set_tests_properties(sync_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
