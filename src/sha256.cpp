#include "internal/sha256.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace rbfsafe::internal {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

class Sha256 {
  public:
    void update(std::span<const std::byte> input) {
        for (const auto byte : input) {
            buffer_[buffer_size_++] = static_cast<std::uint8_t>(byte);
            ++byte_count_;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_.data());
                buffer_size_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bit_count = byte_count_ * 8u;
        buffer_[buffer_size_++] = 0x80u;
        if (buffer_size_ > 56u) {
            while (buffer_size_ < 64u)
                buffer_[buffer_size_++] = 0u;
            transform(buffer_.data());
            buffer_size_ = 0;
        }
        while (buffer_size_ < 56u)
            buffer_[buffer_size_++] = 0u;
        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer_[buffer_size_++] = static_cast<std::uint8_t>((bit_count >> shift) & 0xffu);
        }
        transform(buffer_.data());

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t index = 0; index < state_.size(); ++index) {
            digest[index * 4] = static_cast<std::uint8_t>((state_[index] >> 24u) & 0xffu);
            digest[index * 4 + 1] = static_cast<std::uint8_t>((state_[index] >> 16u) & 0xffu);
            digest[index * 4 + 2] = static_cast<std::uint8_t>((state_[index] >> 8u) & 0xffu);
            digest[index * 4 + 3] = static_cast<std::uint8_t>(state_[index] & 0xffu);
        }
        return digest;
    }

  private:
    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = index * 4;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24u) |
                           (static_cast<std::uint32_t>(block[offset + 1]) << 16u) |
                           (static_cast<std::uint32_t>(block[offset + 2]) << 8u) |
                           static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15], 7u) ^ rotate_right(words[index - 15], 18u) ^
                            (words[index - 15] >> 3u);
            const auto s1 = rotate_right(words[index - 2], 17u) ^ rotate_right(words[index - 2], 19u) ^
                            (words[index - 2] >> 10u);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choose + kRoundConstants[index] + words[index];
            const auto sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t byte_count_ = 0;
};

std::string hex_digest(const std::array<std::uint8_t, 32>& digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

} // namespace

std::string sha256(std::span<const std::byte> bytes) {
    Sha256 state;
    state.update(bytes);
    return hex_digest(state.finish());
}

std::string sha256(std::string_view text) {
    return sha256(std::as_bytes(std::span(text.data(), text.size())));
}

Result<std::string> sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(StatusCode::IoError, "failed to open file for hashing",
                                            path.string());
    }
    Sha256 state;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            state.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(count))));
        }
    }
    if (!input.eof()) {
        return Result<std::string>::failure(StatusCode::IoError, "failed while hashing file", path.string());
    }
    return hex_digest(state.finish());
}

} // namespace rbfsafe::internal
