set(NETFT_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../core/netft")

add_library(netft_core STATIC
  "${NETFT_CORE_ROOT}/src/types.cpp"
  "${NETFT_CORE_ROOT}/src/status.cpp"
  "${NETFT_CORE_ROOT}/src/discovery.cpp"
  "${NETFT_CORE_ROOT}/src/client.cpp"
  "${NETFT_CORE_ROOT}/src/detail/client_impl.cpp"
  "${NETFT_CORE_ROOT}/src/detail/fault_latch.cpp"
  "${NETFT_CORE_ROOT}/src/detail/protocol.cpp"
  "${NETFT_CORE_ROOT}/src/detail/sequence.cpp"
  "${NETFT_CORE_ROOT}/src/detail/xml_config.cpp"
)
if(WIN32)
  target_sources(netft_core PRIVATE "${NETFT_CORE_ROOT}/src/detail/udp_transport_windows.cpp")
  target_link_libraries(netft_core PRIVATE ws2_32)
  if(CURL_USE_STATIC_LIBS)
    target_compile_definitions(netft_core PRIVATE CURL_STATICLIB)
  endif()
else()
  target_sources(netft_core PRIVATE "${NETFT_CORE_ROOT}/src/detail/udp_transport_posix.cpp")
endif()
target_compile_features(netft_core PUBLIC cxx_std_17)
target_include_directories(netft_core PUBLIC "${NETFT_CORE_ROOT}/include"
                                      PRIVATE "${NETFT_CORE_ROOT}/src")
target_link_libraries(netft_core PUBLIC Threads::Threads CURL::libcurl)
netft_enable_warnings(netft_core)
netft_enable_sanitizers(netft_core)
