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

#ifndef DERIVED_COUNTER_H
#define DERIVED_COUNTER_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace DerivedCounter
{

// Axis enumeration for the tensor dimensions
// Order: counter[NUM_XCC][NUM_SE][NUM_CU][NUM_SAMPLES]
enum class Axis
{
    XCC = 0,  // First dimension
    SE = 1,   // Second dimension
    CU = 2,   // Third dimension
    Time = 3, // Fourth dimension (NUM_SAMPLES)
    All = -1  // Special: reduce all axes
};

// Convert axis name string to Axis enum
Axis axisFromString(const std::string& name);
std::string axisToString(Axis axis);

// Number of dimensions in a tensor
constexpr size_t NUM_DIMS = 4;

// Shape of a tensor - sizes for each axis
struct Shape
{
    size_t dim[NUM_DIMS] = {1, 1, 1, 1};

    Shape() = default;
    Shape(size_t xcc, size_t se, size_t cu, size_t samples) : dim{xcc, se, cu, samples} {}

    // Named accessors for dimensions
    size_t getXCC() const { return dim[0]; }
    size_t getSE() const { return dim[1]; }
    size_t getCU() const { return dim[2]; }
    size_t getSamples() const { return dim[3]; }

    void setXCC(size_t v) { dim[0] = v; }
    void setSE(size_t v) { dim[1] = v; }
    void setCU(size_t v) { dim[2] = v; }
    void setSamples(size_t v) { dim[3] = v; }

    size_t totalSize() const { return dim[0] * dim[1] * dim[2] * dim[3]; }
    size_t dimSize(Axis axis) const;
    size_t dimSize(size_t axis) const { return dim[axis]; }
    bool isScalar() const { return totalSize() == 1; }
    bool operator==(const Shape& other) const;
    bool operator!=(const Shape& other) const { return !(*this == other); }

    // Array-style access
    size_t operator[](size_t i) const { return dim[i]; }
    size_t& operator[](size_t i) { return dim[i]; }

    // Get shape after reducing specified axes
    Shape reducedShape(const std::vector<Axis>& axes) const;

    // Check if shapes are broadcastable
    static bool areBroadcastable(const Shape& a, const Shape& b);
    static Shape broadcastShape(const Shape& a, const Shape& b);

    std::string toString() const;
};

// Multi-dimensional tensor class
class Tensor
{
public:
    Tensor();
    explicit Tensor(float scalar);
    Tensor(const Shape& shape, float fillValue = 0.0);
    Tensor(const Shape& shape, const std::vector<float>& data);

    // Move semantics
    Tensor(Tensor&& other) noexcept = default;
    Tensor& operator=(Tensor&& other) noexcept = default;

    // Copy semantics
    Tensor(const Tensor& other) = default;
    Tensor& operator=(const Tensor& other) = default;

    // Accessors
    Shape shape() const { return m_shape; }
    const std::vector<float>& data() const { return m_data; }
    std::vector<float>& data() { return m_data; }
    size_t size() const { return m_data.size(); }
    bool isScalar() const { return m_shape.isScalar(); }
    float scalar() const;

    // Element access with 4D indexing
    float& at(size_t xcc, size_t se, size_t cu, size_t sample);
    float at(size_t xcc, size_t se, size_t cu, size_t sample) const;

    // Linear index access
    float& operator[](size_t idx) { return m_data.at(idx); }
    float operator[](size_t idx) const { return m_data.at(idx); }

    // Convert 4D index to linear index
    size_t linearIndex(size_t xcc, size_t se, size_t cu, size_t sample) const;

    // Convert linear index to 4D indices
    void multiIndex(size_t linear, size_t& xcc, size_t& se, size_t& cu, size_t& sample) const;

    // Reduction operations
    Tensor mean(const std::vector<Axis>& axes) const;
    Tensor max(const std::vector<Axis>& axes) const;
    Tensor min(const std::vector<Axis>& axes) const;
    Tensor sum(const std::vector<Axis>& axes) const;

    // Single axis versions
    Tensor mean(Axis axis) const { return mean(std::vector<Axis>{axis}); }
    Tensor max(Axis axis) const { return max(std::vector<Axis>{axis}); }
    Tensor min(Axis axis) const { return min(std::vector<Axis>{axis}); }
    Tensor sum(Axis axis) const { return sum(std::vector<Axis>{axis}); }

    // Select a single index along an axis (reduces that axis to size 1). Negative allowed.
    Tensor select(int64_t index, Axis axis) const;

    // Select a range of indices along an axis (Python-style slicing: start:stop:step)
    // Returns tensor with selected indices concatenated along the axis
    Tensor selectRange(size_t start, size_t stop, size_t step, Axis axis) const;

    // Remove a single index along an axis (reduces axis size by 1)
    // Supports negative indexing: -1 = last, -2 = second to last, etc.
    Tensor remove(int index, Axis axis) const;

    // Compute difference to previous element along an axis
    // Result has axis size reduced by 1
    Tensor delta(Axis axis) const;

    // Reduce all axes (return scalar tensor)
    Tensor meanAll() const;
    Tensor maxAll() const;
    Tensor minAll() const;
    Tensor sumAll() const;

    // Element-wise operations with broadcasting
    Tensor operator+(const Tensor& other) const;
    Tensor operator-(const Tensor& other) const;
    Tensor operator*(const Tensor& other) const;
    Tensor operator/(const Tensor& other) const;

    // Scalar operations
    Tensor operator+(float scalar) const;
    Tensor operator-(float scalar) const;
    Tensor operator*(float scalar) const;
    Tensor operator/(float scalar) const;

    // Unary operations
    Tensor operator-() const;

    // Comparison operators (return 1.0f for true, 0.0f for false)
    Tensor operator==(const Tensor& other) const;
    Tensor operator<(const Tensor& other) const;
    Tensor operator>(const Tensor& other) const;
    Tensor operator<=(const Tensor& other) const;
    Tensor operator>=(const Tensor& other) const;

    // Scalar comparison operators
    Tensor operator==(float scalar) const;
    Tensor operator<(float scalar) const;
    Tensor operator>(float scalar) const;
    Tensor operator<=(float scalar) const;
    Tensor operator>=(float scalar) const;

    // Logical operators (treat non-zero as true)
    Tensor operator|(const Tensor& other) const;
    Tensor operator&(const Tensor& other) const;
    Tensor operator|(float scalar) const;
    Tensor operator&(float scalar) const;

    // In-place operations
    Tensor& operator+=(const Tensor& other);
    Tensor& operator-=(const Tensor& other);
    Tensor& operator*=(const Tensor& other);
    Tensor& operator/=(const Tensor& other);
    Tensor& operator|=(const Tensor& other);
    Tensor& operator&=(const Tensor& other);

    // In-place comparison (modifies this to hold 0.0f/1.0f result)
    Tensor& eqInPlace(const Tensor& other);
    Tensor& ltInPlace(const Tensor& other);
    Tensor& gtInPlace(const Tensor& other);
    Tensor& leInPlace(const Tensor& other);
    Tensor& geInPlace(const Tensor& other);

    // In-place negation
    Tensor& negateInPlace();

    std::string toString() const;

private:
    Shape m_shape;
    std::vector<float> m_data;

    // Helper for reduction operations
    template <typename ReduceOp> Tensor reduceAxes(const std::vector<Axis>& axes, ReduceOp op, float identity) const;

    // Helper for broadcasting element-wise operations
    template <typename BinaryOp> Tensor broadcastOp(const Tensor& other, BinaryOp op) const;
};

// Free functions for scalar on left side
Tensor operator+(float scalar, const Tensor& t);
Tensor operator-(float scalar, const Tensor& t);
Tensor operator*(float scalar, const Tensor& t);
Tensor operator/(float scalar, const Tensor& t);

// Comparison operators with scalar on left
Tensor operator==(float scalar, const Tensor& t);
Tensor operator<(float scalar, const Tensor& t);
Tensor operator>(float scalar, const Tensor& t);
Tensor operator<=(float scalar, const Tensor& t);
Tensor operator>=(float scalar, const Tensor& t);

// Logical operators with scalar on left
Tensor operator|(float scalar, const Tensor& t);
Tensor operator&(float scalar, const Tensor& t);

// Create a tensor with linear values along an axis
Tensor linear(size_t size, Axis axis);

// ============================================================================
// Expression AST for parsing derived counter definitions
// ============================================================================

// Forward declarations
class Expression;
using ExprPtr = std::shared_ptr<Expression>;

// Base expression class
class Expression
{
public:
    virtual ~Expression() = default;
    virtual Tensor evaluate(class CounterContext& ctx) const = 0;
    virtual std::string toString() const = 0;
};

// Literal number
class LiteralExpr : public Expression
{
public:
    explicit LiteralExpr(float value) : m_value(value) {}
    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    float m_value;
};

// Variable/counter reference
class VariableExpr : public Expression
{
public:
    explicit VariableExpr(const std::string& name) : m_name(name) {}
    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;
    const std::string& name() const { return m_name; }

private:
    std::string m_name;
};

// Binary operation
class BinaryExpr : public Expression
{
public:
    enum class Op
    {
        Add,
        Sub,
        Mul,
        Div
    };
    BinaryExpr(ExprPtr left, Op op, ExprPtr right) : m_left(std::move(left)), m_op(op), m_right(std::move(right)) {}
    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_left;
    Op m_op;
    ExprPtr m_right;
};

// Unary negation
class UnaryExpr : public Expression
{
public:
    explicit UnaryExpr(ExprPtr expr) : m_expr(std::move(expr)) {}
    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_expr;
};

// Reduction function call: mean[counter, axis=...]
class ReductionExpr : public Expression
{
public:
    enum class ReductionType
    {
        Mean,
        Max,
        Min,
        Sum
    };

    ReductionExpr(ReductionType type, ExprPtr operand, std::vector<Axis> axes) :
    m_type(type), m_operand(std::move(operand)), m_axes(std::move(axes))
    {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ReductionType m_type;
    ExprPtr m_operand;
    std::vector<Axis> m_axes;
};

// Select expression: select[counter, index, axis=...]
class SelectExpr : public Expression
{
public:
    SelectExpr(ExprPtr operand, ExprPtr index, Axis axis) :
    m_operand(std::move(operand)), m_index(std::move(index)), m_axis(axis)
    {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_operand;
    ExprPtr m_index;
    Axis m_axis;
};

// Select range expression: select[counter, start:stop:step, axis=...]
class SelectRangeExpr : public Expression
{
public:
    SelectRangeExpr(ExprPtr operand, ExprPtr start, ExprPtr stop, ExprPtr step, Axis axis) :
    m_operand(std::move(operand)),
    m_start(std::move(start)),
    m_stop(std::move(stop)),
    m_step(std::move(step)),
    m_axis(axis)
    {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_operand;
    ExprPtr m_start;
    ExprPtr m_stop;
    ExprPtr m_step;
    Axis m_axis;
};

// Remove expression: remove[counter, index, axis=...]
class RemoveExpr : public Expression
{
public:
    RemoveExpr(ExprPtr operand, ExprPtr index, Axis axis) :
    m_operand(std::move(operand)), m_index(std::move(index)), m_axis(axis)
    {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_operand;
    ExprPtr m_index;
    Axis m_axis;
};

// Delta expression: delta[counter, axis=...]
class DeltaExpr : public Expression
{
public:
    DeltaExpr(ExprPtr operand, Axis axis) : m_operand(std::move(operand)), m_axis(axis) {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_operand;
    Axis m_axis;
};

// Linear tensor generator: linear(size, axis=...)
class LinearExpr : public Expression
{
public:
    LinearExpr(ExprPtr size, Axis axis) : m_size(std::move(size)), m_axis(axis) {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_size;
    Axis m_axis;
};

// Comparison expression
class ComparisonExpr : public Expression
{
public:
    enum class Op
    {
        Eq, // ==
        Lt, // <
        Gt, // >
        Le, // <=
        Ge  // >=
    };

    ComparisonExpr(ExprPtr left, Op op, ExprPtr right) : m_left(std::move(left)), m_op(op), m_right(std::move(right)) {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_left;
    Op m_op;
    ExprPtr m_right;
};

// Logical expression
class LogicalExpr : public Expression
{
public:
    enum class Op
    {
        Or, // |
        And // &
    };

    LogicalExpr(ExprPtr left, Op op, ExprPtr right) : m_left(std::move(left)), m_op(op), m_right(std::move(right)) {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    ExprPtr m_left;
    Op m_op;
    ExprPtr m_right;
};

// Element-wise function call: max(A, B, ...), min(A, B, ...)
class ElementWiseFuncExpr : public Expression
{
public:
    enum class FuncType
    {
        Max,
        Min
    };

    ElementWiseFuncExpr(FuncType type, std::vector<ExprPtr> operands) : m_type(type), m_operands(std::move(operands)) {}

    Tensor evaluate(CounterContext& ctx) const override;
    std::string toString() const override;

private:
    FuncType m_type;
    std::vector<ExprPtr> m_operands;
};

// ============================================================================
// Counter context - holds counter data and derived definitions
// ============================================================================

class CounterContext
{
public:
    CounterContext() = default;

    // Register a raw counter (collected data)
    void setCounter(const std::string& name, std::shared_ptr<Tensor> data);

    // Register a derived counter expression
    void setDerivedCounter(const std::string& name, ExprPtr expr);

    // Remove a derived counter
    void removeDerivedCounter(const std::string& name);

    // Clear all derived counters
    void clearDerivedCounters();

    // Clear only derived counters that had evaluation errors
    void clearErrorDerivedCounters();

    // Get a counter value (evaluates derived counters as needed)
    // Returns shared_ptr for safe lifetime management
    std::shared_ptr<const Tensor> getCounter(const std::string& name);

    // Get all raw counter names (excludes derived counters)
    std::vector<std::string> rawCounterNames() const;

    // Check if a counter exists
    bool hasCounter(const std::string& name) const;
    bool isDerived(const std::string& name) const;

    // Get all counter names
    std::vector<std::string> counterNames() const;
    std::vector<std::string> derivedCounterNames() const;

    // Clear cached derived values (call when raw counters change)
    void clearCache();

    // Get builtin scalar value (derived from first raw counter's shape)
    float getBuiltinScalar(const std::string& name) const;
    bool isBuiltinScalar(const std::string& name) const;

    // Track which raw counters were accessed during evaluation
    const std::unordered_set<std::string>& accessedRawCounters() const { return m_accessedRawCounters; }
    void clearAccessedRawCounters() { m_accessedRawCounters.clear(); }

private:
    std::unordered_map<std::string, std::shared_ptr<Tensor>> m_rawCounters;
    std::unordered_map<std::string, ExprPtr> m_derivedCounters;
    mutable std::unordered_map<std::string, std::shared_ptr<const Tensor>> m_cache;
    std::unordered_set<std::string> m_evalInProgress;
    std::unordered_set<std::string> m_errorCounters; // Derived counters that failed evaluation
    std::unordered_set<std::string> m_accessedRawCounters;
};

// ============================================================================
// Parser for derived counter definitions
// ============================================================================

class Parser
{
public:
    Parser() = default;

    // Parse a single definition: "name := expression"
    struct Definition
    {
        std::string name;
        ExprPtr expression;
    };

    Definition parseDefinition(const std::string& line);

    // Parse multiple definitions from text (one per line, ignoring comments and empty lines)
    std::vector<Definition> parseFile(const std::string& content);

    // Parse just an expression
    ExprPtr parseExpression(const std::string& expr);

private:
    // Tokenizer
    enum class TokenType
    {
        Number,
        Identifier,
        LParen,
        RParen,
        LBracket,
        RBracket,
        Plus,
        Minus,
        Star,
        Slash,
        Comma,
        Colon, // :
        Equals,
        Define,
        DoubleEquals,  // ==
        Less,          // <
        Greater,       // >
        LessEquals,    // <=
        GreaterEquals, // >=
        Pipe,          // |
        Ampersand,     // &
        End,
        Error
    };

    struct Token
    {
        TokenType type;
        std::string text;
        float numValue = 0;
    };

    std::vector<Token> tokenize(const std::string& input);

    // Recursive descent parser
    class ParserState
    {
    public:
        ParserState(std::vector<Token> tokens) : m_tokens(std::move(tokens)), m_pos(0) {}

        ExprPtr parseExpr();
        ExprPtr parseLogicalOr();
        ExprPtr parseLogicalAnd();
        ExprPtr parseComparison();
        ExprPtr parseTerm();
        ExprPtr parseFactor();
        ExprPtr parsePrimary();
        ExprPtr parseReduction();
        ExprPtr parseSelect();
        ExprPtr parseRemove();
        ExprPtr parseDelta();
        ExprPtr parseLinear();
        ExprPtr parseElementWiseFunc();
        std::vector<Axis> parseAxisList();

        const Token& current() const;
        const Token& peek(int offset = 0) const;
        void advance();
        bool match(TokenType type);
        bool check(TokenType type) const;
        void expect(TokenType type, const std::string& msg);
        bool isAtEnd() const;

    private:
        std::vector<Token> m_tokens;
        size_t m_pos;
    };
};

// ============================================================================
// Main interface for loading and managing derived counters
// ============================================================================

class DerivedCounterManager
{
public:
    DerivedCounterManager();

    // Load definitions from a text string
    void loadDefinitions(const std::string& content);

    // Load definitions from a file path
    void loadDefinitionsFromFile(const std::string& filepath);

    // Get the counter context for setting raw data and evaluating counters
    CounterContext& context() { return m_context; }
    const CounterContext& context() const { return m_context; }

    // Get names of all defined derived counters
    std::vector<std::string> derivedCounterNames() const;

    // Evaluate a derived counter
    std::shared_ptr<const Tensor> evaluate(const std::string& name);

    // Clear all definitions
    void clear();

private:
    CounterContext m_context;
    Parser m_parser;
};

} // namespace DerivedCounter

#endif // DERIVED_COUNTER_H
