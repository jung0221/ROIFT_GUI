cmake_minimum_required(VERSION 3.16)
project(roift_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AUTOMOC/AUTOUIC for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(${CMAKE_CURRENT_LIST_DIR}/src/qtdownload.cmake)

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    message(STATUS "Found Qt6")
    set(QT_WIDGETS_LIB Qt6::Widgets)
else()
    message(FATAL_ERROR "Qt6 not found. Aborting.")
endif()

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
    list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
endif()

option(USE_ITK "Automatically fetch/build ITK if not found" ON)

if (NOT DEFINED ITK_INSTALL_DIR)
    if (WIN32)
        set(ITK_INSTALL_DIR "C:/libs/itk_install" CACHE PATH "ITK install prefix")
    else()
        set(ITK_INSTALL_DIR "${CMAKE_BINARY_DIR}/itk_install" CACHE PATH "ITK install prefix")
    endif()
endif()

# Add the install prefix to CMAKE_PREFIX_PATH so find_package can locate ITK
list(APPEND CMAKE_PREFIX_PATH "${ITK_INSTALL_DIR}")

# If a typical ITK cmake folder exists under the prefix, set ITK_DIR for find_package
if (EXISTS "${ITK_INSTALL_DIR}/lib/cmake/ITK")
    set(ITK_DIR "${ITK_INSTALL_DIR}/lib/cmake/ITK" CACHE PATH "ITK install folder" FORCE)
endif()

# Try to find ITK first (it may provide zlib targets and other dependencies)
find_package(ITK CONFIG QUIET)

# If ITK is not found but will be automatically fetched/built, ensure ZLIB is available
# because the ITK ExternalProject can require ZLIB_ROOT.
if (NOT ITK_FOUND AND USE_ITK)
    message(STATUS "ITK not found: preparing to fetch/build ITK - ensuring ZLIB is available for ITK build")
    find_package(ZLIB QUIET)
    include(${CMAKE_CURRENT_LIST_DIR}/src/zlibdownload.cmake)
    fetch_zlib_if_missing()
endif()

# If not found and user requested ITK, configure automatic fetch/build
include(${CMAKE_CURRENT_LIST_DIR}/src/itkdownload.cmake)
fetch_itk_if_missing()

# Ensure we have a usable ZLIB target for linking: prefer ZLIB::ZLIB, fallback to 'zlib',
# otherwise try to find/fetch it now.
find_package(ZLIB QUIET)
include(${CMAKE_CURRENT_LIST_DIR}/src/zlibdownload.cmake)
if (NOT TARGET ZLIB::ZLIB AND NOT TARGET zlib AND NOT TARGET zlibstatic)
    fetch_zlib_if_missing()
endif()

# Normalize legacy names so subprojects can reliably use ZLIB::ZLIB
if (NOT TARGET ZLIB::ZLIB)
    if (TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    elseif (TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()

add_subdirectory(roift)

# CURL (and fetch if needed)
find_package(CURL QUIET)
include(${CMAKE_CURRENT_LIST_DIR}/src/curldownload.cmake)
fetch_curl_if_missing()
message(STATUS "Found CURL: ${CURL_VERSION_STRING}")

add_executable(roift_gui ${SRC_FILES})

if (ITK_FOUND)
    message(STATUS "Found ITK: ${ITK_VERSION} (from ${ITK_DIR})")
    target_compile_definitions(roift_gui PRIVATE HAVE_ITK=1)
else()
    message(STATUS "ITK not found. HAVE_ITK=0")
    target_compile_definitions(roift_gui PRIVATE HAVE_ITK=0)
endif()

# Link core dependencies; add ZLIB target only if we resolved one
if (ZLIB_LINK_TARGET)
    target_link_libraries(roift_gui PRIVATE ${QT_WIDGETS_LIB} ${ZLIB_LINK_TARGET} CURL::libcurl)
else()
    target_link_libraries(roift_gui PRIVATE ${QT_WIDGETS_LIB} CURL::libcurl)
endif()

if (ITK_FOUND)
    target_link_libraries(roift_gui PRIVATE ${ITK_LIBRARIES})
endif()

if (UNIX)
    # ensure we get X11 on Linux systems (Qt usually handles this)
    find_package(X11 QUIET)
    if (X11_FOUND)
        target_link_libraries(roift_gui PRIVATE ${X11_LIBRARIES})
    endif()
endif()

if(WIN32)
    set(ITK_DLL_DIR "${ITK_INSTALL_DIR}/bin")
    set(QT_DIR "${CMAKE_BINARY_DIR}/deps/qt") 
    set(QT_DLL_DIR "${QT_DIR}/bin")
    set(QT_PLUGIN_DIR "${QT_DIR}/plugins")

    set(DEST_BIN_DIR "$<TARGET_FILE_DIR:roift_gui>/bin")
    set(DEST_PLUGIN_DIR "$<TARGET_FILE_DIR:roift_gui>/plugins")

    file(GLOB ITK_RUNTIME_DLLS "${ITK_DLL_DIR}/*.dll")
    file(GLOB QT_RUNTIME_DLLS "${QT_DLL_DIR}/*.dll")

    foreach(_dll IN LISTS ITK_RUNTIME_DLLS QT_RUNTIME_DLLS)
        add_custom_command(TARGET roift_gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_BIN_DIR}"
            COMMAND ${CMAKE_COMMAND} -E echo "Copying runtime DLL ${_dll} -> ${DEST_BIN_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "${DEST_BIN_DIR}"
            VERBATIM
        )
    endforeach()

    if(EXISTS "${QT_PLUGIN_DIR}")
        set(QT_PLUGIN_SUBDIRS platforms imageformats styles)
        foreach(_subdir IN LISTS QT_PLUGIN_SUBDIRS)
            if(EXISTS "${QT_PLUGIN_DIR}/${_subdir}")
                add_custom_command(TARGET roift_gui POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_PLUGIN_DIR}/${_subdir}"
                    COMMAND ${CMAKE_COMMAND} -E echo "Copying Qt plugins ${_subdir} -> ${DEST_PLUGIN_DIR}/${_subdir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${QT_PLUGIN_DIR}/${_subdir}" "${DEST_PLUGIN_DIR}/${_subdir}"
                    VERBATIM
                )
            endif()
        endforeach()
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/_cmake_includes/run_roift_gui.bat" "@echo off\r\nset PATH=%~dp0bin;%PATH%\r\n\"%~dp0roift_gui.exe\" %*\r\n")
    add_custom_command(TARGET roift_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/_cmake_includes/run_roift_gui.bat" "$<TARGET_FILE_DIR:roift_gui>/run_roift_gui.bat"
        VERBATIM
    )
endif()