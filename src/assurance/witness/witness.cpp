#include <rbfsafe/modules/assurance.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"
#include "internal/witness.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;
using internal::sha256;

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumWitnesses = 100'000;

bool valid_text(std::string_view value, std::size_t maximum = kMaximumIdentifierBytes) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return character >= 0x20U && character != 0x7fU; });
}

bool valid_authentication_tag(std::string_view value) {
    return value.size() == kEd25519SignatureBytes * 2U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0 || (character >= static_cast<unsigned char>('a') &&
                                                       character <= static_cast<unsigned char>('f'));
           });
}

bool valid_conflict_type(TransparencyGossipConflictType type) {
    return type == TransparencyGossipConflictType::SameSizeEquivocation ||
           type == TransparencyGossipConflictType::InvalidConsistencyProof;
}

bool valid_gossip_status(TransparencyGossipStatus status) {
    return status == TransparencyGossipStatus::Consistent || status == TransparencyGossipStatus::Incomplete ||
           status == TransparencyGossipStatus::SplitView;
}

Json witness_policy_json(const TransparencyCheckpointWitnessPolicy& policy) {
    return Json::Object{
        {"exclude_log_signer", policy.exclude_log_signer},
        {"minimum_witnesses", std::to_string(policy.minimum_witnesses)},
        {"require_distinct_services", policy.require_distinct_services},
    };
}

Json checkpoint_cosignature_json(const TransparencyCheckpointCosignature& cosignature, bool include_id,
                                 bool include_tag) {
    Json::Object object{
        {"algorithm", static_cast<int>(cosignature.algorithm)},
        {"checkpoint_id", cosignature.checkpoint_id},
        {"log_id", cosignature.log_id},
        {"root_hash", cosignature.root_hash},
        {"storage_schema", std::to_string(cosignature.storage_schema)},
        {"tree_size", std::to_string(cosignature.tree_size)},
        {"trust_bundle_id", cosignature.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(cosignature.trust_bundle_sequence)},
        {"witness_key_id", cosignature.witness_key_id},
        {"witness_service_id", cosignature.witness_service_id},
    };
    if (include_tag)
        object.emplace("authentication_tag", cosignature.authentication_tag);
    if (include_id)
        object.emplace("id", cosignature.id);
    return Json(std::move(object));
}

Json witnessed_checkpoint_json(const WitnessedTransparencyCheckpoint& witnessed_checkpoint, bool include_id) {
    Json::Array cosignatures;
    cosignatures.reserve(witnessed_checkpoint.cosignatures.size());
    for (const auto& cosignature : witnessed_checkpoint.cosignatures)
        cosignatures.emplace_back(checkpoint_cosignature_json(cosignature, true, true));
    Json::Object object{
        {"checkpoint_id", witnessed_checkpoint.checkpoint.id},
        {"cosignatures", Json(std::move(cosignatures))},
        {"log_id", witnessed_checkpoint.log_id},
        {"policy", witness_policy_json(witnessed_checkpoint.policy)},
        {"storage_schema", std::to_string(witnessed_checkpoint.storage_schema)},
        {"trust_bundle_id", witnessed_checkpoint.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(witnessed_checkpoint.trust_bundle_sequence)},
    };
    if (include_id)
        object.emplace("id", witnessed_checkpoint.id);
    return Json(std::move(object));
}

Json checkpoint_gossip_json(const TransparencyCheckpointGossip& gossip, bool include_id, bool include_tag) {
    Json::Object object{
        {"algorithm", static_cast<int>(gossip.algorithm)},
        {"consistency_proof_id", gossip.consistency_proof ? gossip.consistency_proof->id : std::string{}},
        {"log_id", gossip.log_id},
        {"parent_gossip_id", gossip.parent_gossip_id},
        {"recipient_service_id", gossip.recipient_service_id},
        {"sender_key_id", gossip.sender_key_id},
        {"sender_sequence", std::to_string(gossip.sender_sequence)},
        {"sender_service_id", gossip.sender_service_id},
        {"storage_schema", std::to_string(gossip.storage_schema)},
        {"trust_bundle_id", gossip.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(gossip.trust_bundle_sequence)},
        {"witnessed_checkpoint_id", gossip.witnessed_checkpoint.id},
    };
    if (include_tag)
        object.emplace("authentication_tag", gossip.authentication_tag);
    if (include_id)
        object.emplace("id", gossip.id);
    return Json(std::move(object));
}

Json gossip_conflict_json(const TransparencyGossipConflict& conflict, bool include_id) {
    Json::Object object{
        {"consistency_proof_id", conflict.consistency_proof_id},
        {"first_checkpoint_id", conflict.first_checkpoint_id},
        {"first_gossip_id", conflict.first_gossip_id},
        {"first_tree_size", std::to_string(conflict.first_tree_size)},
        {"second_checkpoint_id", conflict.second_checkpoint_id},
        {"second_gossip_id", conflict.second_gossip_id},
        {"second_tree_size", std::to_string(conflict.second_tree_size)},
        {"type", static_cast<int>(conflict.type)},
    };
    if (include_id)
        object.emplace("id", conflict.id);
    return Json(std::move(object));
}

Json gossip_audit_report_json(const TransparencyGossipAuditReport& report, bool include_id) {
    Json::Array conflicts;
    conflicts.reserve(report.conflicts.size());
    for (const auto& conflict : report.conflicts)
        conflicts.emplace_back(gossip_conflict_json(conflict, true));
    Json::Object object{
        {"authenticated_gossip_count", std::to_string(report.authenticated_gossip_count)},
        {"conflicts", Json(std::move(conflicts))},
        {"linked_checkpoint_pairs", std::to_string(report.linked_checkpoint_pairs)},
        {"log_id", report.log_id},
        {"status", static_cast<int>(report.status)},
        {"trust_bundle_id", report.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(report.trust_bundle_sequence)},
        {"unique_checkpoint_count", std::to_string(report.unique_checkpoint_count)},
        {"unlinked_checkpoint_pairs", std::to_string(report.unlinked_checkpoint_pairs)},
    };
    if (include_id)
        object.emplace("id", report.id);
    return Json(std::move(object));
}

Json gossip_record_json(const TransparencyGossipRecord& record, bool include_id) {
    Json::Object object{
        {"gossip_id", record.gossip.id},
        {"log_id", record.log_id},
        {"parent_id", record.parent_id},
        {"sequence", std::to_string(record.sequence)},
        {"storage_schema", std::to_string(record.storage_schema)},
        {"trust_bundle_id", record.trust_bundle_id},
    };
    if (include_id)
        object.emplace("id", record.id);
    return Json(std::move(object));
}

Result<ServicePublicKey> active_publication_key(const ServiceTrustBundle& trust_bundle,
                                                const std::string& service_id, const std::string& key_id) {
    auto trusted = trusted_service_public_key(trust_bundle, service_id, key_id,
                                              ArtifactTransferOperation::Publish, trust_bundle.sequence());
    if (!trusted)
        return trusted.error();
    if (trusted.value().state != ServiceKeyState::Active) {
        return Result<ServicePublicKey>::failure(StatusCode::IdentityMismatch,
                                                 "transparency witness or gossip key is not active",
                                                 trusted.value().id);
    }
    return trusted.value();
}

Result<void> validate_witness_policy(const WitnessedTransparencyCheckpoint& witnessed_checkpoint) {
    if (witnessed_checkpoint.cosignatures.size() < witnessed_checkpoint.policy.minimum_witnesses) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency checkpoint witness quorum is not satisfied",
                                     witnessed_checkpoint.id);
    }
    std::set<std::string> services;
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& cosignature : witnessed_checkpoint.cosignatures) {
        if (!keys.emplace(cosignature.witness_service_id, cosignature.witness_key_id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "transparency checkpoint contains a duplicate witness key",
                                         cosignature.witness_key_id);
        }
        if (witnessed_checkpoint.policy.require_distinct_services &&
            !services.emplace(cosignature.witness_service_id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "transparency checkpoint witnesses are not service-distinct",
                                         cosignature.witness_service_id);
        }
        services.emplace(cosignature.witness_service_id);
        if (witnessed_checkpoint.policy.exclude_log_signer &&
            cosignature.witness_service_id == witnessed_checkpoint.checkpoint.signer_service_id) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "transparency log signer cannot satisfy the independent witness quorum",
                cosignature.witness_service_id);
        }
    }
    return Result<void>::success();
}

struct CheckpointGroup {
    const TransparencyLogCheckpoint* checkpoint = nullptr;
    std::vector<const TransparencyCheckpointGossip*> gossip;
};

} // namespace

std::string
internal::transparency_checkpoint_cosignature_message(const TransparencyCheckpointCosignature& cosignature) {
    return std::string("rbfsafe-transparency-checkpoint-witness-signature-v1\n") +
           checkpoint_cosignature_json(cosignature, false, false).dump(false);
}

std::string
internal::transparency_checkpoint_cosignature_identity(const TransparencyCheckpointCosignature& cosignature) {
    return sha256(std::string("rbfsafe-transparency-checkpoint-witness-identity-v1\n") +
                  checkpoint_cosignature_json(cosignature, false, true).dump(false));
}

std::string internal::witnessed_transparency_checkpoint_identity(
    const WitnessedTransparencyCheckpoint& witnessed_checkpoint) {
    return sha256(std::string("rbfsafe-witnessed-transparency-checkpoint-identity-v1\n") +
                  witnessed_checkpoint_json(witnessed_checkpoint, false).dump(false));
}

std::string internal::transparency_checkpoint_gossip_message(const TransparencyCheckpointGossip& gossip) {
    return std::string("rbfsafe-transparency-checkpoint-gossip-signature-v1\n") +
           checkpoint_gossip_json(gossip, false, false).dump(false);
}

std::string internal::transparency_checkpoint_gossip_identity(const TransparencyCheckpointGossip& gossip) {
    return sha256(std::string("rbfsafe-transparency-checkpoint-gossip-identity-v1\n") +
                  checkpoint_gossip_json(gossip, false, true).dump(false));
}

std::string internal::transparency_gossip_conflict_identity(const TransparencyGossipConflict& conflict) {
    return sha256(std::string("rbfsafe-transparency-gossip-conflict-identity-v1\n") +
                  gossip_conflict_json(conflict, false).dump(false));
}

std::string internal::transparency_gossip_audit_report_identity(const TransparencyGossipAuditReport& report) {
    return sha256(std::string("rbfsafe-transparency-gossip-audit-report-identity-v1\n") +
                  gossip_audit_report_json(report, false).dump(false));
}

std::string internal::transparency_gossip_record_identity(const TransparencyGossipRecord& record) {
    return sha256(std::string("rbfsafe-transparency-gossip-record-identity-v1\n") +
                  gossip_record_json(record, false).dump(false));
}

bool valid_transparency_checkpoint_witness_policy(const TransparencyCheckpointWitnessPolicy& policy) {
    return policy.minimum_witnesses > 0 && policy.minimum_witnesses <= kMaximumWitnesses;
}

bool TransparencyCheckpointCosignature::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           internal::valid_sha256(checkpoint_id) && tree_size > 0 && internal::valid_sha256(root_hash) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           valid_text(witness_service_id) && internal::valid_sha256(witness_key_id) &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_authentication_tag(authentication_tag) &&
           id == internal::transparency_checkpoint_cosignature_identity(*this);
}

Result<TransparencyCheckpointCosignature> sign_transparency_checkpoint_witness(
    const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
    const ServiceTrustBundle& trust_bundle, std::string witness_service_id, std::string witness_key_id,
    std::span<const std::byte> ed25519_secret_key) {
    if (!identity.valid() || !checkpoint.valid() || !trust_bundle.valid() ||
        !valid_text(witness_service_id) || !internal::valid_sha256(witness_key_id) ||
        ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<TransparencyCheckpointCosignature>::failure(
            StatusCode::InvalidArgument, "transparency checkpoint witness signing input is invalid");
    }
    auto checkpoint_verified = verify_transparency_log_checkpoint(identity, checkpoint);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    auto trusted = active_publication_key(trust_bundle, witness_service_id, witness_key_id);
    if (!trusted)
        return trusted.error();
    TransparencyCheckpointCosignature cosignature;
    cosignature.log_id = identity.id;
    cosignature.checkpoint_id = checkpoint.id;
    cosignature.tree_size = checkpoint.tree_size;
    cosignature.root_hash = checkpoint.root_hash;
    cosignature.trust_bundle_id = trust_bundle.id();
    cosignature.trust_bundle_sequence = trust_bundle.sequence();
    cosignature.witness_service_id = std::move(witness_service_id);
    cosignature.witness_key_id = std::move(witness_key_id);
    const auto message = internal::transparency_checkpoint_cosignature_message(cosignature);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    cosignature.authentication_tag = internal::encode_hex(signature.value());
    cosignature.id = internal::transparency_checkpoint_cosignature_identity(cosignature);
    auto verified = verify_transparency_checkpoint_witness(identity, checkpoint, cosignature, trust_bundle);
    if (!verified)
        return verified.error();
    return cosignature;
}

Result<void> verify_transparency_checkpoint_witness(const TransparencyLogIdentity& identity,
                                                    const TransparencyLogCheckpoint& checkpoint,
                                                    const TransparencyCheckpointCosignature& cosignature,
                                                    const ServiceTrustBundle& trust_bundle) {
    if (!identity.valid() || !checkpoint.valid() || !cosignature.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency checkpoint witness verification input is invalid");
    }
    auto checkpoint_verified = verify_transparency_log_checkpoint(identity, checkpoint);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    if (cosignature.log_id != identity.id || cosignature.checkpoint_id != checkpoint.id ||
        cosignature.tree_size != checkpoint.tree_size || cosignature.root_hash != checkpoint.root_hash ||
        cosignature.trust_bundle_id != trust_bundle.id() ||
        cosignature.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency checkpoint witness binding mismatch", cosignature.id);
    }
    auto trusted =
        active_publication_key(trust_bundle, cosignature.witness_service_id, cosignature.witness_key_id);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(cosignature.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::transparency_checkpoint_cosignature_message(cosignature);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

bool WitnessedTransparencyCheckpoint::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !internal::valid_sha256(log_id) ||
        !internal::valid_sha256(trust_bundle_id) || trust_bundle_sequence == 0 ||
        !valid_transparency_checkpoint_witness_policy(policy) || !checkpoint.valid() ||
        checkpoint.log_id != log_id || cosignatures.size() < policy.minimum_witnesses ||
        cosignatures.size() > kMaximumWitnesses ||
        id != internal::witnessed_transparency_checkpoint_identity(*this)) {
        return false;
    }
    for (std::size_t index = 0; index < cosignatures.size(); ++index) {
        const auto& cosignature = cosignatures[index];
        if (!cosignature.valid() || cosignature.log_id != log_id ||
            cosignature.checkpoint_id != checkpoint.id || cosignature.tree_size != checkpoint.tree_size ||
            cosignature.root_hash != checkpoint.root_hash || cosignature.trust_bundle_id != trust_bundle_id ||
            cosignature.trust_bundle_sequence != trust_bundle_sequence ||
            (index > 0 &&
             std::tie(cosignatures[index - 1].witness_service_id, cosignatures[index - 1].witness_key_id) >=
                 std::tie(cosignature.witness_service_id, cosignature.witness_key_id))) {
            return false;
        }
        if (policy.require_distinct_services && index > 0 &&
            cosignatures[index - 1].witness_service_id == cosignature.witness_service_id) {
            return false;
        }
        if (policy.exclude_log_signer && cosignature.witness_service_id == checkpoint.signer_service_id) {
            return false;
        }
    }
    return true;
}

Result<WitnessedTransparencyCheckpoint> assemble_witnessed_transparency_checkpoint(
    const TransparencyLogIdentity& identity, TransparencyLogCheckpoint checkpoint,
    TransparencyCheckpointWitnessPolicy policy, std::vector<TransparencyCheckpointCosignature> cosignatures,
    const ServiceTrustBundle& trust_bundle) {
    if (!identity.valid() || !checkpoint.valid() || !valid_transparency_checkpoint_witness_policy(policy) ||
        !trust_bundle.valid() || cosignatures.size() > kMaximumWitnesses) {
        return Result<WitnessedTransparencyCheckpoint>::failure(
            StatusCode::InvalidArgument, "witnessed transparency checkpoint assembly input is invalid");
    }
    auto checkpoint_verified = verify_transparency_log_checkpoint(identity, checkpoint);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    std::sort(cosignatures.begin(), cosignatures.end(), [](const auto& first, const auto& second) {
        return std::tie(first.witness_service_id, first.witness_key_id) <
               std::tie(second.witness_service_id, second.witness_key_id);
    });
    WitnessedTransparencyCheckpoint result;
    result.log_id = identity.id;
    result.trust_bundle_id = trust_bundle.id();
    result.trust_bundle_sequence = trust_bundle.sequence();
    result.policy = policy;
    result.checkpoint = std::move(checkpoint);
    result.cosignatures = std::move(cosignatures);
    for (const auto& cosignature : result.cosignatures) {
        auto verified =
            verify_transparency_checkpoint_witness(identity, result.checkpoint, cosignature, trust_bundle);
        if (!verified)
            return verified.error();
    }
    auto policy_verified = validate_witness_policy(result);
    if (!policy_verified)
        return policy_verified.error();
    result.id = internal::witnessed_transparency_checkpoint_identity(result);
    if (!result.valid()) {
        return Result<WitnessedTransparencyCheckpoint>::failure(
            StatusCode::IdentityMismatch, "witnessed transparency checkpoint is not canonical");
    }
    return result;
}

Result<void>
verify_witnessed_transparency_checkpoint(const TransparencyLogIdentity& identity,
                                         const WitnessedTransparencyCheckpoint& witnessed_checkpoint,
                                         const ServiceTrustBundle& trust_bundle) {
    if (!identity.valid() || !witnessed_checkpoint.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "witnessed transparency checkpoint verification input is invalid");
    }
    if (witnessed_checkpoint.log_id != identity.id ||
        witnessed_checkpoint.trust_bundle_id != trust_bundle.id() ||
        witnessed_checkpoint.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "witnessed transparency checkpoint trust or log binding mismatch",
                                     witnessed_checkpoint.id);
    }
    auto checkpoint_verified = verify_transparency_log_checkpoint(identity, witnessed_checkpoint.checkpoint);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    auto policy_verified = validate_witness_policy(witnessed_checkpoint);
    if (!policy_verified)
        return policy_verified.error();
    for (const auto& cosignature : witnessed_checkpoint.cosignatures) {
        auto verified = verify_transparency_checkpoint_witness(identity, witnessed_checkpoint.checkpoint,
                                                               cosignature, trust_bundle);
        if (!verified)
            return verified.error();
    }
    return Result<void>::success();
}

bool TransparencyCheckpointGossip::valid() const {
    const bool parent_valid =
        sender_sequence == 1 ? parent_gossip_id.empty() : internal::valid_sha256(parent_gossip_id);
    const bool consistency_valid =
        !consistency_proof ||
        (consistency_proof->valid() && consistency_proof->log_id == log_id &&
         consistency_proof->new_checkpoint_id == witnessed_checkpoint.checkpoint.id &&
         consistency_proof->new_tree_size == witnessed_checkpoint.checkpoint.tree_size &&
         consistency_proof->new_root_hash == witnessed_checkpoint.checkpoint.root_hash);
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           sender_sequence > 0 && parent_valid && valid_text(recipient_service_id) &&
           valid_text(sender_service_id) && internal::valid_sha256(sender_key_id) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           witnessed_checkpoint.valid() && witnessed_checkpoint.log_id == log_id &&
           witnessed_checkpoint.trust_bundle_id == trust_bundle_id &&
           witnessed_checkpoint.trust_bundle_sequence == trust_bundle_sequence && consistency_valid &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_authentication_tag(authentication_tag) &&
           id == internal::transparency_checkpoint_gossip_identity(*this);
}

Result<TransparencyCheckpointGossip> sign_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, WitnessedTransparencyCheckpoint witnessed_checkpoint,
    std::optional<TransparencyCompactConsistencyProof> consistency_proof, std::string recipient_service_id,
    std::uint64_t sender_sequence, std::string parent_gossip_id, const ServiceTrustBundle& trust_bundle,
    std::string sender_service_id, std::string sender_key_id, std::span<const std::byte> ed25519_secret_key) {
    if (!identity.valid() || !witnessed_checkpoint.valid() || !trust_bundle.valid() ||
        !valid_text(recipient_service_id) || sender_sequence == 0 ||
        (sender_sequence == 1 ? !parent_gossip_id.empty() : !internal::valid_sha256(parent_gossip_id)) ||
        !valid_text(sender_service_id) || !internal::valid_sha256(sender_key_id) ||
        ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<TransparencyCheckpointGossip>::failure(
            StatusCode::InvalidArgument, "transparency checkpoint gossip signing input is invalid");
    }
    auto witnessed_verified =
        verify_witnessed_transparency_checkpoint(identity, witnessed_checkpoint, trust_bundle);
    if (!witnessed_verified)
        return witnessed_verified.error();
    if (consistency_proof &&
        (consistency_proof->log_id != identity.id ||
         consistency_proof->new_checkpoint_id != witnessed_checkpoint.checkpoint.id ||
         consistency_proof->new_tree_size != witnessed_checkpoint.checkpoint.tree_size ||
         consistency_proof->new_root_hash != witnessed_checkpoint.checkpoint.root_hash)) {
        return Result<TransparencyCheckpointGossip>::failure(
            StatusCode::IdentityMismatch, "transparency gossip consistency proof targets another checkpoint",
            consistency_proof->id);
    }
    auto trusted = active_publication_key(trust_bundle, sender_service_id, sender_key_id);
    if (!trusted)
        return trusted.error();
    TransparencyCheckpointGossip result;
    result.log_id = identity.id;
    result.sender_sequence = sender_sequence;
    result.parent_gossip_id = std::move(parent_gossip_id);
    result.recipient_service_id = std::move(recipient_service_id);
    result.sender_service_id = std::move(sender_service_id);
    result.sender_key_id = std::move(sender_key_id);
    result.trust_bundle_id = trust_bundle.id();
    result.trust_bundle_sequence = trust_bundle.sequence();
    result.witnessed_checkpoint = std::move(witnessed_checkpoint);
    result.consistency_proof = std::move(consistency_proof);
    const auto message = internal::transparency_checkpoint_gossip_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::transparency_checkpoint_gossip_identity(result);
    auto verified = verify_transparency_checkpoint_gossip(identity, result, trust_bundle);
    if (!verified)
        return verified.error();
    return result;
}

Result<void> verify_transparency_checkpoint_gossip(const TransparencyLogIdentity& identity,
                                                   const TransparencyCheckpointGossip& gossip,
                                                   const ServiceTrustBundle& trust_bundle) {
    if (!identity.valid() || !gossip.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency checkpoint gossip verification input is invalid");
    }
    if (gossip.log_id != identity.id || gossip.trust_bundle_id != trust_bundle.id() ||
        gossip.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency checkpoint gossip trust or log binding mismatch",
                                     gossip.id);
    }
    auto witnessed_verified =
        verify_witnessed_transparency_checkpoint(identity, gossip.witnessed_checkpoint, trust_bundle);
    if (!witnessed_verified)
        return witnessed_verified.error();
    auto trusted = active_publication_key(trust_bundle, gossip.sender_service_id, gossip.sender_key_id);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(gossip.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::transparency_checkpoint_gossip_message(gossip);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

bool TransparencyGossipConflict::valid() const {
    if (!internal::valid_sha256(id) || !valid_conflict_type(type) ||
        !internal::valid_sha256(first_gossip_id) || !internal::valid_sha256(second_gossip_id) ||
        !internal::valid_sha256(first_checkpoint_id) || !internal::valid_sha256(second_checkpoint_id) ||
        first_tree_size == 0 || second_tree_size == 0 ||
        id != internal::transparency_gossip_conflict_identity(*this)) {
        return false;
    }
    if (type == TransparencyGossipConflictType::SameSizeEquivocation) {
        return first_tree_size == second_tree_size && first_checkpoint_id != second_checkpoint_id &&
               consistency_proof_id.empty();
    }
    return first_tree_size < second_tree_size && internal::valid_sha256(consistency_proof_id);
}

bool TransparencyGossipAuditReport::valid() const {
    if (!internal::valid_sha256(id) || !internal::valid_sha256(log_id) ||
        !internal::valid_sha256(trust_bundle_id) || trust_bundle_sequence == 0 ||
        !valid_gossip_status(status) || unique_checkpoint_count > authenticated_gossip_count ||
        id != internal::transparency_gossip_audit_report_identity(*this)) {
        return false;
    }
    for (std::size_t index = 0; index < conflicts.size(); ++index) {
        if (!conflicts[index].valid() || (index > 0 && conflicts[index - 1].id >= conflicts[index].id)) {
            return false;
        }
    }
    if (!conflicts.empty())
        return status == TransparencyGossipStatus::SplitView;
    if (unlinked_checkpoint_pairs > 0)
        return status == TransparencyGossipStatus::Incomplete;
    return status == TransparencyGossipStatus::Consistent;
}

bool TransparencyGossipRecord::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) &&
           (sequence == 0 ? parent_id.empty() : internal::valid_sha256(parent_id)) &&
           internal::valid_sha256(log_id) && internal::valid_sha256(trust_bundle_id) && gossip.valid() &&
           gossip.log_id == log_id && gossip.trust_bundle_id == trust_bundle_id &&
           id == internal::transparency_gossip_record_identity(*this);
}

Result<TransparencyGossipAuditReport> audit_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, std::span<const TransparencyCheckpointGossip> gossip,
    const ServiceTrustBundle& trust_bundle, const TransparencyGossipAuditOptions& options) {
    if (!identity.valid() || !trust_bundle.valid() || options.maximum_gossip_messages == 0 ||
        options.maximum_unique_checkpoints == 0 || options.maximum_pair_checks == 0 ||
        options.maximum_graph_steps == 0) {
        return Result<TransparencyGossipAuditReport>::failure(
            StatusCode::InvalidArgument, "transparency gossip audit input or options are invalid");
    }
    if (gossip.size() > options.maximum_gossip_messages) {
        return Result<TransparencyGossipAuditReport>::failure(
            StatusCode::ResourceLimit, "transparency gossip message count exceeds configured limit");
    }
    if (options.cancellation.cancelled()) {
        return Result<TransparencyGossipAuditReport>::failure(StatusCode::Cancelled,
                                                              "transparency gossip audit was cancelled");
    }

    std::set<std::string> gossip_ids;
    std::map<std::string, CheckpointGroup> groups_by_id;
    for (const auto& message : gossip) {
        if (options.cancellation.cancelled()) {
            return Result<TransparencyGossipAuditReport>::failure(StatusCode::Cancelled,
                                                                  "transparency gossip audit was cancelled");
        }
        auto verified = verify_transparency_checkpoint_gossip(identity, message, trust_bundle);
        if (!verified)
            return verified.error();
        if (!gossip_ids.insert(message.id).second) {
            return Result<TransparencyGossipAuditReport>::failure(
                StatusCode::InvalidArgument, "transparency gossip audit contains a duplicate message",
                message.id);
        }
        auto& group = groups_by_id[message.witnessed_checkpoint.checkpoint.id];
        group.checkpoint = &message.witnessed_checkpoint.checkpoint;
        group.gossip.push_back(&message);
        if (groups_by_id.size() > options.maximum_unique_checkpoints) {
            return Result<TransparencyGossipAuditReport>::failure(
                StatusCode::ResourceLimit,
                "transparency gossip unique checkpoint count exceeds configured limit");
        }
    }

    std::vector<CheckpointGroup> groups;
    groups.reserve(groups_by_id.size());
    for (auto& entry : groups_by_id) {
        auto& messages = entry.second.gossip;
        std::sort(messages.begin(), messages.end(),
                  [](const auto* first, const auto* second) { return first->id < second->id; });
        groups.push_back(std::move(entry.second));
    }
    std::sort(groups.begin(), groups.end(), [](const auto& first, const auto& second) {
        return std::tie(first.checkpoint->tree_size, first.checkpoint->id) <
               std::tie(second.checkpoint->tree_size, second.checkpoint->id);
    });

    TransparencyGossipAuditReport report;
    report.log_id = identity.id;
    report.trust_bundle_id = trust_bundle.id();
    report.trust_bundle_sequence = trust_bundle.sequence();
    report.authenticated_gossip_count = gossip.size();
    report.unique_checkpoint_count = groups.size();

    std::map<std::string, std::size_t> group_index;
    for (std::size_t index = 0; index < groups.size(); ++index)
        group_index.emplace(groups[index].checkpoint->id, index);
    std::vector<std::vector<std::size_t>> edges(groups.size());
    std::size_t pair_checks = 0;

    for (std::size_t first = 0; first < groups.size(); ++first) {
        for (std::size_t second = first + 1; second < groups.size(); ++second) {
            if (groups[first].checkpoint->tree_size != groups[second].checkpoint->tree_size) {
                break;
            }
            if (++pair_checks > options.maximum_pair_checks) {
                return Result<TransparencyGossipAuditReport>::failure(
                    StatusCode::ResourceLimit,
                    "transparency gossip checkpoint-pair checks exceed configured limit");
            }
            TransparencyGossipConflict conflict;
            conflict.type = TransparencyGossipConflictType::SameSizeEquivocation;
            conflict.first_gossip_id = groups[first].gossip.front()->id;
            conflict.second_gossip_id = groups[second].gossip.front()->id;
            conflict.first_checkpoint_id = groups[first].checkpoint->id;
            conflict.second_checkpoint_id = groups[second].checkpoint->id;
            conflict.first_tree_size = groups[first].checkpoint->tree_size;
            conflict.second_tree_size = groups[second].checkpoint->tree_size;
            conflict.id = internal::transparency_gossip_conflict_identity(conflict);
            report.conflicts.push_back(std::move(conflict));
        }
    }

    for (std::size_t current = 0; current < groups.size(); ++current) {
        for (const auto* message : groups[current].gossip) {
            if (!message->consistency_proof)
                continue;
            const auto& proof = *message->consistency_proof;
            const auto old = group_index.find(proof.old_checkpoint_id);
            if (old == group_index.end())
                continue;
            auto verified = verify_transparency_compact_consistency(identity, *groups[old->second].checkpoint,
                                                                    *groups[current].checkpoint, proof);
            if (verified) {
                edges[old->second].push_back(current);
                continue;
            }
            TransparencyGossipConflict conflict;
            conflict.type = TransparencyGossipConflictType::InvalidConsistencyProof;
            conflict.first_gossip_id = groups[old->second].gossip.front()->id;
            conflict.second_gossip_id = message->id;
            conflict.first_checkpoint_id = groups[old->second].checkpoint->id;
            conflict.second_checkpoint_id = groups[current].checkpoint->id;
            conflict.first_tree_size = groups[old->second].checkpoint->tree_size;
            conflict.second_tree_size = groups[current].checkpoint->tree_size;
            conflict.consistency_proof_id = proof.id;
            conflict.id = internal::transparency_gossip_conflict_identity(conflict);
            report.conflicts.push_back(std::move(conflict));
        }
    }
    for (auto& outgoing : edges) {
        std::sort(outgoing.begin(), outgoing.end());
        outgoing.erase(std::unique(outgoing.begin(), outgoing.end()), outgoing.end());
    }

    std::size_t graph_steps = 0;
    for (std::size_t start = 0; start < groups.size(); ++start) {
        std::vector<bool> reachable(groups.size(), false);
        std::queue<std::size_t> pending;
        pending.push(start);
        reachable[start] = true;
        while (!pending.empty()) {
            if (options.cancellation.cancelled()) {
                return Result<TransparencyGossipAuditReport>::failure(
                    StatusCode::Cancelled, "transparency gossip audit was cancelled");
            }
            const auto current = pending.front();
            pending.pop();
            for (const auto next : edges[current]) {
                if (++graph_steps > options.maximum_graph_steps) {
                    return Result<TransparencyGossipAuditReport>::failure(
                        StatusCode::ResourceLimit,
                        "transparency gossip graph traversal exceeds configured limit");
                }
                if (!reachable[next]) {
                    reachable[next] = true;
                    pending.push(next);
                }
            }
        }
        for (std::size_t next = start + 1; next < groups.size(); ++next) {
            if (groups[start].checkpoint->tree_size >= groups[next].checkpoint->tree_size) {
                continue;
            }
            if (++pair_checks > options.maximum_pair_checks) {
                return Result<TransparencyGossipAuditReport>::failure(
                    StatusCode::ResourceLimit,
                    "transparency gossip checkpoint-pair checks exceed configured limit");
            }
            if (reachable[next])
                ++report.linked_checkpoint_pairs;
            else
                ++report.unlinked_checkpoint_pairs;
        }
    }

    std::sort(report.conflicts.begin(), report.conflicts.end(),
              [](const auto& first, const auto& second) { return first.id < second.id; });
    report.conflicts.erase(
        std::unique(report.conflicts.begin(), report.conflicts.end(),
                    [](const auto& first, const auto& second) { return first.id == second.id; }),
        report.conflicts.end());
    report.status = !report.conflicts.empty()
                        ? TransparencyGossipStatus::SplitView
                        : (report.unlinked_checkpoint_pairs > 0 ? TransparencyGossipStatus::Incomplete
                                                                : TransparencyGossipStatus::Consistent);
    report.id = internal::transparency_gossip_audit_report_identity(report);
    if (!report.valid()) {
        return Result<TransparencyGossipAuditReport>::failure(
            StatusCode::InternalError, "constructed transparency gossip audit report is invalid");
    }
    return report;
}

std::string transparency_gossip_conflict_type_name(TransparencyGossipConflictType type) {
    switch (type) {
    case TransparencyGossipConflictType::SameSizeEquivocation:
        return "same_size_equivocation";
    case TransparencyGossipConflictType::InvalidConsistencyProof:
        return "invalid_consistency_proof";
    }
    return "unknown";
}

std::string transparency_gossip_status_name(TransparencyGossipStatus status) {
    switch (status) {
    case TransparencyGossipStatus::Consistent:
        return "consistent";
    case TransparencyGossipStatus::Incomplete:
        return "incomplete";
    case TransparencyGossipStatus::SplitView:
        return "split_view";
    }
    return "unknown";
}

} // namespace rbfsafe
