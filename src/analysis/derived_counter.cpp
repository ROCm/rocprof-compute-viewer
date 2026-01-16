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

#include "derived_counter.h"
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace DerivedCounter
{

// ============================================================================
// Axis utilities
// ============================================================================

Axis axisFromString(const std::string& name)
{
    if (name == "XCC" || name == "xcc" || name == "0") return Axis::XCC;
    if (name == "SE" || name == "se" || name == "1") return Axis::SE;
    if (name == "CU" || name == "cu" || name == "WGP" || name == "wgp" || name == "INSTANCE" || name == "instance" ||
        name == "2")
        return Axis::CU;
    if (name == "Time" || name == "time" || name == "TIME" || name == "SAMPLES" || name == "samples" || name == "3")
        return Axis::Time;
    if (name == "All" || name == "all" || name == "-1") return Axis::All;
    throw std::runtime_error("Unknown axis name: " + name);
}

std::string axisToString(Axis axis)
{
    switch (axis)
    {
        case Axis::XCC: return "XCC";
        case Axis::SE: return "SE";
        case Axis::CU: return "CU";
        case Axis::Time: return "TIME";
        case Axis::All: return "All";
    }
    return "Unknown";
}

// ============================================================================
// Shape implementation
// ============================================================================

size_t Shape::dimSize(Axis axis) const
{
    if (axis == Axis::All) return totalSize();
    return dim[static_cast<size_t>(axis)];
}

bool Shape::operator==(const Shape& other) const
{
    for (size_t i = 0; i < NUM_DIMS; ++i)
    {
        if (dim[i] != other.dim[i]) return false;
    }
    return true;
}

Shape Shape::reducedShape(const std::vector<Axis>& axes) const
{
    Shape result = *this;
    for (Axis axis : axes)
    {
        if (axis == Axis::All) return Shape(1, 1, 1, 1);
        result.dim[static_cast<size_t>(axis)] = 1;
    }
    return result;
}

bool Shape::areBroadcastable(const Shape& a, const Shape& b)
{
    for (size_t i = 0; i < NUM_DIMS; ++i)
    {
        if (a.dim[i] != b.dim[i] && a.dim[i] != 1 && b.dim[i] != 1) return false;
    }
    return true;
}

Shape Shape::broadcastShape(const Shape& a, const Shape& b)
{
    if (!areBroadcastable(a, b))
    {
        throw std::runtime_error("Shapes are not broadcastable: " + a.toString() + " vs " + b.toString());
    }
    Shape result;
    for (size_t i = 0; i < NUM_DIMS; ++i) result.dim[i] = std::max(a.dim[i], b.dim[i]);
    return result;
}

std::string Shape::toString() const
{
    return "[" + std::to_string(dim[0]) + "," + std::to_string(dim[1]) + "," + std::to_string(dim[2]) + "," +
           std::to_string(dim[3]) + "]";
}

// ============================================================================
// Tensor implementation
// ============================================================================

Tensor::Tensor() : m_shape(), m_data(1, 0.0) {}

Tensor::Tensor(float scalar) : m_shape(1, 1, 1, 1), m_data(1, scalar) {}

Tensor::Tensor(const Shape& shape, float fillValue) : m_shape(shape), m_data(shape.totalSize(), fillValue) {}

Tensor::Tensor(const Shape& shape, const std::vector<float>& data) : m_shape(shape), m_data(data)
{
    if (m_data.size() != m_shape.totalSize())
    {
        throw std::runtime_error(
            "Data size mismatch: expected " + std::to_string(m_shape.totalSize()) + ", got " +
            std::to_string(m_data.size())
        );
    }
}

float Tensor::scalar() const
{
    if (!isScalar()) throw std::runtime_error("Tensor is not a scalar");
    return m_data.at(0);
}

size_t Tensor::linearIndex(size_t xcc, size_t se, size_t cu, size_t sample) const
{
    // Row-major order: counter[xcc][se][cu][sample]
    return ((xcc * m_shape[1] + se) * m_shape[2] + cu) * m_shape[3] + sample;
}

void Tensor::multiIndex(size_t linear, size_t& xcc, size_t& se, size_t& cu, size_t& sample) const
{
    sample = linear % m_shape[3];
    linear /= m_shape[3];
    cu = linear % m_shape[2];
    linear /= m_shape[2];
    se = linear % m_shape[1];
    xcc = linear / m_shape[1];
}

float& Tensor::at(size_t xcc, size_t se, size_t cu, size_t sample)
{
    return m_data.at(linearIndex(xcc, se, cu, sample));
}

float Tensor::at(size_t xcc, size_t se, size_t cu, size_t sample) const
{
    return m_data.at(linearIndex(xcc, se, cu, sample));
}

// Template helper for reduction
template <typename ReduceOp> Tensor Tensor::reduceAxes(const std::vector<Axis>& axes, ReduceOp op, float identity) const
{
    // Check if we should reduce all axes
    bool reduceAllAxes = axes.empty();
    for (const auto& axis : axes)
    {
        if (axis == Axis::All)
        {
            reduceAllAxes = true;
            break;
        }
    }

    if (reduceAllAxes)
    {
        // Reduce all - return scalar
        float result = identity;
        size_t count = 0;
        for (float v : m_data)
        {
            result = op(result, v, count);
            count++;
        }
        return Tensor(result);
    }

    Shape newShape = m_shape.reducedShape(axes);
    Tensor result(newShape, identity);

    // Count tensor for mean calculation
    std::vector<size_t> counts(result.size(), 0);

    // Determine which axes to reduce (as a bool array)
    bool reduce[NUM_DIMS] = {false, false, false, false};
    for (Axis axis : axes)
    {
        if (axis == Axis::All)
        {
            for (size_t i = 0; i < NUM_DIMS; ++i) reduce[i] = true;
        }
        else
            reduce[static_cast<size_t>(axis)] = true;
    }

    // Iterate over all elements
    for (size_t xcc = 0; xcc < m_shape[0]; ++xcc)
    {
        for (size_t se = 0; se < m_shape[1]; ++se)
        {
            for (size_t cu = 0; cu < m_shape[2]; ++cu)
            {
                for (size_t sample = 0; sample < m_shape[3]; ++sample)
                {
                    size_t srcIdx = linearIndex(xcc, se, cu, sample);

                    // Compute destination indices (collapse reduced axes to 0)
                    size_t dstXcc = reduce[0] ? 0 : xcc;
                    size_t dstSE = reduce[1] ? 0 : se;
                    size_t dstCU = reduce[2] ? 0 : cu;
                    size_t dstSample = reduce[3] ? 0 : sample;

                    size_t dstIdx = result.linearIndex(dstXcc, dstSE, dstCU, dstSample);
                    result[dstIdx] = op(result[dstIdx], m_data.at(srcIdx), counts.at(dstIdx));
                    counts.at(dstIdx)++;
                }
            }
        }
    }

    return result;
}

Tensor Tensor::sum(const std::vector<Axis>& axes) const
{
    return reduceAxes(
        axes, [](float acc, float val, size_t) { return acc + val; }, 0.0
    );
}

Tensor Tensor::mean(const std::vector<Axis>& axes) const
{
    // First compute sum
    Tensor sumResult = sum(axes);

    // Compute the count for each reduced element
    size_t count = 1;
    for (Axis axis : axes)
    {
        if (axis == Axis::All)
        {
            count = m_shape.totalSize();
            break;
        }
        count *= m_shape.dimSize(axis);
    }

    // Divide by count
    for (float& v : sumResult.data()) v /= static_cast<float>(count);

    return sumResult;
}

Tensor Tensor::max(const std::vector<Axis>& axes) const
{
    return reduceAxes(
        axes,
        [](float acc, float val, size_t count) { return count == 0 ? val : std::max(acc, val); },
        -std::numeric_limits<float>::infinity()
    );
}

Tensor Tensor::min(const std::vector<Axis>& axes) const
{
    return reduceAxes(
        axes,
        [](float acc, float val, size_t count) { return count == 0 ? val : std::min(acc, val); },
        std::numeric_limits<float>::infinity()
    );
}

Tensor Tensor::sumAll() const { return sum({Axis::All}); }
Tensor Tensor::meanAll() const { return mean({Axis::All}); }
Tensor Tensor::maxAll() const { return max({Axis::All}); }
Tensor Tensor::minAll() const { return min({Axis::All}); }

Tensor Tensor::select(int64_t index, Axis axis) const
{
    size_t axisIdx = static_cast<size_t>(axis);
    int64_t axisSize = static_cast<int64_t>(m_shape.dimSize(axisIdx));

    // Handle negative indexing
    if (index < 0) index += axisSize;

    if (index < 0 || index >= axisSize)
    {
        throw std::runtime_error(
            "Select index " + std::to_string(index) + " out of range for axis " + axisToString(axis) + " with size " +
            std::to_string(axisSize)
        );
    }

    // Create result shape with selected axis reduced to 1
    Shape resultShape = m_shape;
    resultShape.dim[axisIdx] = 1;

    Tensor result(resultShape);

    for (size_t xcc = 0; xcc < resultShape[0]; ++xcc)
    {
        for (size_t se = 0; se < resultShape[1]; ++se)
        {
            for (size_t cu = 0; cu < resultShape[2]; ++cu)
            {
                for (size_t sample = 0; sample < resultShape[3]; ++sample)
                {
                    // Build source indices, replacing the selected axis with the index
                    size_t srcXcc = (axisIdx == 0) ? index : xcc;
                    size_t srcSE = (axisIdx == 1) ? index : se;
                    size_t srcCU = (axisIdx == 2) ? index : cu;
                    size_t srcSample = (axisIdx == 3) ? index : sample;

                    result.at(xcc, se, cu, sample) = at(srcXcc, srcSE, srcCU, srcSample);
                }
            }
        }
    }

    return result;
}

Tensor Tensor::selectRange(size_t start, size_t stop, size_t step, Axis axis) const
{
    size_t axisIdx = static_cast<size_t>(axis);
    size_t axisSize = m_shape.dimSize(axisIdx);

    if (start >= axisSize)
    {
        throw std::runtime_error(
            "Select range start " + std::to_string(start) + " out of range for axis " + axisToString(axis) +
            " with size " + std::to_string(axisSize)
        );
    }

    if (stop > axisSize)
    {
        throw std::runtime_error(
            "Select range stop " + std::to_string(stop) + " out of range for axis " + axisToString(axis) +
            " with size " + std::to_string(axisSize)
        );
    }

    if (step == 0) throw std::runtime_error("Select range step cannot be zero");

    if (start >= stop) throw std::runtime_error("Select range start must be less than stop");

    // Calculate the number of elements in the result
    size_t numSelected = 0;
    for (size_t i = start; i < stop; i += step) numSelected++;

    // Create result shape with the selected axis having numSelected elements
    Shape resultShape = m_shape;
    resultShape.dim[axisIdx] = numSelected;

    Tensor result(resultShape);

    // Iterate over all indices in the result tensor
    for (size_t xcc = 0; xcc < resultShape[0]; ++xcc)
    {
        for (size_t se = 0; se < resultShape[1]; ++se)
        {
            for (size_t cu = 0; cu < resultShape[2]; ++cu)
            {
                for (size_t sample = 0; sample < resultShape[3]; ++sample)
                {
                    // Compute the source index for the selected axis
                    size_t selectedIdx = 0;
                    switch (axisIdx)
                    {
                        case 0: selectedIdx = xcc; break;
                        case 1: selectedIdx = se; break;
                        case 2: selectedIdx = cu; break;
                        case 3: selectedIdx = sample; break;
                    }
                    size_t srcIdx = start + selectedIdx * step;

                    // Build source indices
                    size_t srcXcc = (axisIdx == 0) ? srcIdx : xcc;
                    size_t srcSE = (axisIdx == 1) ? srcIdx : se;
                    size_t srcCU = (axisIdx == 2) ? srcIdx : cu;
                    size_t srcSample = (axisIdx == 3) ? srcIdx : sample;

                    result.at(xcc, se, cu, sample) = at(srcXcc, srcSE, srcCU, srcSample);
                }
            }
        }
    }

    return result;
}

Tensor Tensor::remove(int index, Axis axis) const
{
    size_t axisIdx = static_cast<size_t>(axis);
    size_t axisSize = m_shape.dimSize(axisIdx);

    if (axisSize <= 1)
    {
        throw std::runtime_error(
            "Cannot remove from axis " + axisToString(axis) + " with size " + std::to_string(axisSize)
        );
    }

    // Handle negative indexing
    size_t actualIndex;
    if (index < 0)
    {
        int adjustedIndex = static_cast<int>(axisSize) + index;
        if (adjustedIndex < 0)
        {
            throw std::runtime_error(
                "Remove index " + std::to_string(index) + " out of range for axis " + axisToString(axis) +
                " with size " + std::to_string(axisSize)
            );
        }
        actualIndex = static_cast<size_t>(adjustedIndex);
    }
    else
    {
        actualIndex = static_cast<size_t>(index);
        if (actualIndex >= axisSize)
        {
            throw std::runtime_error(
                "Remove index " + std::to_string(index) + " out of range for axis " + axisToString(axis) +
                " with size " + std::to_string(axisSize)
            );
        }
    }

    // Create result shape with axis reduced by 1
    Shape resultShape = m_shape;
    resultShape.dim[axisIdx] = axisSize - 1;

    Tensor result(resultShape);

    for (size_t xcc = 0; xcc < resultShape[0]; ++xcc)
    {
        for (size_t se = 0; se < resultShape[1]; ++se)
        {
            for (size_t cu = 0; cu < resultShape[2]; ++cu)
            {
                for (size_t sample = 0; sample < resultShape[3]; ++sample)
                {
                    // Build source indices, skipping the removed index
                    size_t srcXcc = xcc;
                    size_t srcSE = se;
                    size_t srcCU = cu;
                    size_t srcSample = sample;

                    // Adjust the source index for the removed element
                    if (axisIdx == 0)
                        srcXcc = (xcc >= actualIndex) ? xcc + 1 : xcc;
                    else if (axisIdx == 1)
                        srcSE = (se >= actualIndex) ? se + 1 : se;
                    else if (axisIdx == 2)
                        srcCU = (cu >= actualIndex) ? cu + 1 : cu;
                    else if (axisIdx == 3)
                        srcSample = (sample >= actualIndex) ? sample + 1 : sample;

                    result.at(xcc, se, cu, sample) = at(srcXcc, srcSE, srcCU, srcSample);
                }
            }
        }
    }

    return result;
}

Tensor Tensor::delta(Axis axis) const
{
    size_t axisIdx = static_cast<size_t>(axis);
    size_t axisSize = m_shape.dimSize(axisIdx);

    if (axisSize <= 1)
    {
        throw std::runtime_error(
            "Cannot compute delta on axis " + axisToString(axis) + " with size " + std::to_string(axisSize)
        );
    }

    // Create result shape with axis reduced by 1
    Shape resultShape = m_shape;
    resultShape.dim[axisIdx] = axisSize - 1;

    Tensor result(resultShape);

    for (size_t xcc = 0; xcc < resultShape[0]; ++xcc)
    {
        for (size_t se = 0; se < resultShape[1]; ++se)
        {
            for (size_t cu = 0; cu < resultShape[2]; ++cu)
            {
                for (size_t sample = 0; sample < resultShape[3]; ++sample)
                {
                    // Build indices for current and previous elements
                    size_t currXcc = xcc, currSE = se, currCU = cu, currSample = sample;
                    size_t prevXcc = xcc, prevSE = se, prevCU = cu, prevSample = sample;

                    // Shift current index by 1 and keep previous as-is
                    if (axisIdx == 0)
                    {
                        currXcc = xcc + 1;
                        prevXcc = xcc;
                    }
                    else if (axisIdx == 1)
                    {
                        currSE = se + 1;
                        prevSE = se;
                    }
                    else if (axisIdx == 2)
                    {
                        currCU = cu + 1;
                        prevCU = cu;
                    }
                    else if (axisIdx == 3)
                    {
                        currSample = sample + 1;
                        prevSample = sample;
                    }

                    result.at(xcc, se, cu, sample) =
                        at(currXcc, currSE, currCU, currSample) - at(prevXcc, prevSE, prevCU, prevSample);
                }
            }
        }
    }

    return result;
}

// Template helper for broadcasting operations
template <typename BinaryOp> Tensor Tensor::broadcastOp(const Tensor& other, BinaryOp op) const
{
    // Fast path for same-shape tensors (no broadcasting needed)
    if (m_shape == other.m_shape)
    {
        Tensor result(m_shape);
        for (size_t i = 0; i < m_data.size(); ++i) result[i] = op(m_data[i], other.m_data[i]);
        return result;
    }

    Shape resultShape = Shape::broadcastShape(m_shape, other.m_shape);
    Tensor result(resultShape);

    for (size_t xcc = 0; xcc < resultShape[0]; ++xcc)
    {
        for (size_t se = 0; se < resultShape[1]; ++se)
        {
            for (size_t cu = 0; cu < resultShape[2]; ++cu)
            {
                for (size_t sample = 0; sample < resultShape[3]; ++sample)
                {
                    // Broadcast indices (wrap to 0 if dimension is 1)
                    size_t aXcc = m_shape[0] == 1 ? 0 : xcc;
                    size_t aSE = m_shape[1] == 1 ? 0 : se;
                    size_t aCU = m_shape[2] == 1 ? 0 : cu;
                    size_t aSample = m_shape[3] == 1 ? 0 : sample;

                    size_t bXcc = other.m_shape[0] == 1 ? 0 : xcc;
                    size_t bSE = other.m_shape[1] == 1 ? 0 : se;
                    size_t bCU = other.m_shape[2] == 1 ? 0 : cu;
                    size_t bSample = other.m_shape[3] == 1 ? 0 : sample;

                    float a = at(aXcc, aSE, aCU, aSample);
                    float b = other.at(bXcc, bSE, bCU, bSample);
                    result.at(xcc, se, cu, sample) = op(a, b);
                }
            }
        }
    }

    return result;
}

Tensor Tensor::operator+(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a + b; });
}

Tensor Tensor::operator-(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a - b; });
}

Tensor Tensor::operator*(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a * b; });
}

Tensor Tensor::operator/(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a / b; });
}

Tensor Tensor::operator+(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = m_data[i] + scalar;
    return result;
}

Tensor Tensor::operator-(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = m_data[i] - scalar;
    return result;
}

Tensor Tensor::operator*(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = m_data[i] * scalar;
    return result;
}

Tensor Tensor::operator/(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = m_data[i] / scalar;
    return result;
}

Tensor Tensor::operator-() const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = -m_data.at(i);
    return result;
}

Tensor& Tensor::operator+=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] += other.m_data[i];
        return *this;
    }
    *this = *this + other;
    return *this;
}

Tensor& Tensor::operator-=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] -= other.m_data[i];
        return *this;
    }
    *this = *this - other;
    return *this;
}

Tensor& Tensor::operator*=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] *= other.m_data[i];
        return *this;
    }
    *this = *this * other;
    return *this;
}

Tensor& Tensor::operator/=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] /= other.m_data[i];
        return *this;
    }
    *this = *this / other;
    return *this;
}

Tensor& Tensor::operator|=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i)
            m_data[i] = (m_data[i] != 0.0f || other.m_data[i] != 0.0f) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this | other;
    return *this;
}

Tensor& Tensor::operator&=(const Tensor& other)
{
    // Fast path for same-shape tensors
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i)
            m_data[i] = (m_data[i] != 0.0f && other.m_data[i] != 0.0f) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this & other;
    return *this;
}

Tensor& Tensor::eqInPlace(const Tensor& other)
{
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = (m_data[i] == other.m_data[i]) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this == other;
    return *this;
}

Tensor& Tensor::ltInPlace(const Tensor& other)
{
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = (m_data[i] < other.m_data[i]) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this < other;
    return *this;
}

Tensor& Tensor::gtInPlace(const Tensor& other)
{
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = (m_data[i] > other.m_data[i]) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this > other;
    return *this;
}

Tensor& Tensor::leInPlace(const Tensor& other)
{
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = (m_data[i] <= other.m_data[i]) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this <= other;
    return *this;
}

Tensor& Tensor::geInPlace(const Tensor& other)
{
    if (m_shape == other.m_shape)
    {
        for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = (m_data[i] >= other.m_data[i]) ? 1.0f : 0.0f;
        return *this;
    }
    *this = *this >= other;
    return *this;
}

Tensor& Tensor::negateInPlace()
{
    for (size_t i = 0; i < m_data.size(); ++i) m_data[i] = -m_data[i];
    return *this;
}

// Comparison operators
Tensor Tensor::operator==(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a == b ? 1.0f : 0.0f; });
}

Tensor Tensor::operator<(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a < b ? 1.0f : 0.0f; });
}

Tensor Tensor::operator>(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a > b ? 1.0f : 0.0f; });
}

Tensor Tensor::operator<=(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a <= b ? 1.0f : 0.0f; });
}

Tensor Tensor::operator>=(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return a >= b ? 1.0f : 0.0f; });
}

Tensor Tensor::operator==(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] == scalar) ? 1.0f : 0.0f;
    return result;
}

Tensor Tensor::operator<(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] < scalar) ? 1.0f : 0.0f;
    return result;
}

Tensor Tensor::operator>(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] > scalar) ? 1.0f : 0.0f;
    return result;
}

Tensor Tensor::operator<=(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] <= scalar) ? 1.0f : 0.0f;
    return result;
}

Tensor Tensor::operator>=(float scalar) const
{
    Tensor result(m_shape);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] >= scalar) ? 1.0f : 0.0f;
    return result;
}

// Logical operators (non-zero is true)
Tensor Tensor::operator|(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f; });
}

Tensor Tensor::operator&(const Tensor& other) const
{
    return broadcastOp(other, [](float a, float b) { return (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f; });
}

Tensor Tensor::operator|(float scalar) const
{
    Tensor result(m_shape);
    bool scalarTrue = (scalar != 0.0f);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] != 0.0f || scalarTrue) ? 1.0f : 0.0f;
    return result;
}

Tensor Tensor::operator&(float scalar) const
{
    Tensor result(m_shape);
    bool scalarTrue = (scalar != 0.0f);
    for (size_t i = 0; i < m_data.size(); ++i) result[i] = (m_data[i] != 0.0f && scalarTrue) ? 1.0f : 0.0f;
    return result;
}

std::string Tensor::toString() const
{
    std::ostringstream oss;
    oss << "Tensor" << m_shape.toString() << ": [";
    for (size_t i = 0; i < std::min(m_data.size(), size_t(10)); ++i)
    {
        if (i > 0) oss << ", ";
        oss << m_data.at(i);
    }
    if (m_data.size() > 10) oss << ", ...";
    oss << "]";
    return oss.str();
}

// Free functions
Tensor operator+(float scalar, const Tensor& t) { return t + scalar; }
Tensor operator-(float scalar, const Tensor& t) { return Tensor(scalar) - t; }
Tensor operator*(float scalar, const Tensor& t) { return t * scalar; }
Tensor operator/(float scalar, const Tensor& t) { return Tensor(scalar) / t; }

// Comparison operators with scalar on left
Tensor operator==(float scalar, const Tensor& t) { return t == scalar; }
Tensor operator<(float scalar, const Tensor& t) { return Tensor(scalar) < t; }
Tensor operator>(float scalar, const Tensor& t) { return Tensor(scalar) > t; }
Tensor operator<=(float scalar, const Tensor& t) { return Tensor(scalar) <= t; }
Tensor operator>=(float scalar, const Tensor& t) { return Tensor(scalar) >= t; }

// Logical operators with scalar on left
Tensor operator|(float scalar, const Tensor& t) { return t | scalar; }
Tensor operator&(float scalar, const Tensor& t) { return t & scalar; }

// Create a tensor with linear values along an axis
Tensor linear(size_t size, Axis axis)
{
    Shape shape(1, 1, 1, 1);
    shape.dim[static_cast<size_t>(axis)] = size;

    Tensor result(shape);
    for (size_t i = 0; i < size; ++i)
    {
        // Set the value at position i along the specified axis
        size_t indices[NUM_DIMS] = {0, 0, 0, 0};
        indices[static_cast<size_t>(axis)] = i;
        result.at(indices[0], indices[1], indices[2], indices[3]) = static_cast<float>(i);
    }

    return result;
}

// ============================================================================
// Expression implementations
// ============================================================================

Tensor LiteralExpr::evaluate(CounterContext&) const { return Tensor(m_value); }

std::string LiteralExpr::toString() const { return std::to_string(m_value); }

Tensor VariableExpr::evaluate(CounterContext& ctx) const { return *ctx.getCounter(m_name); }

std::string VariableExpr::toString() const { return m_name; }

Tensor BinaryExpr::evaluate(CounterContext& ctx) const
{
    Tensor left = m_left->evaluate(ctx);
    Tensor right = m_right->evaluate(ctx);

    switch (m_op)
    {
        case Op::Add: left += right; return left;
        case Op::Sub: left -= right; return left;
        case Op::Mul: left *= right; return left;
        case Op::Div: left /= right; return left;
    }
    throw std::runtime_error("Unknown binary operator");
}

std::string BinaryExpr::toString() const
{
    char opChar = '+';
    switch (m_op)
    {
        case Op::Add: opChar = '+'; break;
        case Op::Sub: opChar = '-'; break;
        case Op::Mul: opChar = '*'; break;
        case Op::Div: opChar = '/'; break;
    }
    return "(" + m_left->toString() + " " + opChar + " " + m_right->toString() + ")";
}

Tensor UnaryExpr::evaluate(CounterContext& ctx) const
{
    Tensor result = m_expr->evaluate(ctx);
    result.negateInPlace();
    return result;
}

std::string UnaryExpr::toString() const { return "-" + m_expr->toString(); }

Tensor ReductionExpr::evaluate(CounterContext& ctx) const
{
    Tensor operand = m_operand->evaluate(ctx);

    switch (m_type)
    {
        case ReductionType::Mean: return m_axes.empty() ? operand.meanAll() : operand.mean(m_axes);
        case ReductionType::Max: return m_axes.empty() ? operand.maxAll() : operand.max(m_axes);
        case ReductionType::Min: return m_axes.empty() ? operand.minAll() : operand.min(m_axes);
        case ReductionType::Sum: return m_axes.empty() ? operand.sumAll() : operand.sum(m_axes);
    }
    throw std::runtime_error("Unknown reduction type");
}

std::string ReductionExpr::toString() const
{
    std::string typeName;
    switch (m_type)
    {
        case ReductionType::Mean: typeName = "mean"; break;
        case ReductionType::Max: typeName = "max"; break;
        case ReductionType::Min: typeName = "min"; break;
        case ReductionType::Sum: typeName = "sum"; break;
    }

    std::string axisStr;
    if (!m_axes.empty())
    {
        axisStr = ", axis=[";
        for (size_t i = 0; i < m_axes.size(); ++i)
        {
            if (i > 0) axisStr += ",";
            axisStr += axisToString(m_axes[i]);
        }
        axisStr += "]";
    }

    return typeName + "[" + m_operand->toString() + axisStr + "]";
}

// SelectExpr implementation
Tensor SelectExpr::evaluate(CounterContext& ctx) const
{
    Tensor operand = m_operand->evaluate(ctx);
    Tensor indexTensor = m_index->evaluate(ctx);

    if (!indexTensor.isScalar()) throw std::runtime_error("select[] index must be a scalar");

    int64_t index = static_cast<int64_t>(indexTensor.scalar());
    return operand.select(index, m_axis);
}

std::string SelectExpr::toString() const
{
    return "select[" + m_operand->toString() + ", " + m_index->toString() + ", axis=" + axisToString(m_axis) + "]";
}

// SelectRangeExpr implementation
Tensor SelectRangeExpr::evaluate(CounterContext& ctx) const
{
    Tensor operand = m_operand->evaluate(ctx);
    Tensor startTensor = m_start->evaluate(ctx);
    Tensor stopTensor = m_stop->evaluate(ctx);
    Tensor stepTensor = m_step->evaluate(ctx);

    if (!startTensor.isScalar()) throw std::runtime_error("select[] range start must be a scalar");
    if (!stopTensor.isScalar()) throw std::runtime_error("select[] range stop must be a scalar");
    if (!stepTensor.isScalar()) throw std::runtime_error("select[] range step must be a scalar");

    size_t start = static_cast<size_t>(startTensor.scalar());
    size_t stop = static_cast<size_t>(stopTensor.scalar());
    size_t step = static_cast<size_t>(stepTensor.scalar());

    return operand.selectRange(start, stop, step, m_axis);
}

std::string SelectRangeExpr::toString() const
{
    return "select[" + m_operand->toString() + ", " + m_start->toString() + ":" + m_stop->toString() + ":" +
           m_step->toString() + ", axis=" + axisToString(m_axis) + "]";
}

// RemoveExpr implementation
Tensor RemoveExpr::evaluate(CounterContext& ctx) const
{
    Tensor operand = m_operand->evaluate(ctx);
    Tensor indexTensor = m_index->evaluate(ctx);

    if (!indexTensor.isScalar()) throw std::runtime_error("remove[] index must be a scalar");

    int index = static_cast<int>(indexTensor.scalar());
    return operand.remove(index, m_axis);
}

std::string RemoveExpr::toString() const
{
    return "remove[" + m_operand->toString() + ", " + m_index->toString() + ", axis=" + axisToString(m_axis) + "]";
}

// DeltaExpr implementation
Tensor DeltaExpr::evaluate(CounterContext& ctx) const
{
    Tensor operand = m_operand->evaluate(ctx);
    return operand.delta(m_axis);
}

std::string DeltaExpr::toString() const
{
    return "delta[" + m_operand->toString() + ", axis=" + axisToString(m_axis) + "]";
}

// LinearExpr implementation
Tensor LinearExpr::evaluate(CounterContext& ctx) const
{
    Tensor sizeTensor = m_size->evaluate(ctx);
    if (!sizeTensor.isScalar()) throw std::runtime_error("linear() size argument must be a scalar");
    size_t size = static_cast<size_t>(sizeTensor.scalar());
    return linear(size, m_axis);
}

std::string LinearExpr::toString() const
{
    return "linear(" + m_size->toString() + ", axis=" + axisToString(m_axis) + ")";
}

// ComparisonExpr implementation
Tensor ComparisonExpr::evaluate(CounterContext& ctx) const
{
    Tensor left = m_left->evaluate(ctx);
    const Tensor& right = m_right->evaluate(ctx);

    switch (m_op)
    {
        case Op::Eq: left.eqInPlace(right); return left;
        case Op::Lt: left.ltInPlace(right); return left;
        case Op::Gt: left.gtInPlace(right); return left;
        case Op::Le: left.leInPlace(right); return left;
        case Op::Ge: left.geInPlace(right); return left;
    }
    throw std::runtime_error("Unknown comparison operator");
}

std::string ComparisonExpr::toString() const
{
    std::string opStr;
    switch (m_op)
    {
        case Op::Eq: opStr = "=="; break;
        case Op::Lt: opStr = "<"; break;
        case Op::Gt: opStr = ">"; break;
        case Op::Le: opStr = "<="; break;
        case Op::Ge: opStr = ">="; break;
    }
    return "(" + m_left->toString() + " " + opStr + " " + m_right->toString() + ")";
}

// LogicalExpr implementation
Tensor LogicalExpr::evaluate(CounterContext& ctx) const
{
    Tensor left = m_left->evaluate(ctx);
    const Tensor& right = m_right->evaluate(ctx);

    switch (m_op)
    {
        case Op::Or: left |= right; return left;
        case Op::And: left &= right; return left;
    }
    throw std::runtime_error("Unknown logical operator");
}

std::string LogicalExpr::toString() const
{
    std::string opStr = m_op == Op::Or ? "|" : "&";
    return "(" + m_left->toString() + " " + opStr + " " + m_right->toString() + ")";
}

// ============================================================================
// CounterContext implementation
// ============================================================================

void CounterContext::setCounter(const std::string& name, std::shared_ptr<Tensor> data)
{
    m_rawCounters[name] = std::move(data);
    m_cache.erase(name);
}

void CounterContext::setDerivedCounter(const std::string& name, ExprPtr expr)
{
    m_derivedCounters[name] = std::move(expr);
    m_cache.erase(name);
}

bool CounterContext::isBuiltinScalar(const std::string& name) const
{
    return name == "NUM_XCC" || name == "NUM_SE" || name == "NUM_CU" || name == "NUM_SAMPLES";
}

float CounterContext::getBuiltinScalar(const std::string& name) const
{
    if (m_rawCounters.empty()) throw std::runtime_error("Cannot get " + name + ": no raw counters set");

    // Find the counter with the largest total size (the "full" shape)
    const Tensor* largest = nullptr;
    for (const auto& [_, tensor] : m_rawCounters)
    {
        if (!largest || tensor->shape().totalSize() > largest->shape().totalSize()) largest = tensor.get();
    }
    const Shape& shape = largest->shape();

    if (name == "NUM_XCC") return static_cast<float>(shape.getXCC());
    if (name == "NUM_SE") return static_cast<float>(shape.getSE());
    if (name == "NUM_CU") return static_cast<float>(shape.getCU());
    if (name == "NUM_SAMPLES") return static_cast<float>(shape.getSamples());
    throw std::runtime_error("Unknown builtin scalar: " + name);
}

std::shared_ptr<const Tensor> CounterContext::getCounter(const std::string& name)
{
    // Check cache first (includes builtin scalars and derived results)
    auto cacheIt = m_cache.find(name);
    if (cacheIt != m_cache.end()) return cacheIt->second;

    // Check builtin scalars - cache them
    if (isBuiltinScalar(name))
    {
        auto tensor = std::make_shared<Tensor>(getBuiltinScalar(name));
        m_cache[name] = tensor;
        return tensor;
    }

    // Check raw counters (includes SCLOCK, RCLOCK) - return stored shared_ptr
    auto rawIt = m_rawCounters.find(name);
    if (rawIt != m_rawCounters.end()) return rawIt->second;

    // Check derived counters - evaluate and cache
    auto derivedIt = m_derivedCounters.find(name);
    if (derivedIt != m_derivedCounters.end())
    {
        if (m_evalInProgress.count(name) > 0)
            throw std::runtime_error("Cyclic derived counter definition detected at: " + name);

        struct EvalGuard
        {
            std::unordered_set<std::string>& set;
            std::string key;
            ~EvalGuard() { set.erase(key); }
        } guard{m_evalInProgress, name};

        m_evalInProgress.insert(name);
        try
        {
            auto tensor = std::make_shared<Tensor>(derivedIt->second->evaluate(*this));
            m_cache[name] = tensor;
            return tensor;
        }
        catch (const std::exception&)
        {
            m_errorCounters.insert(name);
            throw;
        }
    }

    throw std::runtime_error("Unknown counter: " + name);
}

void CounterContext::removeDerivedCounter(const std::string& name)
{
    m_derivedCounters.erase(name);
    m_cache.erase(name);
}

void CounterContext::clearDerivedCounters()
{
    m_derivedCounters.clear();
    m_cache.clear();
    m_errorCounters.clear();
}

void CounterContext::clearErrorDerivedCounters()
{
    for (const auto& name : m_errorCounters)
    {
        m_derivedCounters.erase(name);
        m_cache.erase(name);
    }
    m_errorCounters.clear();
}

std::vector<std::string> CounterContext::rawCounterNames() const
{
    std::vector<std::string> names;
    for (const auto& p : m_rawCounters) names.push_back(p.first);
    return names;
}

bool CounterContext::hasCounter(const std::string& name) const
{
    return isBuiltinScalar(name) || m_rawCounters.count(name) > 0 || m_derivedCounters.count(name) > 0;
}

bool CounterContext::isDerived(const std::string& name) const { return m_derivedCounters.count(name) > 0; }

std::vector<std::string> CounterContext::counterNames() const
{
    std::vector<std::string> names;
    for (const auto& p : m_rawCounters) names.push_back(p.first);
    for (const auto& p : m_derivedCounters) names.push_back(p.first);
    return names;
}

std::vector<std::string> CounterContext::derivedCounterNames() const
{
    std::vector<std::string> names;
    for (const auto& p : m_derivedCounters) names.push_back(p.first);
    return names;
}

void CounterContext::clearCache() { m_cache.clear(); }

// ============================================================================
// Parser implementation
// ============================================================================

std::vector<Parser::Token> Parser::tokenize(const std::string& input)
{
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < input.size())
    {
        // Skip whitespace
        while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) ++i;
        if (i >= input.size()) break;

        unsigned char c = static_cast<unsigned char>(input[i]);

        // Number
        if (std::isdigit(c) ||
            (c == '.' && i + 1 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 1]))))
        {
            size_t start = i;
            while (i < input.size() &&
                   (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.' || input[i] == 'e' ||
                    input[i] == 'E' ||
                    ((input[i] == '+' || input[i] == '-') && i > 0 && (input[i - 1] == 'e' || input[i - 1] == 'E'))))
            {
                ++i;
            }
            Token tok;
            tok.type = TokenType::Number;
            tok.text = input.substr(start, i - start);
            tok.numValue = std::stod(tok.text);
            tokens.push_back(tok);
            continue;
        }

        // Identifier (may contain : for counter names like "mycounter:3")
        if (std::isalpha(c) || c == '_')
        {
            size_t start = i;
            while (i < input.size() &&
                   (std::isalnum(static_cast<unsigned char>(input[i])) || input[i] == '_' || input[i] == ':'))
            {
                ++i;
            }
            Token tok;
            tok.type = TokenType::Identifier;
            tok.text = input.substr(start, i - start);
            tokens.push_back(tok);
            continue;
        }

        // Two-character operators
        if (c == ':' && i + 1 < input.size() && input[i + 1] == '=')
        {
            tokens.push_back({TokenType::Define, ":=", 0});
            i += 2;
            continue;
        }
        if (c == '=' && i + 1 < input.size() && input[i + 1] == '=')
        {
            tokens.push_back({TokenType::DoubleEquals, "==", 0});
            i += 2;
            continue;
        }
        if (c == '<' && i + 1 < input.size() && input[i + 1] == '=')
        {
            tokens.push_back({TokenType::LessEquals, "<=", 0});
            i += 2;
            continue;
        }
        if (c == '>' && i + 1 < input.size() && input[i + 1] == '=')
        {
            tokens.push_back({TokenType::GreaterEquals, ">=", 0});
            i += 2;
            continue;
        }

        // Single-character operators
        Token tok;
        tok.type = TokenType::Error;
        tok.text = std::string(1, c);
        switch (c)
        {
            case '(': tok.type = TokenType::LParen; break;
            case ')': tok.type = TokenType::RParen; break;
            case '[': tok.type = TokenType::LBracket; break;
            case ']': tok.type = TokenType::RBracket; break;
            case '+': tok.type = TokenType::Plus; break;
            case '-': tok.type = TokenType::Minus; break;
            case '*': tok.type = TokenType::Star; break;
            case '/': tok.type = TokenType::Slash; break;
            case ',': tok.type = TokenType::Comma; break;
            case ':': tok.type = TokenType::Colon; break;
            case '=': tok.type = TokenType::Equals; break;
            case '<': tok.type = TokenType::Less; break;
            case '>': tok.type = TokenType::Greater; break;
            case '|': tok.type = TokenType::Pipe; break;
            case '&': tok.type = TokenType::Ampersand; break;
            default: throw std::runtime_error("Unexpected character: " + std::string(1, c));
        }
        tokens.push_back(tok);
        ++i;
    }

    tokens.push_back({TokenType::End, "", 0});
    return tokens;
}

const Parser::Token& Parser::ParserState::current() const { return m_tokens[m_pos]; }

const Parser::Token& Parser::ParserState::peek(int offset) const
{
    const auto size = static_cast<std::ptrdiff_t>(m_tokens.size());
    if (size == 0) throw std::runtime_error("ParserState has no tokens");

    std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(m_pos) + static_cast<std::ptrdiff_t>(offset);
    if (idx < 0) return m_tokens.front();
    if (idx >= size) return m_tokens.back();
    return m_tokens[static_cast<size_t>(idx)];
}

void Parser::ParserState::advance()
{
    if (m_pos < m_tokens.size() - 1) ++m_pos;
}

bool Parser::ParserState::match(TokenType type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::ParserState::check(TokenType type) const { return current().type == type; }

void Parser::ParserState::expect(TokenType type, const std::string& msg)
{
    if (!match(type)) throw std::runtime_error(msg + ", got: " + current().text);
}

bool Parser::ParserState::isAtEnd() const { return current().type == TokenType::End; }

ExprPtr Parser::ParserState::parseExpr() { return parseLogicalOr(); }

ExprPtr Parser::ParserState::parseLogicalOr()
{
    ExprPtr left = parseLogicalAnd();

    while (check(TokenType::Pipe))
    {
        advance();
        ExprPtr right = parseLogicalAnd();
        left = std::make_shared<LogicalExpr>(left, LogicalExpr::Op::Or, right);
    }

    return left;
}

ExprPtr Parser::ParserState::parseLogicalAnd()
{
    ExprPtr left = parseComparison();

    while (check(TokenType::Ampersand))
    {
        advance();
        ExprPtr right = parseComparison();
        left = std::make_shared<LogicalExpr>(left, LogicalExpr::Op::And, right);
    }

    return left;
}

ExprPtr Parser::ParserState::parseComparison()
{
    ExprPtr left = parseTerm();

    while (check(TokenType::DoubleEquals) || check(TokenType::Less) || check(TokenType::Greater) ||
           check(TokenType::LessEquals) || check(TokenType::GreaterEquals))
    {
        ComparisonExpr::Op op;
        if (check(TokenType::DoubleEquals))
            op = ComparisonExpr::Op::Eq;
        else if (check(TokenType::Less))
            op = ComparisonExpr::Op::Lt;
        else if (check(TokenType::Greater))
            op = ComparisonExpr::Op::Gt;
        else if (check(TokenType::LessEquals))
            op = ComparisonExpr::Op::Le;
        else
            op = ComparisonExpr::Op::Ge;

        advance();
        ExprPtr right = parseTerm();
        left = std::make_shared<ComparisonExpr>(left, op, right);
    }

    return left;
}

ExprPtr Parser::ParserState::parseTerm()
{
    ExprPtr left = parseFactor();

    while (check(TokenType::Plus) || check(TokenType::Minus))
    {
        BinaryExpr::Op op = check(TokenType::Plus) ? BinaryExpr::Op::Add : BinaryExpr::Op::Sub;
        advance();
        ExprPtr right = parseFactor();
        left = std::make_shared<BinaryExpr>(left, op, right);
    }

    return left;
}

ExprPtr Parser::ParserState::parseFactor()
{
    ExprPtr left = parsePrimary();

    while (check(TokenType::Star) || check(TokenType::Slash))
    {
        BinaryExpr::Op op = check(TokenType::Star) ? BinaryExpr::Op::Mul : BinaryExpr::Op::Div;
        advance();
        ExprPtr right = parsePrimary();
        left = std::make_shared<BinaryExpr>(left, op, right);
    }

    return left;
}

ExprPtr Parser::ParserState::parsePrimary()
{
    // Unary minus
    if (match(TokenType::Minus)) return std::make_shared<UnaryExpr>(parsePrimary());

    // Number
    if (check(TokenType::Number))
    {
        float val = current().numValue;
        advance();
        return std::make_shared<LiteralExpr>(val);
    }

    // Parenthesized expression
    if (match(TokenType::LParen))
    {
        ExprPtr expr = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        return expr;
    }

    // Identifier - could be variable, reduction function, or linear
    if (check(TokenType::Identifier))
    {
        std::string name = current().text;
        advance();

        // Check for linear function: linear(...)
        if (name == "linear" && check(TokenType::LParen)) return parseLinear();

        // Check for function call: name[...]
        if (check(TokenType::LBracket))
        {
            // It's a reduction function
            if (name == "mean" || name == "max" || name == "min" || name == "sum")
                return parseReduction();
            else if (name == "select")
                return parseSelect();
            else if (name == "remove")
                return parseRemove();
            else if (name == "delta")
                return parseDelta();
            else
                throw std::runtime_error("Unknown function: " + name);
        }

        // It's a variable
        return std::make_shared<VariableExpr>(name);
    }

    throw std::runtime_error("Expected expression, got: " + current().text);
}

ExprPtr Parser::ParserState::parseReduction()
{
    // We've already parsed the function name, current token is '['
    // Go back to get the function name
    std::string funcName = peek(-1).text;

    ReductionExpr::ReductionType type;
    if (funcName == "mean")
        type = ReductionExpr::ReductionType::Mean;
    else if (funcName == "max")
        type = ReductionExpr::ReductionType::Max;
    else if (funcName == "min")
        type = ReductionExpr::ReductionType::Min;
    else if (funcName == "sum")
        type = ReductionExpr::ReductionType::Sum;
    else
        throw std::runtime_error("Unknown reduction function: " + funcName);

    expect(TokenType::LBracket, "Expected '['");

    // Parse the operand expression (could be a variable or complex expression)
    ExprPtr operand;
    if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();

        // Check if this identifier is followed by '[' (nested function)
        if (check(TokenType::LBracket))
        {
            if (varName == "mean" || varName == "max" || varName == "min" || varName == "sum")
            {
                operand = parseReduction();
            }
            else if (varName == "select")
                operand = parseSelect();
            else if (varName == "remove")
                operand = parseRemove();
            else if (varName == "delta")
                operand = parseDelta();
            else
            {
                // Not a function, just a variable followed by some [
                operand = std::make_shared<VariableExpr>(varName);
                goto parse_axis;
            }
        }
        else
            operand = std::make_shared<VariableExpr>(varName);
    }
    else if (check(TokenType::LParen))
    {
        // Parenthesized expression
        advance();
        operand = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
    }
    else if (check(TokenType::Number))
    {
        float val = current().numValue;
        advance();
        operand = std::make_shared<LiteralExpr>(val);
    }
    else
        throw std::runtime_error("Expected operand in reduction");

parse_axis:
    // Parse optional axis specification
    std::vector<Axis> axes;
    if (match(TokenType::Comma))
    {
        // Expect "axis" or "axis="
        if (check(TokenType::Identifier) && current().text == "axis")
        {
            advance();
            expect(TokenType::Equals, "Expected '=' after 'axis'");
            axes = parseAxisList();
        }
    }

    expect(TokenType::RBracket, "Expected ']'");

    return std::make_shared<ReductionExpr>(type, operand, axes);
}

ExprPtr Parser::ParserState::parseSelect()
{
    // We've already parsed "select", current token is '['
    expect(TokenType::LBracket, "Expected '['");

    // Parse the operand expression
    ExprPtr operand;
    if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();

        // Check if this identifier is followed by '[' (nested function)
        if (check(TokenType::LBracket))
        {
            if (varName == "mean" || varName == "max" || varName == "min" || varName == "sum")
                operand = parseReduction();
            else if (varName == "select")
                operand = parseSelect();
            else if (varName == "remove")
                operand = parseRemove();
            else if (varName == "delta")
                operand = parseDelta();
            else
                throw std::runtime_error("Unknown function: " + varName);
        }
        else
            operand = std::make_shared<VariableExpr>(varName);
    }
    else if (check(TokenType::LParen))
    {
        advance();
        operand = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
    }
    else if (check(TokenType::Number))
    {
        float val = current().numValue;
        advance();
        operand = std::make_shared<LiteralExpr>(val);
    }
    else
        throw std::runtime_error("Expected operand in select");

    // Parse the index or slice (start:stop:step)
    expect(TokenType::Comma, "Expected ',' after operand in select");

    // Parse the first number/expression (could be index or start of slice)
    ExprPtr firstExpr;
    bool negative = false;
    if (match(TokenType::Minus)) negative = true;
    if (check(TokenType::Number))
    {
        float val = current().numValue;
        if (negative) val = -val;
        advance();
        firstExpr = std::make_shared<LiteralExpr>(val);
    }
    else if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();
        firstExpr = negative ? ExprPtr(std::make_shared<UnaryExpr>(std::make_shared<VariableExpr>(varName)))
                             : ExprPtr(std::make_shared<VariableExpr>(varName));
    }
    else if (check(TokenType::LParen))
    {
        advance();
        ExprPtr inner = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        firstExpr = negative ? std::make_shared<UnaryExpr>(inner) : inner;
    }
    else
        throw std::runtime_error("Expected index or slice in select");

    // Check if this is a slice (has colon)
    bool isSlice = check(TokenType::Colon);
    ExprPtr stopExpr;
    ExprPtr stepExpr;

    if (isSlice)
    {
        advance(); // consume first ':'

        // Parse stop value
        if (check(TokenType::Number))
        {
            float val = current().numValue;
            advance();
            stopExpr = std::make_shared<LiteralExpr>(val);
        }
        else if (check(TokenType::Identifier))
        {
            std::string varName = current().text;
            advance();
            stopExpr = std::make_shared<VariableExpr>(varName);
        }
        else if (check(TokenType::LParen))
        {
            advance();
            stopExpr = parseExpr();
            expect(TokenType::RParen, "Expected ')'");
        }
        else
            throw std::runtime_error("Expected stop value in slice");

        // Check for step (optional second colon)
        if (check(TokenType::Colon))
        {
            advance(); // consume second ':'

            // Parse step value
            if (check(TokenType::Number))
            {
                float val = current().numValue;
                advance();
                stepExpr = std::make_shared<LiteralExpr>(val);
            }
            else if (check(TokenType::Identifier))
            {
                std::string varName = current().text;
                advance();
                stepExpr = std::make_shared<VariableExpr>(varName);
            }
            else if (check(TokenType::LParen))
            {
                advance();
                stepExpr = parseExpr();
                expect(TokenType::RParen, "Expected ')'");
            }
            else
                throw std::runtime_error("Expected step value in slice");
        }
        else
        {
            // Default step is 1
            stepExpr = std::make_shared<LiteralExpr>(1.0f);
        }
    }

    // Parse axis specification: , axis=...
    Axis axis = Axis::Time; // Default axis
    if (match(TokenType::Comma))
    {
        if (check(TokenType::Identifier) && current().text == "axis")
        {
            advance();
            expect(TokenType::Equals, "Expected '=' after 'axis'");

            // Parse single axis
            if (check(TokenType::Identifier))
            {
                axis = axisFromString(current().text);
                advance();
            }
            else if (check(TokenType::Number))
            {
                axis = axisFromString(std::to_string(static_cast<int>(current().numValue)));
                advance();
            }
            else
                throw std::runtime_error("Expected axis name after 'axis='");
        }
        else
            throw std::runtime_error("Expected 'axis=' after ','");
    }

    expect(TokenType::RBracket, "Expected ']'");

    if (isSlice)
        return std::make_shared<SelectRangeExpr>(operand, firstExpr, stopExpr, stepExpr, axis);
    else
        return std::make_shared<SelectExpr>(operand, firstExpr, axis);
}

ExprPtr Parser::ParserState::parseRemove()
{
    // We've already parsed "remove", current token is '['
    expect(TokenType::LBracket, "Expected '['");

    // Parse the operand expression
    ExprPtr operand;
    if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();

        // Check if this identifier is followed by '[' (nested function)
        if (check(TokenType::LBracket))
        {
            if (varName == "mean" || varName == "max" || varName == "min" || varName == "sum")
                operand = parseReduction();
            else if (varName == "select")
                operand = parseSelect();
            else if (varName == "remove")
                operand = parseRemove();
            else if (varName == "delta")
                operand = parseDelta();
            else
                throw std::runtime_error("Unknown function: " + varName);
        }
        else
            operand = std::make_shared<VariableExpr>(varName);
    }
    else if (check(TokenType::LParen))
    {
        advance();
        operand = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
    }
    else if (check(TokenType::Number))
    {
        float val = current().numValue;
        advance();
        operand = std::make_shared<LiteralExpr>(val);
    }
    else
        throw std::runtime_error("Expected operand in remove");

    // Parse the index (can be negative)
    expect(TokenType::Comma, "Expected ',' after operand in remove");
    ExprPtr indexExpr;
    bool negative = false;
    if (match(TokenType::Minus)) negative = true;
    if (check(TokenType::Number))
    {
        float val = current().numValue;
        if (negative) val = -val;
        advance();
        indexExpr = std::make_shared<LiteralExpr>(val);
    }
    else if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();
        indexExpr = negative ? ExprPtr(std::make_shared<UnaryExpr>(std::make_shared<VariableExpr>(varName)))
                             : ExprPtr(std::make_shared<VariableExpr>(varName));
    }
    else if (check(TokenType::LParen))
    {
        advance();
        ExprPtr inner = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        indexExpr = negative ? std::make_shared<UnaryExpr>(inner) : inner;
    }
    else
        throw std::runtime_error("Expected index in remove");

    // Parse axis specification: , axis=...
    Axis axis = Axis::Time; // Default axis
    if (match(TokenType::Comma))
    {
        if (check(TokenType::Identifier) && current().text == "axis")
        {
            advance();
            expect(TokenType::Equals, "Expected '=' after 'axis'");

            // Parse single axis
            if (check(TokenType::Identifier))
            {
                axis = axisFromString(current().text);
                advance();
            }
            else if (check(TokenType::Number))
            {
                axis = axisFromString(std::to_string(static_cast<int>(current().numValue)));
                advance();
            }
            else
                throw std::runtime_error("Expected axis name after 'axis='");
        }
        else
            throw std::runtime_error("Expected 'axis=' after ','");
    }

    expect(TokenType::RBracket, "Expected ']'");

    return std::make_shared<RemoveExpr>(operand, indexExpr, axis);
}

ExprPtr Parser::ParserState::parseDelta()
{
    // We've already parsed "delta", current token is '['
    expect(TokenType::LBracket, "Expected '['");

    // Parse the operand expression
    ExprPtr operand;
    if (check(TokenType::Identifier))
    {
        std::string varName = current().text;
        advance();

        // Check if this identifier is followed by '[' (nested function)
        if (check(TokenType::LBracket))
        {
            if (varName == "mean" || varName == "max" || varName == "min" || varName == "sum")
                operand = parseReduction();
            else if (varName == "select")
                operand = parseSelect();
            else if (varName == "remove")
                operand = parseRemove();
            else if (varName == "delta")
                operand = parseDelta();
            else
                throw std::runtime_error("Unknown function: " + varName);
        }
        else
            operand = std::make_shared<VariableExpr>(varName);
    }
    else if (check(TokenType::LParen))
    {
        advance();
        operand = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
    }
    else if (check(TokenType::Number))
    {
        float val = current().numValue;
        advance();
        operand = std::make_shared<LiteralExpr>(val);
    }
    else
        throw std::runtime_error("Expected operand in delta");

    // Parse optional axis specification: , axis=...
    Axis axis = Axis::Time; // Default axis
    if (match(TokenType::Comma))
    {
        if (check(TokenType::Identifier) && current().text == "axis")
        {
            advance();
            expect(TokenType::Equals, "Expected '=' after 'axis'");

            // Parse single axis
            if (check(TokenType::Identifier))
            {
                axis = axisFromString(current().text);
                advance();
            }
            else if (check(TokenType::Number))
            {
                axis = axisFromString(std::to_string(static_cast<int>(current().numValue)));
                advance();
            }
            else
                throw std::runtime_error("Expected axis name after 'axis='");
        }
        else
            throw std::runtime_error("Expected 'axis=' after ','");
    }

    expect(TokenType::RBracket, "Expected ']'");

    return std::make_shared<DeltaExpr>(operand, axis);
}

std::vector<Axis> Parser::ParserState::parseAxisList()
{
    std::vector<Axis> axes;

    if (check(TokenType::LBracket))
    {
        // List of axes: [XCC, SE, ...]
        advance();
        while (!check(TokenType::RBracket) && !isAtEnd())
        {
            if (check(TokenType::Identifier))
            {
                axes.push_back(axisFromString(current().text));
                advance();
            }
            else if (check(TokenType::Number))
            {
                axes.push_back(axisFromString(std::to_string(static_cast<int>(current().numValue))));
                advance();
            }
            else
                throw std::runtime_error("Expected axis name");

            if (!check(TokenType::RBracket)) expect(TokenType::Comma, "Expected ',' or ']'");
        }
        expect(TokenType::RBracket, "Expected ']'");
    }
    else if (check(TokenType::Identifier))
    {
        // Single axis
        axes.push_back(axisFromString(current().text));
        advance();
    }
    else if (check(TokenType::Number))
    {
        axes.push_back(axisFromString(std::to_string(static_cast<int>(current().numValue))));
        advance();
    }
    else
        throw std::runtime_error("Expected axis specification");

    return axes;
}

ExprPtr Parser::ParserState::parseLinear()
{
    // We've already parsed "linear" and current token is '('
    expect(TokenType::LParen, "Expected '(' after 'linear'");

    // Parse size expression
    ExprPtr sizeExpr = parseExpr();

    // Parse axis specification: , axis=...
    Axis axis = Axis::Time; // Default axis
    if (match(TokenType::Comma))
    {
        if (check(TokenType::Identifier) && current().text == "axis")
        {
            advance();
            expect(TokenType::Equals, "Expected '=' after 'axis'");

            // Parse single axis
            if (check(TokenType::Identifier))
            {
                axis = axisFromString(current().text);
                advance();
            }
            else if (check(TokenType::Number))
            {
                axis = axisFromString(std::to_string(static_cast<int>(current().numValue)));
                advance();
            }
            else
                throw std::runtime_error("Expected axis name");
        }
        else
            throw std::runtime_error("Expected 'axis=' in linear function");
    }

    expect(TokenType::RParen, "Expected ')' after linear arguments");

    return std::make_shared<LinearExpr>(sizeExpr, axis);
}

Parser::Definition Parser::parseDefinition(const std::string& line)
{
    // Find := operator
    size_t pos = line.find(":=");
    if (pos == std::string::npos) throw std::runtime_error("Definition must contain ':='");

    std::string name = line.substr(0, pos);
    std::string exprStr = line.substr(pos + 2);

    // Trim whitespace
    auto trim = [](std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos)
            s = "";
        else
            s = s.substr(start, end - start + 1);
    };

    trim(name);
    trim(exprStr);

    if (name.empty()) throw std::runtime_error("Definition name cannot be empty");

    return {name, parseExpression(exprStr)};
}

std::vector<Parser::Definition> Parser::parseFile(const std::string& content)
{
    std::vector<Definition> defs;
    std::istringstream stream(content);
    std::string line;
    int lineNum = 0;

    while (std::getline(stream, line))
    {
        ++lineNum;

        // Remove comments (everything after #)
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue; // Empty line

        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty()) continue;

        try
        {
            defs.push_back(parseDefinition(line));
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Parse error at line " + std::to_string(lineNum) + ": " + e.what());
        }
    }

    return defs;
}

ExprPtr Parser::parseExpression(const std::string& expr)
{
    auto tokens = tokenize(expr);
    ParserState state(std::move(tokens));
    ExprPtr result = state.parseExpr();
    if (!state.isAtEnd()) throw std::runtime_error("Unexpected token after expression: " + state.current().text);
    return result;
}

// ============================================================================
// DerivedCounterManager implementation
// ============================================================================

DerivedCounterManager::DerivedCounterManager() {}

void DerivedCounterManager::loadDefinitions(const std::string& content)
{
    auto defs = m_parser.parseFile(content);
    for (auto& def : defs) m_context.setDerivedCounter(def.name, std::move(def.expression));
}

void DerivedCounterManager::loadDefinitionsFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file) throw std::runtime_error("Cannot open file: " + filepath);

    std::ostringstream ss;
    ss << file.rdbuf();
    loadDefinitions(ss.str());
}

std::vector<std::string> DerivedCounterManager::derivedCounterNames() const { return m_context.derivedCounterNames(); }

std::shared_ptr<const Tensor> DerivedCounterManager::evaluate(const std::string& name)
{
    return m_context.getCounter(name);
}

void DerivedCounterManager::clear() { m_context = CounterContext(); }

} // namespace DerivedCounter
