// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <gtest/gtest.h>
#include <QApplication>

#include <QColor>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

struct PlotPoint
{
    float time;
    float value;
};

struct WeightedPoint
{
    float time;
    float value;
    float weight;
};

struct LODCurve
{
    std::vector<PlotPoint> data{};
    float min_interval = 1;

    void set(const std::vector<WeightedPoint>& points)
    {
        for (auto& point : points) data.emplace_back(PlotPoint{point.time, point.value});
    }

    float search(float time) const
    {
        if (data.empty()) return 0;
        if (data.back().time < time) return 0;

        auto it =
            std::lower_bound(data.begin(), data.end(), time, [](const PlotPoint& a, float tg) { return a.time < tg; });

        if (it == data.begin())
        {
            if (data.at(0).time > time) return 0;
            return data.at(0).value;
        }
        if (it == data.end()) return 0;

        return (it - 1)->value;
    }
};

const int MAX_LODS = 20;
const int LOD_THRESHOLD = 16;
const float MIN_PIXELS_PER_DOT = 1.5f;

struct PlotCurve
{
    std::string fullname;
    std::string shortname;
    QColor color;
    std::vector<LODCurve> lods = {};
    int lod = 0;
    bool disabled = false;
    double ymax = 1E-3;

    const LODCurve& get() const { return lods.at(lod); }

    void SetData(std::vector<WeightedPoint>&& data)
    {
        int n_lods = 0;
        ymax = 1E-3;
        for (auto& elem : data) ymax = std::max(ymax, 1.025 * static_cast<double>(elem.value));

        if (data.size() < 2) return;

        for (size_t i = 0; i < data.size() - 1; i++) data.at(i).weight = data.at(i + 1).time - data.at(i).time;
        data.back().weight = data.at(data.size() - 2).weight;

        while (n_lods < MAX_LODS && CreateLODs(n_lods, data)) n_lods++;
    }

    bool CreateLODs(int mip, std::vector<WeightedPoint>& points)
    {
        if (mip == 0)
        {
            lods.emplace_back(LODCurve{}).set(points);
            return true;
        }

        size_t og_size = points.size();
        if (og_size < LOD_THRESHOLD) return false;

        float min_interval = std::numeric_limits<float>::max();
        float cur_time = points.at(0).time;

        for (auto& point : points)
            if (point.time - cur_time >= 1.0f)
            {
                min_interval = std::min(min_interval, point.time - cur_time);
                cur_time = point.time;
            }

        while (points.size() > og_size / 2 + 2)
        {
            min_interval *= 1.42f;

            std::vector<WeightedPoint> newdata;
            newdata.reserve(points.size());
            newdata.emplace_back(points.at(0));

            for (size_t i = 1; i < points.size() - 1; i++)
            {
                auto& dplus = points.at(i + 1);
                auto& d0 = points.at(i);

                if (dplus.time - d0.time < min_interval)
                {
                    float weight = dplus.weight + d0.weight;
                    if (weight < 0.001f) continue;

                    float newtime = (dplus.time * dplus.weight + d0.time * d0.weight) / weight;
                    newdata.emplace_back(WeightedPoint{
                        newtime, (dplus.value * dplus.weight + d0.value * d0.weight) / weight, weight});
                    i += 1;
                }
                else
                    newdata.emplace_back(d0);
            }

            newdata.emplace_back(points.back());
            points = std::move(newdata);
        }

        if (points.size() < LOD_THRESHOLD) return false;

        auto& lodCurve = lods.emplace_back(LODCurve{});
        lodCurve.min_interval = min_interval;
        lodCurve.set(points);

        return true;
    }

    void UpdateLOD(float range, int width, bool bAuto)
    {
        if (!bAuto || lods.size() < 2)
        {
            lod = 0;
            return;
        }

        const float pixel_per_cycle = width / range;
        const float point_density_ratio = MIN_PIXELS_PER_DOT / pixel_per_cycle;
        lod = 1;

        while (lod < static_cast<int>(lods.size()) && lods.at(lod).min_interval < point_density_ratio) lod++;

        lod--;
    }
};

// ============================================================================
// LODCurve Tests
// ============================================================================

TEST(LODCurveTest, SetPopulatesDataFromWeightedPoints)
{
    LODCurve curve;
    std::vector<WeightedPoint> points = {
        {0.0f, 1.0f, 1.0f},
        {1.0f, 2.0f, 1.0f},
        {2.0f, 3.0f, 1.0f}
    };

    curve.set(points);

    EXPECT_EQ(curve.data.size(), 3);
    EXPECT_FLOAT_EQ(curve.data[0].time, 0.0f);
    EXPECT_FLOAT_EQ(curve.data[1].value, 2.0f);
}

TEST(LODCurveTest, SearchReturnsZeroForEmptyCurve)
{
    LODCurve curve;
    EXPECT_FLOAT_EQ(curve.search(1.0f), 0.0f);
}

TEST(LODCurveTest, SearchFindsValueAtTime)
{
    LODCurve curve;
    curve.data = {
        {0.0f, 10.0f},
        {1.0f, 20.0f},
        {2.0f, 30.0f}
    };

    EXPECT_FLOAT_EQ(curve.search(0.5f), 10.0f);
    EXPECT_FLOAT_EQ(curve.search(1.5f), 20.0f);
}

TEST(LODCurveTest, SearchReturnsZeroBeforeFirstPoint)
{
    LODCurve curve;
    curve.data = {
        {1.0f, 10.0f},
        {2.0f, 20.0f}
    };

    EXPECT_FLOAT_EQ(curve.search(0.0f), 0.0f);
}

TEST(LODCurveTest, SearchReturnsZeroAfterLastPoint)
{
    LODCurve curve;
    curve.data = {
        {0.0f, 10.0f},
        {1.0f, 20.0f}
    };

    EXPECT_FLOAT_EQ(curve.search(5.0f), 0.0f);
}

// ============================================================================
// PlotCurve Tests
// ============================================================================

TEST(PlotCurveTest, DefaultConstruction)
{
    PlotCurve curve;

    EXPECT_TRUE(curve.fullname.empty());
    EXPECT_TRUE(curve.lods.empty());
    EXPECT_EQ(curve.lod, 0);
    EXPECT_FALSE(curve.disabled);
}

TEST(PlotCurveTest, SetDataCalculatesYmax)
{
    PlotCurve curve;
    std::vector<WeightedPoint> data = {
        {0.0f, 10.0f, 1.0f},
        {1.0f, 50.0f, 1.0f},
        {2.0f, 30.0f, 1.0f}
    };

    curve.SetData(std::move(data));
    EXPECT_NEAR(curve.ymax, 50.0 * 1.025, 0.01);
}

TEST(PlotCurveTest, SetDataWithTooFewPoints)
{
    PlotCurve curve;
    std::vector<WeightedPoint> data = {
        {0.0f, 10.0f, 1.0f}
    };

    curve.SetData(std::move(data));
    EXPECT_TRUE(curve.lods.empty());
}

TEST(PlotCurveTest, SetDataCreatesAtLeastOneLOD)
{
    PlotCurve curve;
    std::vector<WeightedPoint> data;
    for (int i = 0; i < 100; i++) data.push_back({static_cast<float>(i), static_cast<float>(i % 10), 1.0f});

    curve.SetData(std::move(data));
    EXPECT_GE(curve.lods.size(), 1u);
}

TEST(PlotCurveTest, UpdateLODWithAutoFalse)
{
    PlotCurve curve;
    std::vector<WeightedPoint> data;
    for (int i = 0; i < 200; i++) data.push_back({static_cast<float>(i), static_cast<float>(i % 10), 1.0f});

    curve.SetData(std::move(data));
    curve.lod = 5;

    curve.UpdateLOD(100.0f, 100, false);
    EXPECT_EQ(curve.lod, 0);
}

TEST(PlotCurveTest, LODReducesDataSize)
{
    PlotCurve curve;
    std::vector<WeightedPoint> data;
    for (int i = 0; i < 500; i++) data.push_back({static_cast<float>(i), static_cast<float>(i % 50), 1.0f});

    curve.SetData(std::move(data));

    if (curve.lods.size() >= 2) EXPECT_LT(curve.lods[1].data.size(), curve.lods[0].data.size());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
