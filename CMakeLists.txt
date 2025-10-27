cmake_minimum_required(VERSION 3.16)
project(roift_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AUTOMOC/AUTOUIC for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer Qt6 if available, fall back to Qt5
find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    set(QT_WIDGETS_LIB Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_WIDGETS_LIB Qt5::Widgets)
endif()

message(STATUS "Attempting to find ZLIB")
find_package(ZLIB REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

message(STATUS "Attempting to find ITK")
find_package(ITK CONFIG REQUIRED)
if (ITK_FOUND)
    include(${ITK_USE_FILE})
    message(STATUS "Found ITK: ${ITK_VERSION}")
else()
    message(FATAL_ERROR "ITK was not found on this system. Install ITK or set ITK_DIR to the installation path.")
endif()

find_package(CURL REQUIRED)
message(STATUS "Found CURL: ${CURL_VERSION_STRING}")

add_executable(roift_gui ${SRC_FILES})
target_compile_definitions(roift_gui PRIVATE HAVE_ITK=1)
target_link_libraries(roift_gui PRIVATE ${QT_WIDGETS_LIB} ZLIB::ZLIB ${ITK_LIBRARIES} CURL::libcurl)


if (UNIX)
    # ensure we get X11 on Linux systems (Qt usually handles this)
    find_package(X11 QUIET)
    if (X11_FOUND)
        target_link_libraries(roift_gui PRIVATE ${X11_LIBRARIES})
    endif()
endif()

if (USE_ITK)
    message(STATUS "CMake project configured for roift_gui (HAVE_ITK=1)")
else()
    message(STATUS "CMake project configured for roift_gui (HAVE_ITK=0)")
endif()
