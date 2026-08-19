# Overlay of vcpkg's stock x64-windows triplet.
#
# vcpkg auto-loads it via vcpkg-configuration.json in manifest mode, so no
# extra flags are needed on the CMake command line.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Release-only dependencies. The app ships as Release, and building the debug
# variant of Qt6, VTK and ITK as well is what filled the CI runner's disk:
# vtk's x64-windows-dbg tree died with "fatal error C1085 ... No space left on
# device" after ~3h50m. Skipping it roughly halves both the disk footprint and
# the first build. To develop against a Debug app build, comment this out and
# reconfigure.
set(VCPKG_BUILD_TYPE release)

# CMake 4.x's Ninja generator emits a "re-run CMake during build" rule that
# never converges for some ports (liblzma/xz, tiff — both pulled in by ITK and
# VTK), so `ninja install` loops re-running CMake and aborts with "manifest
# 'build.ninja' still dirty after 100 tries, perhaps system time is not set".
# The clock is a red herring; the rule itself is the bug. vcpkg configures once
# then builds once, so regeneration is never needed. Remove once vcpkg/CMake
# ships a fix and the toolchains in use have picked it up.
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_SUPPRESS_REGENERATION=ON")
