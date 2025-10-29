include(FetchContent)

if(WIN32)
    set(QT_URL_X86 "https://mlperf-public-files.s3-accelerate.amazonaws.com/binaries/Qt/6.9.0/Windows_x64.zip")
    set(QT_URL_ARM "https://mlperf-public-files.s3-accelerate.amazonaws.com/binaries/Qt/6.9.0/Windows_ARM.zip")
endif()

function(download_and_setup_qt url destination_dir qt_target_name)
    # We avoid FetchContent subbuilds which create long nested paths on Windows
    if(NOT EXISTS ${destination_dir})
        message(STATUS "Downloading Qt from: ${url}")

        set(_qt_archive "${CMAKE_BINARY_DIR}/qt_download.zip")
        file(REMOVE_RECURSE "${_qt_archive}")

        message(STATUS "Downloading Qt archive to ${_qt_archive} (this may take a while)...")
        file(DOWNLOAD ${url} ${_qt_archive} SHOW_PROGRESS STATUS _dl_status)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            message(FATAL_ERROR "Failed to download Qt from ${url} (status: ${_dl_status})")
        endif()

        set(_extract_dir "${CMAKE_BINARY_DIR}/qt_extract_${qt_target_name}")
        file(REMOVE_RECURSE "${_extract_dir}")
        file(MAKE_DIRECTORY "${_extract_dir}")

        message(STATUS "Extracting Qt archive to ${_extract_dir}...")
        file(ARCHIVE_EXTRACT INPUT "${_qt_archive}" DESTINATION "${_extract_dir}")
        file(REMOVE "${_qt_archive}")

        # Find the extracted folder that contains lib/cmake/Qt6/Qt6Config.cmake
        set(_found_qt_root "")
        if(EXISTS "${_extract_dir}/lib/cmake/Qt6/Qt6Config.cmake")
            set(_found_qt_root "${_extract_dir}")
        else()
            file(GLOB _children RELATIVE "${_extract_dir}" "${_extract_dir}/*")
            foreach(_c ${_children})
                if(EXISTS "${_extract_dir}/${_c}/lib/cmake/Qt6/Qt6Config.cmake")
                    set(_found_qt_root "${_extract_dir}/${_c}")
                    break()
                endif()
            endforeach()
        endif()

        if(_found_qt_root STREQUAL "")
            message(FATAL_ERROR "Qt download/extract did not produce a valid Qt layout containing lib/cmake/Qt6/Qt6Config.cmake")
        endif()

        # Move (rename) the found root to the requested destination for consistency
        # so other CMake logic can use the expected ${destination_dir} path.
        file(REMOVE_RECURSE "${destination_dir}")
        file(RENAME "${_found_qt_root}" "${destination_dir}")
        # Clean up any leftover extract dir (if it still exists and is different)
        if(EXISTS "${_extract_dir}")
            file(REMOVE_RECURSE "${_extract_dir}")
        endif()

        message(STATUS "Qt downloaded and extracted to: ${destination_dir}")
    else()
        message(STATUS "Qt found at: ${destination_dir}")
    endif()
endfunction()


if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(CMAKE_PREFIX_PATH "")
    if(WIN32)
        set(qt_target_name "qt_windows")
        if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
            message(STATUS "Using x86 version of Qt")
            set(QT_URL ${QT_URL_X86})
        else()
            message(STATUS "Using ARM version of Qt")
            set(QT_URL ${QT_URL_ARM})
        endif()
    endif()
    if(DEFINED QT_URL AND DEFINED qt_target_name)
        download_and_setup_qt(${QT_URL} ${CMAKE_BINARY_DIR}/qt ${qt_target_name})
        list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/qt)
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE STRING "Qt path" FORCE)
        message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
    else()
        message(STATUS "No Qt download configured for this platform; using system Qt if available")
    endif()
else()
    message(STATUS "Using user-specified Qt from: ${CMAKE_PREFIX_PATH}")
endif()

           
            



