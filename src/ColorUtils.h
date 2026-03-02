#pragma once

#include <QColor>
#include <algorithm>

inline QColor colorForLabel(int lbl)
{
    int v = std::max(0, std::min(255, lbl));
    if (v == 0)
        return QColor(200, 200, 200);
    // 1 = left lung, 2 = right lung, 3 = trachea
    if (v == 1)
        return QColor(70, 170, 255); // blue-cyan
    if (v == 2)
        return QColor(80, 220, 120); // green
    if (v == 3)
        return QColor(255, 90, 70); // red-orange
    const int MOD = 251;
    int r = (v * 67) % MOD;
    int g = (v * 131) % MOD;
    int b = (v * 199) % MOD;
    int sr = (r * 255) / MOD;
    int sg = (g * 255) / MOD;
    int sb = (b * 255) / MOD;
    return QColor(sr, sg, sb);
}
