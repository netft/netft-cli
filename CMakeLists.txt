cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(netft_cli VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)

option(NETFT_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(NETFT_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)

if(NETFT_CLANG_TIDY)
  find_program(NETFT_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY
      "${NETFT_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
endif()

find_package(Threads REQUIRED)
include(cmake/StaticCurl.cmake)
netft_prepare_static_curl()
find_package(CURL 7.63.0 REQUIRED MODULE)
netft_lock_static_curl_target()

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/NetftCore.cmake)

add_library(netft_cli_options src/cli/options.cpp)
target_compile_features(netft_cli_options PUBLIC cxx_std_17)
target_include_directories(netft_cli_options PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
netft_enable_warnings(netft_cli_options)
netft_enable_sanitizers(netft_cli_options)

add_library(netft_cli_output
  src/sensor/netft_backend.cpp
  src/platform/clock.cpp
  src/platform/interrupt.cpp
  src/monitor/latest_sample.cpp
  src/output/records.cpp
  src/output/context.cpp
  src/output/confirmation.cpp
  src/output/json.cpp
  src/output/csv.cpp
  src/output/terminal.cpp
)
if(WIN32)
  target_sources(netft_cli_output PRIVATE
    src/platform/line_reader_windows.cpp
    src/platform/terminal_windows.cpp
  )
else()
  target_sources(netft_cli_output PRIVATE
    src/platform/line_reader_posix.cpp
    src/platform/terminal_posix.cpp
  )
endif()
target_compile_features(netft_cli_output PUBLIC cxx_std_17)
target_include_directories(netft_cli_output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netft_cli_output PUBLIC netft_core netft_cli_options)
netft_enable_warnings(netft_cli_output)
netft_enable_sanitizers(netft_cli_output)

add_library(netft_cli_commands
  src/commands/bias.cpp
  src/commands/info.cpp
  src/commands/monitor.cpp
)
target_compile_features(netft_cli_commands PUBLIC cxx_std_17)
target_include_directories(netft_cli_commands PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netft_cli_commands PUBLIC netft_cli_output)
netft_enable_warnings(netft_cli_commands)
netft_enable_sanitizers(netft_cli_commands)

add_library(netft_cli_application src/application.cpp)
target_compile_features(netft_cli_application PUBLIC cxx_std_17)
target_include_directories(netft_cli_application PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netft_cli_application PUBLIC netft_cli_commands)
target_compile_definitions(netft_cli_application PRIVATE
  NETFT_CLI_VERSION="${PROJECT_VERSION}"
)
netft_enable_warnings(netft_cli_application)
netft_enable_sanitizers(netft_cli_application)

add_executable(netft src/main.cpp)
target_compile_features(netft PRIVATE cxx_std_17)
target_link_libraries(netft PRIVATE netft_cli_application)
netft_enable_warnings(netft)
netft_enable_sanitizers(netft)

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

  foreach(test_name IN ITEMS clock interrupt terminal)
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

  if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_executable(netft_interrupt_signal_macro_test
      src/platform/interrupt.cpp
      test/unit/interrupt_test.cpp
    )
    target_compile_features(netft_interrupt_signal_macro_test PRIVATE cxx_std_17)
    target_include_directories(netft_interrupt_signal_macro_test PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_options(netft_interrupt_signal_macro_test PRIVATE
      "-include${CMAKE_CURRENT_SOURCE_DIR}/test/support/sigemptyset_macro.hpp"
    )
    target_link_libraries(netft_interrupt_signal_macro_test PRIVATE
      Threads::Threads
      GTest::gtest_main
    )
    netft_enable_warnings(netft_interrupt_signal_macro_test)
    netft_enable_sanitizers(netft_interrupt_signal_macro_test)
    add_test(NAME netft_interrupt_signal_macro COMMAND netft_interrupt_signal_macro_test)
  endif()

  add_executable(netft_latest_sample_test test/unit/latest_sample_test.cpp)
  target_compile_features(netft_latest_sample_test PRIVATE cxx_std_17)
  target_link_libraries(netft_latest_sample_test PRIVATE
    netft_cli_output
    netft_test_support
    GTest::gtest_main
  )
  netft_enable_warnings(netft_latest_sample_test)
  netft_enable_sanitizers(netft_latest_sample_test)
  add_test(NAME netft_latest_sample COMMAND netft_latest_sample_test)

  add_executable(netft_line_reader_test test/unit/line_reader_test.cpp)
  target_compile_features(netft_line_reader_test PRIVATE cxx_std_17)
  target_link_libraries(netft_line_reader_test PRIVATE
    netft_cli_output
    netft_test_support
    GTest::gtest_main
  )
  netft_enable_warnings(netft_line_reader_test)
  netft_enable_sanitizers(netft_line_reader_test)
  add_test(NAME netft_line_reader COMMAND netft_line_reader_test)

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

  add_executable(netft_monitor_test test/unit/monitor_command_test.cpp)
  target_compile_features(netft_monitor_test PRIVATE cxx_std_17)
  target_link_libraries(netft_monitor_test PRIVATE
    netft_cli_commands
    netft_cli_output
    netft_test_support
    GTest::gtest_main
  )
  netft_enable_warnings(netft_monitor_test)
  netft_enable_sanitizers(netft_monitor_test)
  add_test(NAME netft_monitor COMMAND netft_monitor_test)

  add_executable(netft_bias_test test/unit/bias_command_test.cpp)
  target_compile_features(netft_bias_test PRIVATE cxx_std_17)
  target_link_libraries(netft_bias_test PRIVATE
    netft_cli_commands
    netft_cli_output
    netft_test_support
    GTest::gtest_main
  )
  netft_enable_warnings(netft_bias_test)
  netft_enable_sanitizers(netft_bias_test)
  add_test(NAME netft_bias COMMAND netft_bias_test)

  if(NOT WIN32)
    add_executable(netft_udp_port_isolation_test test/unit/udp_port_isolation_test.cpp)
    target_include_directories(netft_udp_port_isolation_test PRIVATE core/netft/src)
    target_link_libraries(netft_udp_port_isolation_test PRIVATE netft_core GTest::gtest_main)
    netft_enable_warnings(netft_udp_port_isolation_test)
    netft_enable_sanitizers(netft_udp_port_isolation_test)
    add_test(NAME netft_udp_port_isolation COMMAND netft_udp_port_isolation_test)
  endif()

  add_executable(netft_cli_integration_test
    test/integration/cli_integration_test.cpp
    test/integration/fake_sensor.cpp
  )
  target_compile_features(netft_cli_integration_test PRIVATE cxx_std_17)
  target_include_directories(netft_cli_integration_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/test"
  )
  target_link_libraries(netft_cli_integration_test PRIVATE
    netft_cli_application
    netft_test_support
    GTest::gtest_main
  )
  if(WIN32)
    target_link_libraries(netft_cli_integration_test PRIVATE ws2_32)
  endif()
  netft_enable_warnings(netft_cli_integration_test)
  netft_enable_sanitizers(netft_cli_integration_test)
  add_test(NAME netft_cli_integration COMMAND netft_cli_integration_test)

  add_test(
    NAME sync_core
    COMMAND "${Python3_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/test_sync_core.py" -q
  )
  set_tests_properties(sync_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  add_test(
    NAME netft_release_tools
    COMMAND "${Python3_EXECUTABLE}" -m pytest
            "${CMAKE_CURRENT_SOURCE_DIR}/test/tools/test_release_tools.py" -q
  )
  set_tests_properties(netft_release_tools
                       PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  add_test(
    NAME netft_cli_process
    COMMAND
      "${CMAKE_COMMAND}" -E env "NETFT_EXECUTABLE=$<TARGET_FILE:netft>"
      "${Python3_EXECUTABLE}" -m pytest
      "${CMAKE_CURRENT_SOURCE_DIR}/test/integration/process_test.py" -q
  )
  set_tests_properties(netft_cli_process PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  if(WIN32)
    add_test(
      NAME netft_release_artifacts
      COMMAND
        "${Python3_EXECUTABLE}" -m pytest
        "${CMAKE_CURRENT_SOURCE_DIR}/test/artifact" -q
        --ignore "${CMAKE_CURRENT_SOURCE_DIR}/test/artifact/install_sh_test.py"
        --ignore "${CMAKE_CURRENT_SOURCE_DIR}/test/artifact/test_static_curl.py"
    )
  else()
    add_test(
      NAME netft_release_artifacts
      COMMAND "${Python3_EXECUTABLE}" -m pytest
              "${CMAKE_CURRENT_SOURCE_DIR}/test/artifact" -q
    )
  endif()
  set_tests_properties(netft_release_artifacts
                       PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
