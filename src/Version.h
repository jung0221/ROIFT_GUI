#pragma once

/**
 * Version.h — the single place that reads the version CMake stamped in.
 *
 * CMakeLists.txt defines ROIFT_GUI_VERSION from project(... VERSION ...) at
 * directory scope. Nothing else in src/ spells a version out, so bumping
 * project() is the whole change; --version, the About box, the Windows exe
 * resource and every package file name follow from it.
 */

#ifndef ROIFT_GUI_VERSION
#error "ROIFT_GUI_VERSION is not defined — configure through CMakeLists.txt"
#endif

#include <QString>

namespace Version
{

inline QString string()
{
    return QStringLiteral(ROIFT_GUI_VERSION);
}

} // namespace Version
