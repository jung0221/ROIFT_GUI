#pragma once

// The 2D raster formats opened through ITK's readers. Header-only and Qt-free, so that
// NiftiImage and UiUtils read the same list.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace raster
{

inline constexpr std::array<std::string_view, 6> kExtensions{".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"};

// True when text ends with one of kExtensions, in any letter case.
inline bool hasRasterExtension(const std::string &text)
{
    const auto sameLetter = [](char a, char b)
    { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
    for (std::string_view extension : kExtensions)
        if (text.size() >= extension.size() &&
            std::equal(extension.begin(), extension.end(),
                       text.end() - static_cast<std::ptrdiff_t>(extension.size()), sameLetter))
            return true;
    return false;
}

} // namespace raster
