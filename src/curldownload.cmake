include(FetchContent)

function(fetch_curl_if_missing)
    if (NOT CURL_FOUND)
        message(STATUS "CURL not found — fetching libcurl (curl v8.1.2) via FetchContent")
        FetchContent_Declare(
            curl_src
            URL https://github.com/curl/curl/archive/refs/tags/curl-8_1_2.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            CMAKE_ARGS
                -DBUILD_CURL_EXE=OFF
                -DBUILD_TESTING=OFF
                -DBUILD_SHARED_LIBS=OFF
                -DCURL_USE_SCHANNEL=ON
                -DCURL_DISABLE_LDAP=ON
        )
        FetchContent_GetProperties(curl_src)
        if (NOT curl_src_POPULATED)
            message(STATUS "Fetching and configuring libcurl source...")
            FetchContent_MakeAvailable(curl_src)
        endif()
        # curl's CMake typically provides CURL::libcurl when built
        if (TARGET CURL::libcurl)
            message(STATUS "Fetched libcurl and found target CURL::libcurl")
            set(CURL_FOUND TRUE CACHE BOOL "curl fetched" FORCE)
        else()
            message(FATAL_ERROR "Fetched libcurl but CURL::libcurl target not available. Curl may require extra dependencies (OpenSSL) on your platform.")
        endif()
    else()
        message(STATUS "Using system libcurl")
    endif()
endfunction()