#pragma once

#include <QColor>
#include <algorithm>

inline QColor colorForLabel(int lbl)
{
    int v = std::max(0, std::min(255, lbl));
    if (v == 0)
        return QColor(200, 200, 200);
    const int MOD = 251;
    int r = (v * 67) % MOD;
    int g = (v * 131) % MOD;
    int b = (v * 199) % MOD;
    int sr = (r * 255) / MOD;
    int sg = (g * 255) / MOD;
    int sb = (b * 255) / MOD;
    return QColor(sr, sg, sb);
}
