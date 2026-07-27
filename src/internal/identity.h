#pragma once

#include <rbfsafe/identity.h>

#include <span>
#include <string>
#include <vector>

namespace rbfsafe::internal {

std::string service_public_key_identity(const ServicePublicKey& key);
std::string service_trust_bundle_identity(const ServiceTrustBundle& bundle);

std::string encode_hex(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> decode_hex(const std::string& text, std::size_t expected_bytes);

} // namespace rbfsafe::internal
