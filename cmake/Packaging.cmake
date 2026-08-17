# Install rules and CPack configuration for roift_gui.
#
# Layouts produced by `cmake --install`:
#   Linux:   <prefix>/bin/roift_gui                  (RPATH: $ORIGIN/../lib)
#            <prefix>/bin/oiftrelax, exp_*           (found via applicationDirPath)
#            <prefix>/share/applications|icons|metainfo
#            → the AppDir staging tree for packaging/linux/build-appimage.sh
#   Windows: <prefix>/bin/roift_gui.exe + the CLI exes + every runtime DLL and
#            Qt plugin the build step already placed beside the exe
#            → consumed by CPack NSIS/ZIP (`cpack -C Release` in the build dir)

# ─── Binaries ───────────────────────────────────────────────────────────────
# SegmentationRunner resolves the ROIFT executables relative to
# QCoreApplication::applicationDirPath(), so they ship in the same bin/ as the
# GUI. oiftrelax_gpu is absent unless CUDA was found; the experiments only when
# roift/experiments exists.
install(TARGETS roift_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

foreach(_cli oiftrelax oiftrelax_gpu
             exp_gradient_weight exp_gaussian_rbf_relax exp_coarse_to_fine
             exp_26connect exp_geodesic_seeds)
  if(TARGET ${_cli})
    install(TARGETS ${_cli} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endforeach()

# ─── Linux: desktop integration ─────────────────────────────────────────────
if(UNIX AND NOT APPLE)
  # Nothing is bundled into <prefix>/lib by the install itself — linuxdeploy
  # fills it while building the AppImage — but the binary has to be able to
  # find those libs once it does.
  set_target_properties(roift_gui PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

  install(FILES packaging/linux/roift_gui.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/icons/roift_gui.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES resources/icons/roift_gui-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME roift_gui.png)
  install(FILES packaging/linux/roift_gui.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()

# ─── Windows: runtime DLLs + Qt plugins next to the exe ─────────────────────
if(WIN32)
  # windeployqt (POST_BUILD, see CMakeLists.txt) and vcpkg's applocal deployment
  # have already put every DLL and Qt plugin subtree in the target directory.
  # Installing it wholesale keeps one source of truth for what the app needs.
  if(NOT ROIFT_COPY_RUNTIME)
    message(WARNING "ROIFT_COPY_RUNTIME=OFF: packages will be missing Qt/VTK/ITK DLLs")
  endif()

  # Configured without a toolchain file, CMakeLists.txt falls back to a local
  # vcpkg_installed tree (the layout scripts/restore_prebuilt.cmd unpacks). That
  # path gets no app-local deployment — windeployqt covers Qt and nothing covers
  # VTK or ITK — so take the DLLs and Qt plugins from the prefix itself. Release
  # only; packaging a debug build is not supported.
  if(DEFINED _vcpkg_root AND _vcpkg_root)
    install(DIRECTORY "${_vcpkg_root}/bin/"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      FILES_MATCHING PATTERN "*.dll")

    foreach(_qt_plugin_dir platforms styles imageformats iconengines tls)
      if(EXISTS "${_vcpkg_root}/Qt6/plugins/${_qt_plugin_dir}")
        install(DIRECTORY "${_vcpkg_root}/Qt6/plugins/${_qt_plugin_dir}"
          DESTINATION ${CMAKE_INSTALL_BINDIR}
          FILES_MATCHING PATTERN "*.dll")
      endif()
    endforeach()
  endif()
  # The excludes only bite under a single-config generator, where the target
  # directory is the build root: without them the whole vcpkg tree (debug DLLs
  # included) would be swept in. Under Visual Studio the target dir is
  # build/Release and already holds nothing else.
  install(DIRECTORY "$<TARGET_FILE_DIR:roift_gui>/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILES_MATCHING
      PATTERN "*.dll"
      PATTERN "CMakeFiles" EXCLUDE
      PATTERN "vcpkg_installed" EXCLUDE)
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME})

# ─── CPack ──────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "roift_gui")
set(CPACK_PACKAGE_VENDOR "jung0221")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Seed-based ROIFT segmentation for 3D medical images")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ROIFT GUI")
set(CPACK_PACKAGE_EXECUTABLES "roift_gui" "ROIFT GUI")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES TRUE)

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_PACKAGE_FILE_NAME "roift_gui-${PROJECT_VERSION}-win64")

  set(CPACK_NSIS_DISPLAY_NAME "ROIFT GUI")
  set(CPACK_NSIS_PACKAGE_NAME "ROIFT GUI")
  set(CPACK_NSIS_MUI_ICON    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/roift_gui.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/roift_gui.ico")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\roift_gui.exe")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  # Start-menu/desktop shortcuts point into bin/ where the exe lives.
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
  set(CPACK_CREATE_DESKTOP_LINKS "roift_gui")
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_PACKAGE_FILE_NAME "roift_gui-${PROJECT_VERSION}-linux-x86_64")
endif()

# Only when roift_gui is the project being built. Added as a subdirectory of a
# larger tree (ctsegmentation does this), the install rules above still apply,
# but CPack would write its config into the parent's build dir and claim to
# package that project. Releases are cut from this repo, standalone.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CPack)
endif()
