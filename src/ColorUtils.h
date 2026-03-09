#pragma once

#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

inline QColor colorForLabel(int lbl)
{
    auto perceptualColorDistance = [](const QColor &a, const QColor &b) -> double
    {
        const double ar = a.redF();
        const double ag = a.greenF();
        const double ab = a.blueF();
        const double br = b.redF();
        const double bg = b.greenF();
        const double bb = b.blueF();

        const double dr = ar - br;
        const double dg = ag - bg;
        const double db = ab - bb;
        const double rMean = 0.5 * (ar + br);
        const double lumaA = 0.2126 * ar + 0.7152 * ag + 0.0722 * ab;
        const double lumaB = 0.2126 * br + 0.7152 * bg + 0.0722 * bb;
        const double dLuma = lumaA - lumaB;

        return (2.0 + rMean) * dr * dr +
               4.0 * dg * dg +
               (3.0 - rMean) * db * db +
               2.5 * dLuma * dLuma;
    };

    auto buildLabelPalette = [&]() -> std::vector<QColor>
    {
        std::vector<QColor> palette(256, QColor(200, 200, 200));
        palette[0] = QColor(200, 200, 200);
        palette[1] = QColor(70, 170, 255);  // blue-cyan
        palette[2] = QColor(80, 220, 120);  // green
        palette[3] = QColor(255, 90, 70);   // red-orange

        std::vector<QColor> selected = {palette[0], palette[1], palette[2], palette[3]};
        std::vector<QColor> candidates;
        candidates.reserve(432);

        constexpr std::array<int, 36> hues = {
            0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110,
            120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230,
            240, 250, 260, 270, 280, 290, 300, 310, 320, 330, 340, 350};
        constexpr std::array<int, 4> saturations = {240, 210, 180, 150};
        constexpr std::array<int, 3> values = {245, 225, 205};

        for (int hue : hues)
        {
            for (int saturation : saturations)
            {
                for (int value : values)
                {
                    const QColor candidate = QColor::fromHsv(hue, saturation, value);
                    const double luma = 0.2126 * candidate.redF() +
                                        0.7152 * candidate.greenF() +
                                        0.0722 * candidate.blueF();
                    if (luma < 0.22 || luma > 0.82)
                        continue;
                    candidates.push_back(candidate);
                }
            }
        }

        for (int label = 4; label < static_cast<int>(palette.size()); ++label)
        {
            int bestIndex = -1;
            double bestScore = -1.0;

            for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
            {
                const QColor &candidate = candidates[static_cast<size_t>(i)];
                double minDistance = std::numeric_limits<double>::max();
                for (const QColor &existing : selected)
                    minDistance = std::min(minDistance, perceptualColorDistance(candidate, existing));

                if (minDistance > bestScore)
                {
                    bestScore = minDistance;
                    bestIndex = i;
                }
            }

            if (bestIndex < 0)
            {
                const double golden = 0.6180339887498949;
                const double hueUnit = std::fmod(label * golden, 1.0);
                palette[static_cast<size_t>(label)] = QColor::fromHsvF(hueUnit, 0.72, 0.92);
                continue;
            }

            const QColor chosen = candidates[static_cast<size_t>(bestIndex)];
            palette[static_cast<size_t>(label)] = chosen;
            selected.push_back(chosen);
            candidates.erase(candidates.begin() + bestIndex);
        }

        return palette;
    };

    static const std::vector<QColor> palette = buildLabelPalette();
    const int v = std::max(0, std::min(255, lbl));
    return palette[static_cast<size_t>(v)];
}
