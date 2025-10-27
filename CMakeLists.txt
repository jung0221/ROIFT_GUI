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

find_package(ZLIB REQUIRED)

# Optionally enable ITK. Default OFF to keep dependencies minimal.
option(USE_ITK "Enable ITK for robust NIfTI I/O and image processing" ON)
if (NOT USE_ITK)
    message(FATAL_ERROR "ITK is required for this project. Re-run CMake with -DUSE_ITK=ON.")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(roift_gui ${SRC_FILES})

# Build the ROIFT standalone executable (oiftrelax) located in the roift/ subdirectory.
# This produces an additional binary named 'oiftrelax' alongside 'roift_gui'.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/roift)

# Build without ITK; use bundled fallback NIfTI reader in code
message(STATUS "Attempting to find ITK (USE_ITK=ON)")
find_package(ITK REQUIRED)
if (ITK_FOUND)
    include(${ITK_USE_FILE})
    message(STATUS "Found ITK: ${ITK_VERSION}")
else()
    message(FATAL_ERROR "ITK was not found on this system. Install ITK or set ITK_DIR to the installation path.")
endif()

# ITK may have optional dependencies (HDF5 built with libcurl). Ensure libcurl is available for linking.
find_package(CURL QUIET)
if (NOT CURL_FOUND)
    message(WARNING "ITK found but libcurl (CURL) was not found; some ITK HDF5 features may be unavailable.")
else()
    message(STATUS "Found CURL: ${CURL_VERSION_STRING}")
endif()

target_compile_definitions(roift_gui PRIVATE HAVE_ITK=1)
if (CURL_FOUND)
    target_link_libraries(roift_gui PRIVATE ${QT_WIDGETS_LIB} ZLIB::ZLIB ${ITK_LIBRARIES} CURL::libcurl)
else()
    target_link_libraries(roift_gui PRIVATE ${QT_WIDGETS_LIB} ZLIB::ZLIB ${ITK_LIBRARIES})
endif()

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
