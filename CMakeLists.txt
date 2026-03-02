cmake_minimum_required(VERSION 3.16)
project(roift_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# build output folder. Set cmake -S . -B build -DROIFT_COPY_RUNTIME=OFF to avoid copying files on every --build.
option(ROIFT_COPY_RUNTIME "Copy Qt plugins and vcpkg runtime DLLs to the build output folder" ON)

# Windows instalation
if(WIN32)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_vcpkg_candidate_1 "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows")
    set(_vcpkg_candidate_2 "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/x64-windows")
    set(_vcpkg_root "")
    if(EXISTS "${_vcpkg_candidate_1}")
      set(_vcpkg_root "${_vcpkg_candidate_1}")
    elseif(EXISTS "${_vcpkg_candidate_2}")
      set(_vcpkg_root "${_vcpkg_candidate_2}")
    endif()

    if(_vcpkg_root)
      message(STATUS "Using local vcpkg installed root: ${_vcpkg_root}")
      list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}")
      list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}/share")
      list(APPEND CMAKE_INCLUDE_PATH "${_vcpkg_root}/include")
      list(APPEND CMAKE_LIBRARY_PATH "${_vcpkg_root}/lib" "${_vcpkg_root}/bin")
      file(GLOB_RECURSE _qt6_config_files RELATIVE "${_vcpkg_root}" "${_vcpkg_root}/*Qt6Config.cmake")
      if(_qt6_config_files)
        list(GET _qt6_config_files 0 _qt6_first)
        get_filename_component(_qt6_dir "${_vcpkg_root}/${_qt6_first}" DIRECTORY)
        set(Qt6_DIR "${_qt6_dir}" CACHE PATH "Qt6_DIR set from local vcpkg_installed (detected)")
        message(STATUS "Detected Qt6 config at ${Qt6_DIR}")
      else()
        if(EXISTS "${_vcpkg_root}/share/qt6" OR EXISTS "${_vcpkg_root}/share/Qt6")
          list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}/lib/cmake")
          message(STATUS "Appended ${_vcpkg_root}/lib/cmake to CMAKE_PREFIX_PATH for Qt/other packages")
        endif()
      endif()
    else()
      message(STATUS "No local vcpkg_installed/x64-windows found. If you rely on vcpkg, provide vcpkg.cmake via -DCMAKE_TOOLCHAIN_FILE or supply system Qt/VTK.")
    endif()
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Try enabling Qt SVG only when it is available in the same Qt installation
# used for Qt6::Widgets, avoiding mismatched modules from other prefixes.
if(DEFINED Qt6_DIR)
  set(_qt_svg_dir "${Qt6_DIR}/../Qt6Svg")
  if(EXISTS "${_qt_svg_dir}/Qt6SvgConfig.cmake")
    find_package(Qt6Svg QUIET PATHS "${Qt6_DIR}/.." NO_DEFAULT_PATH)
    message(WARNING "Qt6SvgConfig.cmake found")
  else()
    message(WARNING "Qt6SvgConfig.cmake not found")
  endif()
endif()

if(WIN32)
  get_target_property(_qt_qmake_exe Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${_qt_bin_dir}"
    NO_DEFAULT_PATH
  )

  if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found (Qt bin dir: ${_qt_bin_dir})")
  else()
    message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
  endif()
endif()

# Find ITK
find_package(ITK REQUIRED)
add_compile_definitions(HAVE_ITK=1)

message(STATUS "Selected Qt major version: 6 (Qt6 found and required)")

# Include ITK
if (DEFINED ITK_USE_FILE)
  include(${ITK_USE_FILE})
elseif (ITK_USE_FILE)
  include(${ITK_USE_FILE})
endif()

file(GLOB SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
#     list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp")
# endif()

# Find VTK
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  FiltersCore
  FiltersGeneral
  FiltersGeometry
  FiltersModeling
  IOImage
  InteractionStyle
  RenderingCore
  RenderingFreeType
  RenderingOpenGL2
  GUISupportQt
)


# Include ROIFT
add_subdirectory(roift)
add_executable(roift_gui ${SRC_FILES})



target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    Qt6::Widgets
    ${ITK_LIBRARIES}
    ${VTK_LIBRARIES}
)

if(TARGET Qt6::Svg)
  target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Svg)
  target_compile_definitions(${PROJECT_NAME} PRIVATE ROIFT_HAS_QT_SVG=1)
else()
  message(WARNING "Qt6 Svg module not found. NIfTI icon buttons will use non-SVG fallbacks.")
endif()

if(DEFINED _vcpkg_root AND _vcpkg_root)
  # Ensure the vcpkg library directories are on the linker's search path so
  # that plain-named libs (e.g. minc2.lib) provided by vcpkg are found.
  target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${_vcpkg_root}/debug/lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${_vcpkg_root}/lib>"
  )
endif()

# For UIC/MOC if needed later
set_target_properties(${PROJECT_NAME} PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
)

# Deploy Qt runtime files (DLLs and plugins)
if(WIN32 AND WINDEPLOYQT_EXECUTABLE AND ROIFT_COPY_RUNTIME)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
      --no-translations
      --no-system-d3d-compiler
      --no-opengl-sw
      "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Running windeployqt to deploy Qt runtime files..."
  )
endif()
