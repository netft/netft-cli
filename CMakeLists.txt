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

add_library(netft_cli_output
  src/sensor/netft_backend.cpp
  src/platform/interrupt.cpp
  src/output/records.cpp
  src/output/context.cpp
  src/output/json.cpp
  src/output/csv.cpp
  src/output/terminal.cpp
)
if(WIN32)
  target_sources(netft_cli_output PRIVATE src/platform/terminal_windows.cpp)
else()
  target_sources(netft_cli_output PRIVATE src/platform/terminal_posix.cpp)
endif()
target_compile_features(netft_cli_output PUBLIC cxx_std_17)
target_include_directories(netft_cli_output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netft_cli_output PUBLIC netft_core netft_cli_options)
netft_enable_warnings(netft_cli_output)
netft_enable_sanitizers(netft_cli_output)

add_library(netft_cli_commands src/commands/info.cpp)
target_compile_features(netft_cli_commands PUBLIC cxx_std_17)
target_include_directories(netft_cli_commands PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netft_cli_commands PUBLIC netft_cli_output)
netft_enable_warnings(netft_cli_commands)
netft_enable_sanitizers(netft_cli_commands)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(GTest REQUIRED)
  find_package(nlohmann_json REQUIRED)

  add_executable(netft_options_test test/unit/options_test.cpp)
  target_compile_features(netft_options_test PRIVATE cxx_std_17)
  target_link_libraries(netft_options_test PRIVATE netft_cli_options GTest::gtest_main)
  netft_enable_warnings(netft_options_test)
  netft_enable_sanitizers(netft_options_test)

  add_test(NAME netft_options COMMAND netft_options_test)

  add_library(netft_test_support
    test/support/fake_backend.cpp
    test/support/structured_parsers.cpp
  )
  target_compile_features(netft_test_support PUBLIC cxx_std_17)
  target_include_directories(netft_test_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/test"
  )
  target_link_libraries(netft_test_support PUBLIC
    netft_core
    netft_cli_options
    GTest::gtest
    nlohmann_json::nlohmann_json
  )
  netft_enable_warnings(netft_test_support)
  netft_enable_sanitizers(netft_test_support)

  foreach(test_name IN ITEMS records json csv)
    add_executable(netft_${test_name}_test test/unit/${test_name}_test.cpp)
    target_compile_features(netft_${test_name}_test PRIVATE cxx_std_17)
    target_link_libraries(netft_${test_name}_test PRIVATE
      netft_cli_output
      netft_test_support
      GTest::gtest_main
    )
    netft_enable_warnings(netft_${test_name}_test)
    netft_enable_sanitizers(netft_${test_name}_test)
    add_test(NAME netft_${test_name} COMMAND netft_${test_name}_test)
  endforeach()

  foreach(test_name IN ITEMS interrupt terminal)
    add_executable(netft_${test_name}_test test/unit/${test_name}_test.cpp)
    target_compile_features(netft_${test_name}_test PRIVATE cxx_std_17)
    target_link_libraries(netft_${test_name}_test PRIVATE
      netft_cli_output
      netft_test_support
      GTest::gtest_main
    )
    netft_enable_warnings(netft_${test_name}_test)
    netft_enable_sanitizers(netft_${test_name}_test)
    add_test(NAME netft_${test_name} COMMAND netft_${test_name}_test)
  endforeach()

  add_executable(netft_info_test test/unit/info_command_test.cpp)
  target_compile_features(netft_info_test PRIVATE cxx_std_17)
  target_link_libraries(netft_info_test PRIVATE
    netft_cli_commands
    netft_cli_output
    netft_test_support
    GTest::gtest_main
  )
  netft_enable_warnings(netft_info_test)
  netft_enable_sanitizers(netft_info_test)
  add_test(NAME netft_info COMMAND netft_info_test)

  add_test(
    NAME sync_core
    COMMAND "${Python3_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/test_sync_core.py" -q
  )
  set_tests_properties(sync_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
