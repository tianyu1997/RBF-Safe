#pragma once

#include <rbfsafe/result.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe::internal {

class BinaryWriter {
  public:
    void bytes(std::span<const std::byte> value) { data_.insert(data_.end(), value.begin(), value.end()); }
    void text(std::string_view value) { bytes(std::as_bytes(std::span(value.data(), value.size()))); }
    void u8(std::uint8_t value) { data_.push_back(static_cast<std::byte>(value)); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void string(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        text(value);
    }
    const std::vector<std::byte>& data() const noexcept { return data_; }
    Result<void> save(const std::filesystem::path& path) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return Result<void>::failure(StatusCode::IoError, "failed to open binary output", path.string());
        output.write(reinterpret_cast<const char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
        output.flush();
        if (!output)
            return Result<void>::failure(StatusCode::IoError, "failed to write binary output", path.string());
        return Result<void>::success();
    }

  private:
    std::vector<std::byte> data_;
};

class BinaryReader {
  public:
    explicit BinaryReader(std::span<const std::byte> data) : data_(data) {}

    std::size_t remaining() const noexcept { return data_.size() - position_; }
    std::size_t position() const noexcept { return position_; }
    bool finished() const noexcept { return position_ == data_.size(); }

    Result<std::uint8_t> u8() {
        if (remaining() < 1)
            return truncated("u8");
        return static_cast<std::uint8_t>(data_[position_++]);
    }
    Result<std::uint32_t> u32() {
        if (remaining() < 4)
            return truncated32("u32");
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
        }
        return value;
    }
    Result<std::uint64_t> u64() {
        if (remaining() < 8)
            return truncated64("u64");
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
        }
        return value;
    }
    Result<double> f64() {
        auto bits = u64();
        if (!bits)
            return bits.error();
        return std::bit_cast<double>(bits.value());
    }
    Result<std::string> string(std::size_t maximum = 1024 * 1024) {
        auto length = u32();
        if (!length)
            return length.error();
        if (length.value() > maximum) {
            return Result<std::string>::failure(StatusCode::ResourceLimit, "binary string exceeds limit");
        }
        if (remaining() < length.value())
            return truncated_string();
        const char* begin = reinterpret_cast<const char*>(data_.data() + position_);
        std::string value(begin, begin + length.value());
        position_ += length.value();
        return value;
    }
    Result<std::string> fixed_text(std::size_t count) {
        if (remaining() < count)
            return truncated_string();
        const char* begin = reinterpret_cast<const char*>(data_.data() + position_);
        std::string value(begin, begin + count);
        position_ += count;
        return value;
    }

  private:
    Result<std::uint8_t> truncated(std::string context) const {
        return Result<std::uint8_t>::failure(StatusCode::CorruptData, "truncated binary payload",
                                             std::move(context));
    }
    Result<std::uint32_t> truncated32(std::string context) const {
        return Result<std::uint32_t>::failure(StatusCode::CorruptData, "truncated binary payload",
                                              std::move(context));
    }
    Result<std::uint64_t> truncated64(std::string context) const {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "truncated binary payload",
                                              std::move(context));
    }
    Result<std::string> truncated_string() const {
        return Result<std::string>::failure(StatusCode::CorruptData, "truncated binary string");
    }

    std::span<const std::byte> data_;
    std::size_t position_ = 0;
};

inline Result<std::vector<std::byte>> read_binary_file(const std::filesystem::path& path,
                                                       std::uintmax_t maximum_size = 2ull * 1024ull *
                                                                                     1024ull * 1024ull) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return Result<std::vector<std::byte>>::failure(StatusCode::IoError, "failed to stat binary file",
                                                       path.string());
    if (size > maximum_size)
        return Result<std::vector<std::byte>>::failure(StatusCode::ResourceLimit,
                                                       "binary file exceeds resource limit", path.string());
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Result<std::vector<std::byte>>::failure(StatusCode::IoError, "failed to open binary file",
                                                       path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty())
        return Result<std::vector<std::byte>>::failure(StatusCode::IoError, "failed to read binary file",
                                                       path.string());
    return bytes;
}

} // namespace rbfsafe::internal
