// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CoverAccent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace linernotes::ui {

namespace {

struct Bucket {
    double weight = 0.0;
    double redWeightedSum = 0.0;
    double greenWeightedSum = 0.0;
    double blueWeightedSum = 0.0;
};

} // namespace

QColor coverAccentColor(const QImage &image)
{
    if (image.isNull()) {
        return { };
    }

    const QImage scaled = image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_ARGB32);
    const int totalPixels = scaled.width() * scaled.height();
    if (totalPixels == 0) {
        return { };
    }

    constexpr int kBucketCount = 18;
    constexpr double kBucketSpan = 360.0 / kBucketCount;

    std::array<Bucket, kBucketCount> buckets { };

    int validCount = 0;

    for (int y = 0; y < scaled.height(); ++y) {
        for (int x = 0; x < scaled.width(); ++x) {
            const QColor color = scaled.pixelColor(x, y);

            const auto h = static_cast<double>(color.hsvHueF());
            const auto s = static_cast<double>(color.hsvSaturationF());
            const auto v = static_cast<double>(color.valueF());

            // 忽略饱和度 < 0.25 或明度 < 0.2 的像素
            if (s < 0.25 || v < 0.2 || h < 0.0) {
                continue;
            }

            ++validCount;

            double hueDeg = h * 360.0;
            if (hueDeg >= 360.0 || hueDeg < 0.0) {
                hueDeg = std::fmod(hueDeg, 360.0);
                if (hueDeg < 0.0) {
                    hueDeg += 360.0;
                }
            }

            const int bucket
                = std::clamp(static_cast<int>(hueDeg / kBucketSpan), 0, kBucketCount - 1);
            const double weight = s * v;

            buckets.at(bucket).weight += weight;
            buckets.at(bucket).redWeightedSum += weight * static_cast<double>(color.redF());
            buckets.at(bucket).greenWeightedSum += weight * static_cast<double>(color.greenF());
            buckets.at(bucket).blueWeightedSum += weight * static_cast<double>(color.blueF());
        }
    }

    // 有效像素不足总数 5% 时返回无效 QColor()
    if (static_cast<double>(validCount) / static_cast<double>(totalPixels) < 0.05) {
        return { };
    }

    int bestBucket = 0;
    double maxWeight = -1.0;
    for (int i = 0; i < kBucketCount; ++i) {
        if (buckets.at(i).weight > maxWeight) {
            maxWeight = buckets.at(i).weight;
            bestBucket = i;
        }
    }

    if (maxWeight <= 0.0 || buckets.at(bestBucket).weight <= 0.0) {
        return { };
    }

    const double totalBucketWeight = buckets.at(bestBucket).weight;
    const double r = buckets.at(bestBucket).redWeightedSum / totalBucketWeight;
    const double g = buckets.at(bestBucket).greenWeightedSum / totalBucketWeight;
    const double b = buckets.at(bestBucket).blueWeightedSum / totalBucketWeight;

    return QColor::fromRgbF(static_cast<float>(std::clamp(r, 0.0, 1.0)),
        static_cast<float>(std::clamp(g, 0.0, 1.0)), static_cast<float>(std::clamp(b, 0.0, 1.0)));
}

} // namespace linernotes::ui
