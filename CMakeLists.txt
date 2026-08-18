cmake_minimum_required(VERSION 3.16)
# The release line is v1.1.x; v1.0.0/v1.0.1 are the prebuilt-vcpkg bundles.
# .github/workflows/release.yml bumps the patch here on every merge to main.
project(roift_gui VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

option(BUILD_ROIFT_GUI "Build the Qt/VTK/ITK GUI application" ON)

# Windows instalation
#
# Must run before add_subdirectory(roift): roift/CMakeLists.txt calls
# find_package(ZLIB REQUIRED), and without the prefix path set up first that
# fails outright on a toolchain-less configure.
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

# Include ROIFT (gft library + oiftrelax + experiments) — always built
add_subdirectory(roift)

if(BUILD_ROIFT_GUI)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# build output folder. Set cmake -S . -B build -DROIFT_COPY_RUNTIME=OFF to avoid copying files on every --build.
option(ROIFT_COPY_RUNTIME "Copy Qt plugins and vcpkg runtime DLLs to the build output folder" ON)

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
    NAMES windeployqt windeployqt6
    HINTS "${_qt_bin_dir}"
    NO_DEFAULT_PATH
  )

  # Not fatal. vcpkg only builds the tool when qtbase's opt-in "windeployqt"
  # feature is requested (see vcpkg.json), and a prefix without it can still
  # produce a working build — the vcpkg toolchain deploys dependent DLLs itself,
  # and cmake/Packaging.cmake takes Qt's plugins from the prefix either way.
  if(NOT WINDEPLOYQT_EXECUTABLE)
    message(WARNING "windeployqt not found (Qt bin dir: ${_qt_bin_dir}); "
                    "the build tree will not get a Qt runtime copied beside the exe")
  else()
    message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
  endif()
endif()

# Find ITK
find_package(ITK REQUIRED)
add_compile_definitions(HAVE_ITK=1)

# Directory scope so the test targets that reuse src/*.cpp see it too. src/Version.h
# is the only place that reads it; nothing in src/ spells a version out.
add_compile_definitions(ROIFT_GUI_VERSION="${PROJECT_VERSION}")

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

# resources/roift_gui.qrc carries the window/taskbar icon (AUTORCC compiles it in).
add_executable(roift_gui ${SRC_FILES} resources/roift_gui.qrc)

if(WIN32)
  # Exe icon + version metadata, and the GUI subsystem so no console window
  # opens behind the app. main.cpp routes --help/--version to a message box
  # when there is no console to print to.
  configure_file(packaging/windows/roift_gui.rc.in
                 "${CMAKE_CURRENT_BINARY_DIR}/roift_gui.rc" @ONLY)
  target_sources(roift_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/roift_gui.rc")
  target_include_directories(roift_gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows")
  set_target_properties(roift_gui PROPERTIES WIN32_EXECUTABLE ON)
endif()

target_include_directories(roift_gui PRIVATE src)
target_link_libraries(roift_gui
  PRIVATE
    Qt6::Widgets
    ${ITK_LIBRARIES}
    ${VTK_LIBRARIES}
)

if(TARGET Qt6::Svg)
  target_link_libraries(roift_gui PRIVATE Qt6::Svg)
  target_compile_definitions(roift_gui PRIVATE ROIFT_HAS_QT_SVG=1)
else()
  message(WARNING "Qt6 Svg module not found. NIfTI icon buttons will use non-SVG fallbacks.")
endif()

# Off by default: these targets are only needed to run the self-tests.
option(BUILD_ROIFT_TESTS "Build the import probe and path-helper tests" OFF)
if(BUILD_ROIFT_TESTS)
  enable_testing()

  add_executable(ui_paths_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_paths_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UiUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RangeSlider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
  )
  target_include_directories(ui_paths_test PRIVATE src)
  target_link_libraries(ui_paths_test PRIVATE Qt6::Widgets)
  add_test(NAME ui_paths COMMAND ui_paths_test)

  add_executable(wheel_guard_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/wheel_guard_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
  )
  target_include_directories(wheel_guard_test PRIVATE src)
  target_link_libraries(wheel_guard_test PRIVATE Qt6::Widgets)
  add_test(NAME wheel_guard COMMAND wheel_guard_test)
  # Needs a QApplication but no display.
  set_tests_properties(wheel_guard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Drives the real window (mask list, overlay blend), so it needs the whole
  # application except its entry point.
  set(GUI_TEST_SOURCES ${SRC_FILES})
  list(REMOVE_ITEM GUI_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
  add_executable(mask_overlay_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/mask_overlay_test.cpp
    ${GUI_TEST_SOURCES}
  )
  target_include_directories(mask_overlay_test PRIVATE src)
  target_link_libraries(mask_overlay_test PRIVATE Qt6::Widgets ${ITK_LIBRARIES} ${VTK_LIBRARIES})
  if(TARGET Qt6::Svg)
    target_link_libraries(mask_overlay_test PRIVATE Qt6::Svg)
    target_compile_definitions(mask_overlay_test PRIVATE ROIFT_HAS_QT_SVG=1)
  endif()
  add_test(NAME mask_overlay COMMAND mask_overlay_test)
  # Builds a window and reads composed slices back; no display needed.
  set_tests_properties(mask_overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(npz_import_probe
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/npz_import_probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NiftiImage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NpzVolume.cpp
  )
  target_include_directories(npz_import_probe PRIVATE src)
  target_link_libraries(npz_import_probe PRIVATE ${ITK_LIBRARIES})
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME npz_import
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/npz_import_selftest.py
        $<TARGET_FILE:npz_import_probe>
    )
    # The script exits 77 when numpy/SimpleITK are missing rather than failing.
    set_tests_properties(npz_import PROPERTIES SKIP_RETURN_CODE 77)
  else()
    message(STATUS "BUILD_ROIFT_TESTS=ON but no Python3 interpreter: npz_import test not registered")
  endif()

  # wheel_guard and mask_overlay run with QT_QPA_PLATFORM=offscreen. Qt aborts
  # at startup if the requested platform plugin is absent, and windeployqt
  # stages only the native one — so on Windows the offscreen plugin has to be
  # put beside the test binaries. Without it the tests do not fail, they hang:
  # the aborting process sits on a Windows error dialog until CI's job limit
  # kills the whole run.
  if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
    foreach(_offscreen_consumer wheel_guard_test mask_overlay_test)
      add_custom_command(TARGET ${_offscreen_consumer} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${_offscreen_consumer}>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
          "$<TARGET_FILE_DIR:${_offscreen_consumer}>/platforms/"
      )
    endforeach()
  endif()
endif()

if(DEFINED _vcpkg_root AND _vcpkg_root)
  target_link_directories(roift_gui PRIVATE
    "$<$<CONFIG:Debug>:${_vcpkg_root}/debug/lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${_vcpkg_root}/lib>"
  )
endif()

set_target_properties(roift_gui PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
)

# Deploy Qt runtime files (DLLs and plugins)
if(WIN32 AND WINDEPLOYQT_EXECUTABLE AND ROIFT_COPY_RUNTIME)
  add_custom_command(TARGET roift_gui POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
      --no-translations
      --no-system-d3d-compiler
      --no-opengl-sw
      "$<TARGET_FILE:roift_gui>"
    COMMENT "Running windeployqt to deploy Qt runtime files..."
  )
endif()

include(cmake/Packaging.cmake)

else() # BUILD_ROIFT_GUI=OFF
  message(STATUS "BUILD_ROIFT_GUI=OFF: skipping Qt/VTK/ITK GUI build")
endif() # BUILD_ROIFT_GUI
