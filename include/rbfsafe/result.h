#pragma once

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
