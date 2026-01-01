#pragma once

#include <QColor>
#include <algorithm>

inline QColor colorForLabel(int lbl)
{
    int v = std::max(1, std::min(254, lbl));
    const int MOD = 251;
    int r = (v * 67) % MOD;
    int g = (v * 131) % MOD;
    int b = (v * 199) % MOD;
    int sr = (r * 255) / MOD;
    int sg = (g * 255) / MOD;
    int sb = (b * 255) / MOD;
    return QColor(sr, sg, sb);
}
