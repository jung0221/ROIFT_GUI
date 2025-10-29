cmake_minimum_required(VERSION 3.16)
project(roift_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AUTOMOC/AUTOUIC for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer Qt6 if available, fall back to Qt5
include(${CMAKE_CURRENT_LIST_DIR}/src/qtdownload.cmake)

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    message(STATUS "Found Qt6")
    set(QT_WIDGETS_LIB Qt6::Widgets)
else()
    # qtdownload.cmake was included above and will attempt to populate CMAKE_PREFIX_PATH
    # with a Qt6 distribution. If that failed or Qt6 is still not found, abort.
    message(FATAL_ERROR "Qt6 not found. Aborting.")
endif()

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
    list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
endif()

# Option to automatically fetch/build ITK if missing
option(USE_ITK "Automatically fetch/build ITK if not found" ON)

# Allow the user to preconfigure an ITK install prefix (useful when ITK was built separately)
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
if (TARGET ZLIB::ZLIB)
    set(ZLIB_LINK_TARGET ZLIB::ZLIB)
elseif (TARGET zlib)
    set(ZLIB_LINK_TARGET zlib)
else()
    find_package(ZLIB QUIET)
    include(${CMAKE_CURRENT_LIST_DIR}/src/zlibdownload.cmake)
    fetch_zlib_if_missing()
    if (TARGET ZLIB::ZLIB)
        set(ZLIB_LINK_TARGET ZLIB::ZLIB)
    elseif (TARGET zlib)
        set(ZLIB_LINK_TARGET zlib)
    else()
        set(ZLIB_LINK_TARGET "") # nothing found
    endif()
endif()

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
    set(QT_DIR "${CMAKE_BINARY_DIR}/qt")  # root do Qt baixado pelo qtdownload.cmake
    set(QT_DLL_DIR "${QT_DIR}/bin")
    set(QT_PLUGIN_DIR "${QT_DIR}/plugins")

    file(GLOB ITK_RUNTIME_DLLS "${ITK_DLL_DIR}/*.dll")
    file(GLOB QT_RUNTIME_DLLS "${QT_DLL_DIR}/*.dll")

    foreach(_dll IN LISTS ITK_RUNTIME_DLLS QT_RUNTIME_DLLS)
        if(TARGET roift_gui)
            add_custom_command(TARGET roift_gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:roift_gui>
                COMMENT "Copying runtime DLL ${_dll} -> $<TARGET_FILE_DIR:roift_gui>")
        endif()
    endforeach()

    # Copia pastas de plugins do Qt (platforms, imageformats, styles) para ao lado do exe
    if(EXISTS "${QT_PLUGIN_DIR}")
        set(QT_PLUGIN_SUBDIRS platforms imageformats styles)
        foreach(_subdir IN LISTS QT_PLUGIN_SUBDIRS)
            if(EXISTS "${QT_PLUGIN_DIR}/${_subdir}")
                if(TARGET roift_gui)
                    add_custom_command(TARGET roift_gui POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory "${QT_PLUGIN_DIR}/${_subdir}" $<TARGET_FILE_DIR:roift_gui>/${_subdir}
                        COMMENT "Copying Qt plugins ${_subdir} -> $<TARGET_FILE_DIR:roift_gui>/${_subdir}")
                endif()
            endif()
        endforeach()
    endif()
endif()
if (USE_ITK)
    message(STATUS "CMake project configured for roift_gui (USE_ITK=ON)")
else()
    message(STATUS "CMake project configured for roift_gui (USE_ITK=OFF)")
endif()