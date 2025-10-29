include(FetchContent)

function(fetch_zlib_if_missing)
    if (NOT ZLIB_FOUND)
        message(STATUS "ZLIB not found — fetching zlib v1.3.1 via FetchContent")
        FetchContent_Declare(
            zlib_src
            URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip
        )
        FetchContent_GetProperties(zlib_src)
        if (NOT zlib_src_POPULATED)
            # Prefer a static zlib build for predictable target names; adjust if you need shared
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
            FetchContent_Populate(zlib_src)
            set(zlib_binary_dir "${CMAKE_BINARY_DIR}/deps/zlib_src-populated-build")
            file(MAKE_DIRECTORY "${zlib_binary_dir}")
            add_subdirectory("${zlib_src_SOURCE_DIR}" "${zlib_binary_dir}" EXCLUDE_FROM_ALL)
        endif()

        # Normalize common target names to ZLIB::ZLIB
        if (TARGET ZLIB::ZLIB)
            message(STATUS "Fetched zlib and found target ZLIB::ZLIB")
        elseif (TARGET zlibstatic)
            add_library(ZLIB::ZLIB ALIAS zlibstatic)
            message(STATUS "Fetched zlib and created alias ZLIB::ZLIB -> zlibstatic")
        elseif (TARGET zlib)
            add_library(ZLIB::ZLIB ALIAS zlib)
            message(STATUS "Fetched zlib and created alias ZLIB::ZLIB -> zlib")
        else()
            message(FATAL_ERROR "Fetched zlib but could not find a zlib target (expected ZLIB::ZLIB, zlibstatic or zlib).")
        endif()

        set(ZLIB_FOUND TRUE CACHE BOOL "zlib fetched" FORCE)
    else()
        message(STATUS "Using system zlib")
    endif()
endfunction()