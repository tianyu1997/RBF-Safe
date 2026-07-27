#pragma once

#include <rbfsafe/identity.h>

#include <span>
#include <string>
#include <vector>

namespace rbfsafe::internal {

std::string service_public_key_identity(const ServicePublicKey& key);
std::string service_trust_bundle_identity(const ServiceTrustBundle& bundle);
std::string service_trust_bundle_storage_document(const ServiceTrustBundle& bundle);
std::string service_trust_bundle_authorization_message(const ServiceTrustBundleAuthorization& authorization);
std::string service_trust_bundle_authorization_identity(const ServiceTrustBundleAuthorization& authorization);
std::string
service_trust_bundle_authorization_set_identity(const ServiceTrustBundleAuthorizationSet& authorization_set);
std::string service_trust_rotation_record_identity(const ServiceTrustRotationRecord& record);
std::string service_trust_checkpoint_signature_message(const ServiceTrustCheckpoint& checkpoint,
                                                       const ServiceTrustCheckpointSignature& signature);
std::string service_trust_checkpoint_identity(const ServiceTrustCheckpoint& checkpoint);

Result<void> validate_service_trust_bundle_rotation(const ServiceTrustBundle& predecessor,
                                                    const ServiceTrustBundle& successor);

std::string encode_hex(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> decode_hex(const std::string& text, std::size_t expected_bytes);

} // namespace rbfsafe::internal
