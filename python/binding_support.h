#pragma once

#include <rbfsafe/rbfsafe.h>

#include <pybind11/pybind11.h>

#include <span>
#include <stdexcept>
#include <utility>

namespace rbfsafe::python_binding {

namespace py = pybind11;

class RBFSafeException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class IdentityMismatchException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class IncompatibleFormatException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class CorruptDataException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class CancelledException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class InternalException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};

[[noreturn]] inline void throw_error(const Error& error) {
    switch (error.code) {
    case StatusCode::InvalidArgument:
    case StatusCode::DimensionMismatch:
        throw py::value_error(error.describe());
    case StatusCode::IoError:
        PyErr_SetString(PyExc_OSError, error.describe().c_str());
        throw py::error_already_set();
    case StatusCode::ResourceLimit:
        PyErr_SetString(PyExc_MemoryError, error.describe().c_str());
        throw py::error_already_set();
    case StatusCode::IdentityMismatch:
        throw IdentityMismatchException(error.describe());
    case StatusCode::IncompatibleFormat:
        throw IncompatibleFormatException(error.describe());
    case StatusCode::CorruptData:
        throw CorruptDataException(error.describe());
    case StatusCode::Cancelled:
        throw CancelledException(error.describe());
    case StatusCode::InternalError:
        throw InternalException(error.describe());
    default:
        throw RBFSafeException(error.describe());
    }
}

template <typename T> T unwrap(Result<T> result) {
    if (!result)
        throw_error(result.error());
    return std::move(result).value();
}

inline void unwrap_void(Result<void> result) {
    if (!result)
        throw_error(result.error());
}

inline std::span<const double> view(const Configuration& values) {
    return std::span<const double>(values.data(), values.size());
}

void bind_policy(py::module_& module);

} // namespace rbfsafe::python_binding
