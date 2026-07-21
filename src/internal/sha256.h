#pragma once

#include <rbfsafe/result.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace rbfsafe::internal {

std::string sha256(std::span<const std::byte> bytes);
std::string sha256(std::string_view text);
Result<std::string> sha256_file(const std::filesystem::path& path);

} // namespace rbfsafe::internal
