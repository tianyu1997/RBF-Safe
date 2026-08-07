#pragma once

// Results and error handling.

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace rbfsafe {

enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    DimensionMismatch,
    ResourceLimit,
    IdentityMismatch,
    IncompatibleFormat,
    CorruptData,
    IoError,
    Cancelled,
    InternalError,
};

struct Error {
    StatusCode code = StatusCode::InternalError;
    std::string message;
    std::string context;

    std::string describe() const { return context.empty() ? message : message + " [" + context + "]"; }
};

template <typename T> class Result {
  public:
    Result(const T& value) : storage_(value) {}
    Result(T&& value) : storage_(std::move(value)) {}
    Result(const Error& error) : storage_(error) {}
    Result(Error&& error) : storage_(std::move(error)) {}

    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(StatusCode code, std::string message, std::string context = {}) {
        return Result(Error{code, std::move(message), std::move(context)});
    }

    bool has_value() const noexcept { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const noexcept { return has_value(); }

    T& value() & {
        if (!has_value())
            throw std::logic_error(error().describe());
        return std::get<T>(storage_);
    }
    const T& value() const& {
        if (!has_value())
            throw std::logic_error(error().describe());
        return std::get<T>(storage_);
    }
    T&& value() && {
        if (!has_value())
            throw std::logic_error(error().describe());
        return std::get<T>(std::move(storage_));
    }
    const Error& error() const {
        if (has_value())
            throw std::logic_error("Result has no error");
        return std::get<Error>(storage_);
    }

  private:
    std::variant<T, Error> storage_;
};

template <> class Result<void> {
  public:
    Result() = default;
    Result(const Error& error) : error_(error), ok_(false) {}
    Result(Error&& error) : error_(std::move(error)), ok_(false) {}

    static Result success() { return Result(); }
    static Result failure(StatusCode code, std::string message, std::string context = {}) {
        return Result(Error{code, std::move(message), std::move(context)});
    }

    bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    const Error& error() const {
        if (ok_)
            throw std::logic_error("Result has no error");
        return error_;
    }

  private:
    Error error_{};
    bool ok_ = true;
};

} // namespace rbfsafe

// Common geometry-independent value types.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe {

using Configuration = std::vector<double>;
using RegionId = std::uint64_t;
using ComponentId = std::uint64_t;

struct Interval {
    double lower = 0.0;
    double upper = 0.0;

    constexpr Interval() = default;
    constexpr Interval(double lower_value, double upper_value) : lower(lower_value), upper(upper_value) {}

    bool valid() const noexcept { return std::isfinite(lower) && std::isfinite(upper) && lower <= upper; }
    double width() const noexcept { return upper - lower; }
    double center() const noexcept { return 0.5 * (lower + upper); }
    bool contains(double value, double tolerance = 0.0) const noexcept {
        return value >= lower - tolerance && value <= upper + tolerance;
    }
    bool overlaps(const Interval& other, double tolerance = 0.0) const noexcept {
        return lower <= other.upper + tolerance && other.lower <= upper + tolerance;
    }
    friend bool operator==(const Interval&, const Interval&) = default;
};

class CspaceAabb {
  public:
    CspaceAabb() = default;
    explicit CspaceAabb(std::vector<Interval> axes) : axes_(std::move(axes)) {}

    std::size_t dimension() const noexcept { return axes_.size(); }
    const std::vector<Interval>& axes() const noexcept { return axes_; }
    std::vector<Interval>& axes() noexcept { return axes_; }

    bool valid() const noexcept {
        return !axes_.empty() &&
               std::all_of(axes_.begin(), axes_.end(), [](const Interval& axis) { return axis.valid(); });
    }
    bool contains(std::span<const double> configuration, double tolerance = 0.0) const noexcept {
        if (configuration.size() != axes_.size())
            return false;
        for (std::size_t index = 0; index < axes_.size(); ++index) {
            if (!axes_[index].contains(configuration[index], tolerance))
                return false;
        }
        return true;
    }
    bool overlaps(const CspaceAabb& other, double tolerance = 0.0) const noexcept {
        if (other.dimension() != dimension())
            return false;
        for (std::size_t index = 0; index < axes_.size(); ++index) {
            if (!axes_[index].overlaps(other.axes_[index], tolerance))
                return false;
        }
        return true;
    }
    double volume() const noexcept {
        if (!valid())
            return 0.0;
        double result = 1.0;
        for (const auto& axis : axes_)
            result *= axis.width();
        return result;
    }
    Configuration center() const {
        Configuration result;
        result.reserve(axes_.size());
        for (const auto& axis : axes_)
            result.push_back(axis.center());
        return result;
    }

  private:
    std::vector<Interval> axes_;
};

struct WorkspaceAabb {
    std::array<double, 3> lower{};
    std::array<double, 3> upper{};

    bool valid() const noexcept {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(lower[axis]) || !std::isfinite(upper[axis]) || lower[axis] > upper[axis]) {
                return false;
            }
        }
        return true;
    }
    bool overlaps(const WorkspaceAabb& other, double tolerance = 0.0) const noexcept {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (lower[axis] > other.upper[axis] + tolerance || other.lower[axis] > upper[axis] + tolerance)
                return false;
        }
        return true;
    }
    double distance_lower_bound(const WorkspaceAabb& other) const noexcept {
        double squared = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            double separation = 0.0;
            if (upper[axis] < other.lower[axis])
                separation = other.lower[axis] - upper[axis];
            else if (other.upper[axis] < lower[axis])
                separation = lower[axis] - other.upper[axis];
            squared += separation * separation;
        }
        return std::sqrt(squared);
    }
    friend bool operator==(const WorkspaceAabb&, const WorkspaceAabb&) = default;
};

inline Result<void> validate_configuration(std::span<const double> configuration,
                                           std::size_t expected_dimension,
                                           std::string context = "configuration") {
    if (configuration.size() != expected_dimension) {
        return Result<void>::failure(StatusCode::DimensionMismatch,
                                     "configuration dimension does not match robot", std::move(context));
    }
    for (const double value : configuration) {
        if (!std::isfinite(value)) {
            return Result<void>::failure(StatusCode::InvalidArgument,
                                         "configuration contains a non-finite value", std::move(context));
        }
    }
    return Result<void>::success();
}

} // namespace rbfsafe

// Library and storage-schema versions.

#define RBFSAFE_VERSION_MAJOR 5
#define RBFSAFE_VERSION_MINOR 0
#define RBFSAFE_VERSION_PATCH 0
#define RBFSAFE_VERSION_STRING "5.0.0"

namespace rbfsafe {
inline constexpr const char* kVersion = RBFSAFE_VERSION_STRING;
inline constexpr unsigned kAtlasSchemaVersion = 2;
inline constexpr unsigned kLectSchemaVersion = 1;
} // namespace rbfsafe
