include(FetchContent)

if(WIN32)
    set(QT_URL_X86 "https://mlperf-public-files.s3-accelerate.amazonaws.com/binaries/Qt/6.9.0/Windows_x64.zip")
    set(QT_URL_ARM "https://mlperf-public-files.s3-accelerate.amazonaws.com/binaries/Qt/6.9.0/Windows_ARM.zip")
endif()

function(download_and_setup_qt url destination_dir qt_target_name)
    if(NOT EXISTS ${destination_dir})
        message(STATUS "Downloading Qt from: ${url}")
        string(MD5 qt_target_name ${url})
        FetchContent_Declare(
            ${qt_target_name}
            URL ${url}
            SOURCE_DIR ${destination_dir}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_GetProperties(${qt_target_name})
        if(NOT ${qt_target_name}_POPULATED)
            message(STATUS "Downloading Qt...")
            FetchContent_Populate(${qt_target_name})
            if(NOT EXISTS "${destination_dir}/lib/cmake/Qt6/Qt6Config.cmake")
                message(FATAL_ERROR "Qt download failed")
            else()
               message(STATUS "Qt downloaded to: ${destination_dir}")
            endif()
        endif()
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
    elseif(APPLE)
        set(qt_target_name "qt_mac")
        message(STATUS "Using MacOS version of Qt")
        set(QT_URL ${QT_URL_MAC})
    endif()

    download_and_setup_qt(${QT_URL} ${CMAKE_BINARY_DIR}/qt ${qt_target_name})
    list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/qt)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE STRING "Qt path" FORCE)
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
else()
    message(STATUS "Using user-specified Qt from: ${CMAKE_PREFIX_PATH}")
endif()

           
            



