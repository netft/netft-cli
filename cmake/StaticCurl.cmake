function(netft_prepare_static_curl)
  if(NOT CURL_USE_STATIC_LIBS)
    return()
  endif()
  if(NOT CURL_ROOT)
    message(FATAL_ERROR "CURL_ROOT is required for a static release build")
  endif()

  get_filename_component(_netft_curl_root "${CURL_ROOT}" REALPATH)
  set(_netft_curl_include "${_netft_curl_root}/include")
  if(NOT EXISTS "${_netft_curl_include}/curl/curl.h")
    message(FATAL_ERROR "the static curl prefix does not contain curl headers")
  endif()

  if(WIN32)
    set(_netft_curl_library "${_netft_curl_root}/lib/libcurl_a.lib")
    if(EXISTS "${_netft_curl_root}/lib/libcurl.lib")
      message(FATAL_ERROR "the static curl prefix contains an ambiguous import library")
    endif()
    file(GLOB_RECURSE _netft_curl_dlls
      "${_netft_curl_root}/bin/libcurl*.dll"
      "${_netft_curl_root}/lib/libcurl*.dll")
    if(_netft_curl_dlls)
      message(FATAL_ERROR "the static curl prefix contains a shared library")
    endif()
  else()
    set(_netft_curl_library "${_netft_curl_root}/lib/libcurl.a")
  endif()

  if(NOT EXISTS "${_netft_curl_library}")
    message(FATAL_ERROR "the selected static curl library does not exist")
  endif()
  file(READ "${_netft_curl_library}" _netft_curl_archive_magic
       LIMIT 8 HEX)
  string(TOLOWER "${_netft_curl_archive_magic}"
         _netft_curl_archive_magic)
  if(NOT _netft_curl_archive_magic STREQUAL "213c617263683e0a")
    message(FATAL_ERROR "the selected curl library is not a static archive")
  endif()
  get_filename_component(_netft_curl_library "${_netft_curl_library}" REALPATH)
  file(TO_CMAKE_PATH "${_netft_curl_root}/" _netft_curl_root_with_separator)
  file(TO_CMAKE_PATH "${_netft_curl_library}" _netft_curl_library_normalized)
  string(FIND "${_netft_curl_library_normalized}"
              "${_netft_curl_root_with_separator}" _netft_curl_prefix_position)
  if(NOT _netft_curl_prefix_position EQUAL 0)
    message(FATAL_ERROR "the static curl library resolves outside CURL_ROOT")
  endif()

  set(CURL_INCLUDE_DIR "${_netft_curl_include}" CACHE PATH
      "Pinned static curl include directory" FORCE)
  set(CURL_LIBRARY "${_netft_curl_library}" CACHE FILEPATH
      "Pinned static curl library" FORCE)
  set(CURL_LIBRARY_RELEASE "${_netft_curl_library}" CACHE FILEPATH
      "Pinned static curl release library" FORCE)
  set(NETFT_CLI_STATIC_CURL_LIBRARY "${_netft_curl_library}" CACHE INTERNAL
      "Validated static curl library" FORCE)
endfunction()

function(netft_lock_static_curl_target)
  if(NOT CURL_USE_STATIC_LIBS)
    return()
  endif()
  if(NOT TARGET CURL::libcurl)
    message(FATAL_ERROR "FindCURL did not define CURL::libcurl")
  endif()
  if(NOT NETFT_CLI_STATIC_CURL_LIBRARY)
    message(FATAL_ERROR "the static curl library was not validated")
  endif()

  set_target_properties(CURL::libcurl PROPERTIES
    IMPORTED_LOCATION "${NETFT_CLI_STATIC_CURL_LIBRARY}"
    IMPORTED_LOCATION_DEBUG "${NETFT_CLI_STATIC_CURL_LIBRARY}"
    IMPORTED_LOCATION_RELEASE "${NETFT_CLI_STATIC_CURL_LIBRARY}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${NETFT_CLI_STATIC_CURL_LIBRARY}"
    IMPORTED_LOCATION_MINSIZEREL "${NETFT_CLI_STATIC_CURL_LIBRARY}"
  )
  get_target_property(_netft_curl_imported_location
                      CURL::libcurl IMPORTED_LOCATION)
  if(NOT _netft_curl_imported_location STREQUAL
         NETFT_CLI_STATIC_CURL_LIBRARY)
    message(FATAL_ERROR "CURL::libcurl was not locked to the static archive")
  endif()
endfunction()
