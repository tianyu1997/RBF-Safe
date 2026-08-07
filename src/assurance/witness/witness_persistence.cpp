#include <rbfsafe/modules/core.h>
#include <rbfsafe/modules/assurance.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"
#include "internal/witness.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumRecognizableSchema = 1000;
constexpr std::size_t kMaximumStringBytes = 4096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json log_identity_json(const TransparencyLogIdentity& identity) {
    return internal::Json::Object{
        {"id", identity.id},
        {"log_namespace", identity.log_namespace},
        {"signer_key_id", identity.signer_key_id},
        {"signer_public_key", internal::encode_hex(identity.signer_public_key)},
        {"signer_service_id", identity.signer_service_id},
        {"storage_schema", static_cast<double>(identity.storage_schema)},
    };
}

internal::Json checkpoint_json(const TransparencyLogCheckpoint& checkpoint) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(checkpoint.algorithm)},
        {"authentication_tag", checkpoint.authentication_tag},
        {"id", checkpoint.id},
        {"log_id", checkpoint.log_id},
        {"previous_checkpoint_id", checkpoint.previous_checkpoint_id},
        {"root_hash", checkpoint.root_hash},
        {"signer_key_id", checkpoint.signer_key_id},
        {"signer_service_id", checkpoint.signer_service_id},
        {"storage_schema", static_cast<double>(checkpoint.storage_schema)},
        {"tree_size", std::to_string(checkpoint.tree_size)},
    };
}

internal::Json merkle_subtree_json(const TransparencyMerkleSubtree& subtree) {
    return internal::Json::Object{
        {"hash", subtree.hash},
        {"level", static_cast<double>(subtree.level)},
    };
}

internal::Json compact_proof_json(const TransparencyCompactConsistencyProof& proof) {
    internal::Json::Array old_frontier;
    old_frontier.reserve(proof.old_frontier.size());
    for (const auto& subtree : proof.old_frontier)
        old_frontier.emplace_back(merkle_subtree_json(subtree));
    internal::Json::Array appended_subtrees;
    appended_subtrees.reserve(proof.appended_subtrees.size());
    for (const auto& subtree : proof.appended_subtrees)
        appended_subtrees.emplace_back(merkle_subtree_json(subtree));
    return internal::Json::Object{
        {"appended_subtrees", std::move(appended_subtrees)},
        {"id", proof.id},
        {"log_id", proof.log_id},
        {"new_checkpoint_id", proof.new_checkpoint_id},
        {"new_root_hash", proof.new_root_hash},
        {"new_tree_size", std::to_string(proof.new_tree_size)},
        {"old_checkpoint_id", proof.old_checkpoint_id},
        {"old_frontier", std::move(old_frontier)},
        {"old_root_hash", proof.old_root_hash},
        {"old_tree_size", std::to_string(proof.old_tree_size)},
        {"storage_schema", static_cast<double>(proof.storage_schema)},
    };
}

internal::Json witness_policy_json(const TransparencyCheckpointWitnessPolicy& policy) {
    return internal::Json::Object{
        {"exclude_log_signer", policy.exclude_log_signer},
        {"minimum_witnesses", std::to_string(policy.minimum_witnesses)},
        {"require_distinct_services", policy.require_distinct_services},
    };
}

internal::Json cosignature_json(const TransparencyCheckpointCosignature& cosignature) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(cosignature.algorithm)},
        {"authentication_tag", cosignature.authentication_tag},
        {"checkpoint_id", cosignature.checkpoint_id},
        {"id", cosignature.id},
        {"log_id", cosignature.log_id},
        {"root_hash", cosignature.root_hash},
        {"storage_schema", static_cast<double>(cosignature.storage_schema)},
        {"tree_size", std::to_string(cosignature.tree_size)},
        {"trust_bundle_id", cosignature.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(cosignature.trust_bundle_sequence)},
        {"witness_key_id", cosignature.witness_key_id},
        {"witness_service_id", cosignature.witness_service_id},
    };
}

internal::Json witnessed_checkpoint_json(const WitnessedTransparencyCheckpoint& witnessed) {
    internal::Json::Array cosignatures;
    cosignatures.reserve(witnessed.cosignatures.size());
    for (const auto& cosignature : witnessed.cosignatures)
        cosignatures.emplace_back(cosignature_json(cosignature));
    return internal::Json::Object{
        {"checkpoint", checkpoint_json(witnessed.checkpoint)},
        {"cosignatures", std::move(cosignatures)},
        {"id", witnessed.id},
        {"log_id", witnessed.log_id},
        {"policy", witness_policy_json(witnessed.policy)},
        {"storage_schema", static_cast<double>(witnessed.storage_schema)},
        {"trust_bundle_id", witnessed.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(witnessed.trust_bundle_sequence)},
    };
}

internal::Json gossip_json(const TransparencyCheckpointGossip& gossip) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(gossip.algorithm)},
        {"authentication_tag", gossip.authentication_tag},
        {"consistency_proof",
         gossip.consistency_proof ? compact_proof_json(*gossip.consistency_proof) : internal::Json(nullptr)},
        {"id", gossip.id},
        {"log_id", gossip.log_id},
        {"parent_gossip_id", gossip.parent_gossip_id},
        {"recipient_service_id", gossip.recipient_service_id},
        {"sender_key_id", gossip.sender_key_id},
        {"sender_sequence", std::to_string(gossip.sender_sequence)},
        {"sender_service_id", gossip.sender_service_id},
        {"storage_schema", static_cast<double>(gossip.storage_schema)},
        {"trust_bundle_id", gossip.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(gossip.trust_bundle_sequence)},
        {"witnessed_checkpoint", witnessed_checkpoint_json(gossip.witnessed_checkpoint)},
    };
}

internal::Json record_json(const TransparencyGossipRecord& record) {
    return internal::Json::Object{
        {"gossip", gossip_json(record.gossip)},
        {"id", record.id},
        {"log_id", record.log_id},
        {"parent_id", record.parent_id},
        {"sequence", std::to_string(record.sequence)},
        {"storage_schema", static_cast<double>(record.storage_schema)},
        {"trust_bundle_id", record.trust_bundle_id},
    };
}

internal::Json manifest_payload(const TransparencyLogIdentity& log_identity,
                                const std::string& trust_bundle_id, std::uint64_t trust_bundle_sequence,
                                const std::string& library_version) {
    return internal::Json::Object{
        {"format", "rbfsafe-transparency-gossip-archive"},
        {"library_version", library_version},
        {"log_identity", log_identity_json(log_identity)},
        {"schema", static_cast<double>(kSchema)},
        {"trust_bundle_id", trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(trust_bundle_sequence)},
    };
}

internal::Json manifest_document(const TransparencyLogIdentity& log_identity,
                                 const ServiceTrustBundle& trust_bundle) {
    auto payload = manifest_payload(log_identity, trust_bundle.id(), trust_bundle.sequence(), kVersion);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    return internal::Json(std::move(object));
}

internal::Json record_document(const TransparencyGossipRecord& record) {
    return internal::Json::Object{
        {"format", "rbfsafe-transparency-gossip-record"},
        {"record", record_json(record)},
        {"schema", static_cast<double>(kSchema)},
    };
}

std::string record_filename(const TransparencyGossipRecord& record) {
    std::ostringstream name;
    name << std::setw(20) << std::setfill('0') << record.sequence << '-' << record.id << ".json";
    return name.str();
}

Result<void> inspect_regular_file(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "transparency gossip file is missing or indirect", path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to inspect transparency gossip file size",
                                     path.string());
    }
    if (bytes > maximum_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "transparency gossip file exceeds configured byte limit", path.string());
    }
    return Result<void>::success();
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    auto inspected = inspect_regular_file(path, maximum_bytes);
    if (!inspected)
        return inspected.error();
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<internal::Json>::failure(
            StatusCode::IoError, "failed to inspect transparency gossip file size", path.string());
    }
    if (bytes > maximum_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<internal::Json>::failure(StatusCode::ResourceLimit,
                                               "transparency gossip file exceeds configured byte limit",
                                               path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to open transparency gossip file",
                                               path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<internal::Json>::failure(
                StatusCode::CorruptData, "transparency gossip file changed while reading", path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<internal::Json>::failure(
            StatusCode::CorruptData, "transparency gossip file changed while reading", path.string());
    }
    return internal::Json::parse(text);
}

Result<void> inspect_archive_root(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to enumerate transparency gossip archive root",
                                         directory.string());
        }
        const auto name = iterator->path().filename().string();
        if (name != "manifest.json" && name != "records" && name != ".writer-lock") {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "unexpected transparency gossip archive root entry", name);
        }
        if (name == ".writer-lock") {
            const auto status = std::filesystem::symlink_status(iterator->path(), error);
            if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
                return Result<void>::failure(StatusCode::CorruptData,
                                             "transparency gossip writer lock is indirect or invalid", name);
            }
        }
    }
    if (error) {
        return Result<void>::failure(
            StatusCode::IoError, "failed to enumerate transparency gossip archive root", directory.string());
    }
    return Result<void>::success();
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "transparency gossip value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "transparency gossip string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(
            StatusCode::CorruptData, "transparency gossip decimal field is invalid", std::string(key));
    }
    return value;
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<std::size_t>::failure(StatusCode::CorruptData,
                                            "transparency gossip value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || value->as_number() < 0 ||
        value->as_number() > static_cast<double>(maximum) ||
        value->as_number() != static_cast<double>(static_cast<std::size_t>(value->as_number()))) {
        return Result<std::size_t>::failure(StatusCode::CorruptData,
                                            "transparency gossip enum field is invalid", std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData, "transparency gossip value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "transparency gossip bool field is invalid",
                                     std::string(key));
    }
    return value->as_bool();
}

Result<TransparencyLogIdentity> decode_log_identity(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log_namespace = string_field(object, "log_namespace");
    auto service = string_field(object, "signer_service_id");
    auto key = string_field(object, "signer_key_id");
    auto public_key = string_field(object, "signer_public_key");
    if (!storage || !id || !log_namespace || !service || !key || !public_key) {
        return Result<TransparencyLogIdentity>::failure(StatusCode::CorruptData,
                                                        "transparency gossip log identity is incomplete");
    }
    auto decoded_key = internal::decode_hex(public_key.value(), kEd25519PublicKeyBytes);
    if (!decoded_key)
        return decoded_key.error();
    TransparencyLogIdentity result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_namespace = std::move(log_namespace).value();
    result.signer_service_id = std::move(service).value();
    result.signer_key_id = std::move(key).value();
    std::copy(decoded_key.value().begin(), decoded_key.value().end(), result.signer_public_key.begin());
    if (!result.valid()) {
        return Result<TransparencyLogIdentity>::failure(
            StatusCode::CorruptData, "transparency gossip log identity is invalid", result.id);
    }
    return result;
}

Result<TransparencyLogCheckpoint> decode_checkpoint(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log = string_field(object, "log_id");
    auto tree_size = decimal_field(object, "tree_size");
    auto root = string_field(object, "root_hash");
    auto previous = string_field(object, "previous_checkpoint_id", true);
    auto service = string_field(object, "signer_service_id");
    auto key = string_field(object, "signer_key_id");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !log || !tree_size || !root || !previous || !service || !key || !algorithm ||
        !tag) {
        return Result<TransparencyLogCheckpoint>::failure(StatusCode::CorruptData,
                                                          "transparency gossip checkpoint is incomplete");
    }
    TransparencyLogCheckpoint result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log).value();
    result.tree_size = tree_size.value();
    result.root_hash = std::move(root).value();
    result.previous_checkpoint_id = std::move(previous).value();
    result.signer_service_id = std::move(service).value();
    result.signer_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<TransparencyLogCheckpoint>::failure(
            StatusCode::CorruptData, "transparency gossip checkpoint is invalid", result.id);
    }
    return result;
}

Result<TransparencyMerkleSubtree> decode_subtree(const internal::Json& object) {
    auto level = enum_field(object, "level", 63);
    auto hash = string_field(object, "hash");
    if (!level || !hash) {
        return Result<TransparencyMerkleSubtree>::failure(StatusCode::CorruptData,
                                                          "transparency gossip Merkle subtree is incomplete");
    }
    TransparencyMerkleSubtree result;
    result.level = static_cast<std::uint8_t>(level.value());
    result.hash = std::move(hash).value();
    if (!result.valid()) {
        return Result<TransparencyMerkleSubtree>::failure(StatusCode::CorruptData,
                                                          "transparency gossip Merkle subtree is invalid");
    }
    return result;
}

Result<TransparencyCompactConsistencyProof>
decode_compact_proof(const internal::Json& object, const TransparencyGossipArchiveLoadOptions& options,
                     std::size_t& total_proof_subtrees) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log = string_field(object, "log_id");
    auto old_checkpoint = string_field(object, "old_checkpoint_id");
    auto new_checkpoint = string_field(object, "new_checkpoint_id");
    auto old_size = decimal_field(object, "old_tree_size");
    auto new_size = decimal_field(object, "new_tree_size");
    auto old_root = string_field(object, "old_root_hash");
    auto new_root = string_field(object, "new_root_hash");
    const auto* old_frontier = object.is_object() ? object.find("old_frontier") : nullptr;
    const auto* appended = object.is_object() ? object.find("appended_subtrees") : nullptr;
    if (!storage || !id || !log || !old_checkpoint || !new_checkpoint || !old_size || !new_size ||
        !old_root || !new_root || old_frontier == nullptr || appended == nullptr ||
        !old_frontier->is_array() || !appended->is_array()) {
        return Result<TransparencyCompactConsistencyProof>::failure(
            StatusCode::CorruptData, "transparency compact consistency proof is incomplete");
    }
    const std::size_t proof_subtrees = old_frontier->as_array().size() + appended->as_array().size();
    if (proof_subtrees > options.maximum_proof_subtrees ||
        total_proof_subtrees > options.maximum_total_proof_subtrees ||
        proof_subtrees > options.maximum_total_proof_subtrees - total_proof_subtrees) {
        return Result<TransparencyCompactConsistencyProof>::failure(
            StatusCode::ResourceLimit, "transparency compact proof subtree count exceeds configured limits");
    }
    TransparencyCompactConsistencyProof result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log).value();
    result.old_checkpoint_id = std::move(old_checkpoint).value();
    result.new_checkpoint_id = std::move(new_checkpoint).value();
    result.old_tree_size = old_size.value();
    result.new_tree_size = new_size.value();
    result.old_root_hash = std::move(old_root).value();
    result.new_root_hash = std::move(new_root).value();
    result.old_frontier.reserve(old_frontier->as_array().size());
    for (const auto& value : old_frontier->as_array()) {
        if (options.cancellation.cancelled()) {
            return Result<TransparencyCompactConsistencyProof>::failure(
                StatusCode::Cancelled, "transparency gossip load was cancelled");
        }
        auto subtree = decode_subtree(value);
        if (!subtree)
            return subtree.error();
        result.old_frontier.push_back(std::move(subtree).value());
    }
    result.appended_subtrees.reserve(appended->as_array().size());
    for (const auto& value : appended->as_array()) {
        if (options.cancellation.cancelled()) {
            return Result<TransparencyCompactConsistencyProof>::failure(
                StatusCode::Cancelled, "transparency gossip load was cancelled");
        }
        auto subtree = decode_subtree(value);
        if (!subtree)
            return subtree.error();
        result.appended_subtrees.push_back(std::move(subtree).value());
    }
    total_proof_subtrees += proof_subtrees;
    if (!result.valid()) {
        return Result<TransparencyCompactConsistencyProof>::failure(
            StatusCode::CorruptData, "transparency compact consistency proof is invalid", result.id);
    }
    return result;
}

Result<TransparencyCheckpointCosignature> decode_cosignature(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log = string_field(object, "log_id");
    auto checkpoint = string_field(object, "checkpoint_id");
    auto tree_size = decimal_field(object, "tree_size");
    auto root = string_field(object, "root_hash");
    auto bundle = string_field(object, "trust_bundle_id");
    auto bundle_sequence = decimal_field(object, "trust_bundle_sequence");
    auto service = string_field(object, "witness_service_id");
    auto key = string_field(object, "witness_key_id");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !log || !checkpoint || !tree_size || !root || !bundle || !bundle_sequence ||
        !service || !key || !algorithm || !tag) {
        return Result<TransparencyCheckpointCosignature>::failure(
            StatusCode::CorruptData, "transparency checkpoint cosignature is incomplete");
    }
    TransparencyCheckpointCosignature result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log).value();
    result.checkpoint_id = std::move(checkpoint).value();
    result.tree_size = tree_size.value();
    result.root_hash = std::move(root).value();
    result.trust_bundle_id = std::move(bundle).value();
    result.trust_bundle_sequence = bundle_sequence.value();
    result.witness_service_id = std::move(service).value();
    result.witness_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<TransparencyCheckpointCosignature>::failure(
            StatusCode::CorruptData, "transparency checkpoint cosignature is invalid", result.id);
    }
    return result;
}

Result<TransparencyCheckpointWitnessPolicy> decode_witness_policy(const internal::Json& object) {
    auto minimum = decimal_field(object, "minimum_witnesses");
    auto distinct = bool_field(object, "require_distinct_services");
    auto exclude = bool_field(object, "exclude_log_signer");
    if (!minimum || !distinct || !exclude || minimum.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<TransparencyCheckpointWitnessPolicy>::failure(
            StatusCode::CorruptData, "transparency checkpoint witness policy is incomplete");
    }
    TransparencyCheckpointWitnessPolicy result;
    result.minimum_witnesses = static_cast<std::uint32_t>(minimum.value());
    result.require_distinct_services = distinct.value();
    result.exclude_log_signer = exclude.value();
    if (!valid_transparency_checkpoint_witness_policy(result)) {
        return Result<TransparencyCheckpointWitnessPolicy>::failure(
            StatusCode::CorruptData, "transparency checkpoint witness policy is invalid");
    }
    return result;
}

Result<WitnessedTransparencyCheckpoint>
decode_witnessed_checkpoint(const internal::Json& object, const TransparencyGossipArchiveLoadOptions& options,
                            std::size_t& total_witnesses) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log = string_field(object, "log_id");
    auto bundle = string_field(object, "trust_bundle_id");
    auto bundle_sequence = decimal_field(object, "trust_bundle_sequence");
    const auto* policy_value = object.is_object() ? object.find("policy") : nullptr;
    const auto* checkpoint_value = object.is_object() ? object.find("checkpoint") : nullptr;
    const auto* cosignatures = object.is_object() ? object.find("cosignatures") : nullptr;
    if (!storage || !id || !log || !bundle || !bundle_sequence || policy_value == nullptr ||
        checkpoint_value == nullptr || cosignatures == nullptr || !cosignatures->is_array()) {
        return Result<WitnessedTransparencyCheckpoint>::failure(
            StatusCode::CorruptData, "witnessed transparency checkpoint is incomplete");
    }
    const auto witness_count = cosignatures->as_array().size();
    if (witness_count > options.maximum_witnesses_per_checkpoint ||
        total_witnesses > options.maximum_total_witnesses ||
        witness_count > options.maximum_total_witnesses - total_witnesses) {
        return Result<WitnessedTransparencyCheckpoint>::failure(
            StatusCode::ResourceLimit, "transparency checkpoint witness count exceeds configured limits");
    }
    auto policy = decode_witness_policy(*policy_value);
    auto checkpoint = decode_checkpoint(*checkpoint_value);
    if (!policy || !checkpoint)
        return !policy ? policy.error() : checkpoint.error();
    WitnessedTransparencyCheckpoint result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log).value();
    result.trust_bundle_id = std::move(bundle).value();
    result.trust_bundle_sequence = bundle_sequence.value();
    result.policy = policy.value();
    result.checkpoint = std::move(checkpoint).value();
    result.cosignatures.reserve(witness_count);
    for (const auto& value : cosignatures->as_array()) {
        if (options.cancellation.cancelled()) {
            return Result<WitnessedTransparencyCheckpoint>::failure(StatusCode::Cancelled,
                                                                    "transparency gossip load was cancelled");
        }
        auto cosignature = decode_cosignature(value);
        if (!cosignature)
            return cosignature.error();
        result.cosignatures.push_back(std::move(cosignature).value());
    }
    total_witnesses += witness_count;
    if (!result.valid()) {
        return Result<WitnessedTransparencyCheckpoint>::failure(
            StatusCode::CorruptData, "witnessed transparency checkpoint is invalid", result.id);
    }
    return result;
}

Result<TransparencyCheckpointGossip> decode_gossip(const internal::Json& object,
                                                   const TransparencyGossipArchiveLoadOptions& options,
                                                   std::size_t& total_witnesses,
                                                   std::size_t& total_proof_subtrees) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log = string_field(object, "log_id");
    auto sender_sequence = decimal_field(object, "sender_sequence");
    auto parent = string_field(object, "parent_gossip_id", true);
    auto recipient = string_field(object, "recipient_service_id");
    auto service = string_field(object, "sender_service_id");
    auto key = string_field(object, "sender_key_id");
    auto bundle = string_field(object, "trust_bundle_id");
    auto bundle_sequence = decimal_field(object, "trust_bundle_sequence");
    const auto* witnessed = object.is_object() ? object.find("witnessed_checkpoint") : nullptr;
    const auto* proof = object.is_object() ? object.find("consistency_proof") : nullptr;
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !log || !sender_sequence || !parent || !recipient || !service || !key || !bundle ||
        !bundle_sequence || witnessed == nullptr || proof == nullptr || !algorithm || !tag) {
        return Result<TransparencyCheckpointGossip>::failure(StatusCode::CorruptData,
                                                             "transparency checkpoint gossip is incomplete");
    }
    auto decoded_witnessed = decode_witnessed_checkpoint(*witnessed, options, total_witnesses);
    if (!decoded_witnessed)
        return decoded_witnessed.error();
    TransparencyCheckpointGossip result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log).value();
    result.sender_sequence = sender_sequence.value();
    result.parent_gossip_id = std::move(parent).value();
    result.recipient_service_id = std::move(recipient).value();
    result.sender_service_id = std::move(service).value();
    result.sender_key_id = std::move(key).value();
    result.trust_bundle_id = std::move(bundle).value();
    result.trust_bundle_sequence = bundle_sequence.value();
    result.witnessed_checkpoint = std::move(decoded_witnessed).value();
    if (!proof->is_null()) {
        auto decoded_proof = decode_compact_proof(*proof, options, total_proof_subtrees);
        if (!decoded_proof)
            return decoded_proof.error();
        result.consistency_proof = std::move(decoded_proof).value();
    }
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<TransparencyCheckpointGossip>::failure(
            StatusCode::CorruptData, "transparency checkpoint gossip is invalid", result.id);
    }
    return result;
}

Result<TransparencyGossipRecord> decode_record(const internal::Json& document,
                                               const TransparencyGossipArchiveLoadOptions& options,
                                               std::size_t& total_witnesses,
                                               std::size_t& total_proof_subtrees) {
    auto format = string_field(document, "format");
    auto schema = enum_field(document, "schema", kMaximumRecognizableSchema);
    const auto* record_value = document.is_object() ? document.find("record") : nullptr;
    if (!format || !schema || record_value == nullptr) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::CorruptData,
                                                         "transparency gossip record document is incomplete");
    }
    if (format.value() != "rbfsafe-transparency-gossip-record" || schema.value() != kSchema) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::IncompatibleFormat,
                                                         "unsupported transparency gossip record schema");
    }
    auto storage = enum_field(*record_value, "storage_schema", 1);
    auto sequence = decimal_field(*record_value, "sequence");
    auto id = string_field(*record_value, "id");
    auto parent = string_field(*record_value, "parent_id", true);
    auto log = string_field(*record_value, "log_id");
    auto bundle = string_field(*record_value, "trust_bundle_id");
    const auto* gossip_value = record_value->is_object() ? record_value->find("gossip") : nullptr;
    if (!storage || !sequence || !id || !parent || !log || !bundle || gossip_value == nullptr) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::CorruptData,
                                                         "transparency gossip record is incomplete");
    }
    auto gossip = decode_gossip(*gossip_value, options, total_witnesses, total_proof_subtrees);
    if (!gossip)
        return gossip.error();
    TransparencyGossipRecord result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_id = std::move(parent).value();
    result.log_id = std::move(log).value();
    result.trust_bundle_id = std::move(bundle).value();
    result.gossip = std::move(gossip).value();
    if (!result.valid()) {
        return Result<TransparencyGossipRecord>::failure(
            StatusCode::CorruptData, "transparency gossip record identity is invalid", result.id);
    }
    return result;
}

bool valid_load_options(const TransparencyGossipArchiveLoadOptions& options) {
    return options.maximum_records > 0 && options.maximum_witnesses_per_checkpoint > 0 &&
           options.maximum_total_witnesses > 0 && options.maximum_proof_subtrees > 0 &&
           options.maximum_total_proof_subtrees > 0 && options.maximum_unique_checkpoints > 0 &&
           options.maximum_pair_checks > 0 && options.maximum_graph_steps > 0 &&
           options.maximum_manifest_bytes > 0 && options.maximum_record_bytes > 0;
}

using SenderKey = std::tuple<std::string, std::string, std::string>;

Result<void> validate_sender_chain(const std::vector<TransparencyGossipRecord>& records) {
    std::map<SenderKey, std::pair<std::uint64_t, std::string>> heads;
    std::set<std::string> gossip_ids;
    for (const auto& record : records) {
        if (!gossip_ids.insert(record.gossip.id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "transparency gossip archive repeats a gossip message",
                                         record.gossip.id);
        }
        const SenderKey key{record.gossip.sender_service_id, record.gossip.sender_key_id,
                            record.gossip.recipient_service_id};
        const auto previous = heads.find(key);
        if (previous == heads.end()) {
            if (record.gossip.sender_sequence != 1 || !record.gossip.parent_gossip_id.empty()) {
                return Result<void>::failure(
                    StatusCode::IdentityMismatch,
                    "transparency gossip sender chain does not start at sequence one", record.gossip.id);
            }
        } else if (record.gossip.sender_sequence != previous->second.first + 1U ||
                   record.gossip.parent_gossip_id != previous->second.second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "transparency gossip sender chain is discontinuous",
                                         record.gossip.id);
        }
        heads[key] = {record.gossip.sender_sequence, record.gossip.id};
    }
    return Result<void>::success();
}

Result<void> write_immutable_file(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable transparency gossip record",
                                         destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable transparency gossip record already exists",
                                     destination.string());
    }
    const auto temporary = unique_sibling(destination, ".tmp-");
    auto written = internal::write_text_file(temporary, content);
    if (!written)
        return written;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to publish immutable transparency gossip record",
                                     destination.string());
    }
    return Result<void>::success();
}

} // namespace

namespace internal {

TransparencyGossipWriteLock::TransparencyGossipWriteLock(TransparencyGossipWriteLock&& other) noexcept
    : path_(std::move(other.path_)), held_(other.held_) {
    other.held_ = false;
}

TransparencyGossipWriteLock::~TransparencyGossipWriteLock() {
    if (!held_)
        return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

Result<TransparencyGossipWriteLock>
TransparencyGossipWriteLock::acquire(const std::filesystem::path& directory) {
    std::error_code directory_error;
    const auto directory_status = std::filesystem::symlink_status(directory, directory_error);
    if (directory_error || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status)) {
        return Result<TransparencyGossipWriteLock>::failure(
            StatusCode::CorruptData, "transparency gossip archive directory is missing or indirect",
            directory.string());
    }
    const auto path = directory / ".writer-lock";
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    if (error) {
        return Result<TransparencyGossipWriteLock>::failure(
            StatusCode::IoError, "failed to acquire transparency gossip writer lock", path.string());
    }
    if (!created) {
        return Result<TransparencyGossipWriteLock>::failure(
            StatusCode::ResourceLimit, "transparency gossip writer lock is already held", path.string());
    }
    return TransparencyGossipWriteLock(path);
}

TransparencyGossipWriteLock::TransparencyGossipWriteLock(std::filesystem::path path)
    : path_(std::move(path)) {}

Result<void> append_transparency_gossip_record_file(const std::filesystem::path& directory,
                                                    const TransparencyGossipRecord& record,
                                                    std::uintmax_t maximum_record_bytes) {
    if (directory.empty() || !record.valid() || maximum_record_bytes == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency gossip append-file input is invalid");
    }
    const auto content = record_document(record).dump(true) + "\n";
    if (content.size() > maximum_record_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "transparency gossip record exceeds configured publication byte limit",
                                     record.id);
    }
    return write_immutable_file(directory / "records" / record_filename(record), content);
}

} // namespace internal

bool TransparencyGossipArchive::valid() const {
    if (directory_.empty() || !log_identity_.valid() || !trust_bundle_.valid() ||
        trust_bundle_id_ != trust_bundle_.id() || trust_bundle_sequence_ != trust_bundle_.sequence() ||
        records_.size() > options_.maximum_records || !valid_load_options(options_)) {
        return false;
    }
    if (records_.empty())
        return current_record_id_.empty();
    std::size_t total_witnesses = 0;
    std::size_t total_proof_subtrees = 0;
    std::string parent;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (!record.valid() || record.sequence != index || record.parent_id != parent ||
            record.log_id != log_identity_.id || record.trust_bundle_id != trust_bundle_id_ ||
            !verify_transparency_checkpoint_gossip(log_identity_, record.gossip, trust_bundle_)) {
            return false;
        }
        const auto witness_count = record.gossip.witnessed_checkpoint.cosignatures.size();
        if (witness_count > options_.maximum_witnesses_per_checkpoint ||
            total_witnesses > options_.maximum_total_witnesses ||
            witness_count > options_.maximum_total_witnesses - total_witnesses) {
            return false;
        }
        total_witnesses += witness_count;
        if (record.gossip.consistency_proof) {
            const auto proof_count = record.gossip.consistency_proof->old_frontier.size() +
                                     record.gossip.consistency_proof->appended_subtrees.size();
            if (proof_count > options_.maximum_proof_subtrees ||
                total_proof_subtrees > options_.maximum_total_proof_subtrees ||
                proof_count > options_.maximum_total_proof_subtrees - total_proof_subtrees) {
                return false;
            }
            total_proof_subtrees += proof_count;
        }
        parent = record.id;
    }
    return current_record_id_ == records_.back().id && validate_sender_chain(records_);
}

Result<TransparencyGossipArchive> TransparencyGossipArchive::create(const std::filesystem::path& directory,
                                                                    TransparencyLogIdentity log_identity,
                                                                    const ServiceTrustBundle& trust_bundle) {
    if (directory.empty() || directory == directory.root_path() || !log_identity.valid() ||
        !trust_bundle.valid()) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::InvalidArgument, "transparency gossip archive creation input is invalid");
    }
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IoError, "transparency gossip archive destination already exists",
            directory.string());
    }
    if (error) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IoError, "failed to inspect transparency gossip archive destination",
            directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::IoError, "failed to create transparency gossip archive parent",
                directory.parent_path().string());
        }
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    const bool records_created = std::filesystem::create_directories(temporary / "records", error);
    if (error || !records_created) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IoError, "failed to create temporary transparency gossip archive directory");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    auto manifest_written = internal::write_text_file(
        temporary / "manifest.json", manifest_document(log_identity, trust_bundle).dump(true) + "\n");
    if (!manifest_written) {
        cleanup();
        return manifest_written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IoError, "failed to publish transparency gossip archive", directory.string());
    }
    TransparencyGossipArchive result;
    result.directory_ = directory;
    result.log_identity_ = std::move(log_identity);
    result.trust_bundle_ = trust_bundle;
    result.trust_bundle_id_ = trust_bundle.id();
    result.trust_bundle_sequence_ = trust_bundle.sequence();
    result.options_ = TransparencyGossipArchiveLoadOptions{};
    if (!result.valid()) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::InternalError, "constructed transparency gossip archive is invalid");
    }
    return result;
}

Result<TransparencyGossipArchive> TransparencyGossipArchive::open(
    const std::filesystem::path& directory, const TransparencyLogIdentity& expected_log_identity,
    const ServiceTrustBundle& trust_bundle, const std::string& expected_trust_bundle_id,
    const std::string& expected_head_record_id, const TransparencyGossipArchiveLoadOptions& options) {
    if (directory.empty() || !expected_log_identity.valid() || !trust_bundle.valid() ||
        !internal::valid_sha256(expected_trust_bundle_id) || trust_bundle.id() != expected_trust_bundle_id ||
        (!expected_head_record_id.empty() && !internal::valid_sha256(expected_head_record_id)) ||
        !valid_load_options(options)) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::InvalidArgument, "transparency gossip archive open input is invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<TransparencyGossipArchive>::failure(StatusCode::Cancelled,
                                                          "transparency gossip archive open was cancelled");
    }
    std::error_code root_error;
    const auto root_status = std::filesystem::symlink_status(directory, root_error);
    if (root_error || std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::CorruptData, "transparency gossip archive directory is missing or indirect");
    }
    auto root_inspected = inspect_archive_root(directory);
    if (!root_inspected)
        return root_inspected.error();
    auto manifest = read_bounded_json(directory / "manifest.json", options.maximum_manifest_bytes);
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = enum_field(manifest.value(), "schema", kMaximumRecognizableSchema);
    auto library_version = string_field(manifest.value(), "library_version");
    auto checksum = string_field(manifest.value(), "identity");
    auto bundle_id = string_field(manifest.value(), "trust_bundle_id");
    auto bundle_sequence = decimal_field(manifest.value(), "trust_bundle_sequence");
    const auto* identity_value =
        manifest.value().is_object() ? manifest.value().find("log_identity") : nullptr;
    if (!format || !schema || !library_version || !checksum || !bundle_id || !bundle_sequence ||
        identity_value == nullptr) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::CorruptData, "transparency gossip archive manifest is incomplete");
    }
    if (format.value() != "rbfsafe-transparency-gossip-archive" || schema.value() != kSchema) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IncompatibleFormat, "unsupported transparency gossip archive manifest schema");
    }
    auto decoded_identity = decode_log_identity(*identity_value);
    if (!decoded_identity)
        return decoded_identity.error();
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() !=
            internal::sha256(manifest_payload(decoded_identity.value(), bundle_id.value(),
                                              bundle_sequence.value(), library_version.value())
                                 .dump(false)) ||
        decoded_identity.value().id != expected_log_identity.id ||
        decoded_identity.value().log_namespace != expected_log_identity.log_namespace ||
        decoded_identity.value().signer_service_id != expected_log_identity.signer_service_id ||
        decoded_identity.value().signer_key_id != expected_log_identity.signer_key_id ||
        decoded_identity.value().signer_public_key != expected_log_identity.signer_public_key ||
        bundle_id.value() != expected_trust_bundle_id || bundle_sequence.value() != trust_bundle.sequence()) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IdentityMismatch, "transparency gossip archive manifest does not match caller pins");
    }

    const auto records_directory = directory / "records";
    std::error_code error;
    const auto records_status = std::filesystem::symlink_status(records_directory, error);
    if (error || std::filesystem::is_symlink(records_status) ||
        !std::filesystem::is_directory(records_status)) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::CorruptData, "transparency gossip records directory is missing or indirect");
    }
    std::vector<std::pair<std::string, TransparencyGossipRecord>> decoded;
    std::size_t total_witnesses = 0;
    std::size_t total_proof_subtrees = 0;
    for (std::filesystem::directory_iterator iterator(records_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::IoError, "failed to enumerate transparency gossip records");
        }
        if (options.cancellation.cancelled()) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::Cancelled, "transparency gossip archive open was cancelled");
        }
        const auto filename = iterator->path().filename().string();
        const auto entry_status = std::filesystem::symlink_status(iterator->path(), error);
        if (error) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::IoError, "failed to inspect transparency gossip record entry");
        }
        if (!std::filesystem::is_symlink(entry_status) && std::filesystem::is_regular_file(entry_status) &&
            iterator->path().extension() == ".json") {
            if (decoded.size() >= options.maximum_records) {
                return Result<TransparencyGossipArchive>::failure(
                    StatusCode::ResourceLimit, "transparency gossip record count exceeds configured limit");
            }
            auto document = read_bounded_json(iterator->path(), options.maximum_record_bytes);
            if (!document)
                return document.error();
            auto record = decode_record(document.value(), options, total_witnesses, total_proof_subtrees);
            if (!record)
                return record.error();
            decoded.emplace_back(filename, std::move(record).value());
        } else if (std::filesystem::is_symlink(entry_status) || filename.find(".tmp-") == std::string::npos) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::CorruptData, "unexpected transparency gossip record entry", filename);
        }
    }
    if (error) {
        return Result<TransparencyGossipArchive>::failure(StatusCode::IoError,
                                                          "failed to enumerate transparency gossip records");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& left, const auto& right) {
        if (left.second.sequence != right.second.sequence)
            return left.second.sequence < right.second.sequence;
        return left.second.id < right.second.id;
    });
    std::vector<TransparencyGossipRecord> records;
    records.reserve(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const auto& filename = decoded[index].first;
        const auto& record = decoded[index].second;
        if (record.sequence != index || filename != record_filename(record) ||
            record.log_id != expected_log_identity.id || record.trust_bundle_id != expected_trust_bundle_id ||
            (index == 0 ? !record.parent_id.empty() : record.parent_id != decoded[index - 1].second.id)) {
            return Result<TransparencyGossipArchive>::failure(
                StatusCode::CorruptData, "transparency gossip record chain is invalid", record.id);
        }
        records.push_back(record);
    }
    auto sender_chain = validate_sender_chain(records);
    if (!sender_chain)
        return sender_chain.error();
    const std::string current_record = records.empty() ? std::string{} : records.back().id;
    if (current_record != expected_head_record_id) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::IdentityMismatch, "transparency gossip archive head does not match caller pin",
            current_record);
    }
    TransparencyGossipArchive result;
    result.directory_ = directory;
    result.log_identity_ = std::move(decoded_identity).value();
    result.trust_bundle_ = trust_bundle;
    result.trust_bundle_id_ = expected_trust_bundle_id;
    result.trust_bundle_sequence_ = trust_bundle.sequence();
    result.records_ = std::move(records);
    result.current_record_id_ = current_record;
    result.options_ = options;
    if (!result.valid()) {
        return Result<TransparencyGossipArchive>::failure(
            StatusCode::CorruptData, "loaded transparency gossip archive is structurally invalid");
    }
    return result;
}

Result<TransparencyGossipRecord>
TransparencyGossipArchive::publish(TransparencyCheckpointGossip gossip,
                                   const std::string& expected_head_record_id) {
    if (options_.cancellation.cancelled()) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::Cancelled,
                                                         "transparency gossip publication was cancelled");
    }
    if (!valid()) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::CorruptData,
                                                         "open transparency gossip archive state is invalid");
    }
    if (expected_head_record_id != current_record_id_) {
        return Result<TransparencyGossipRecord>::failure(
            StatusCode::IdentityMismatch,
            "transparency gossip publication expected head does not match the archive",
            expected_head_record_id);
    }
    auto gossip_verified = verify_transparency_checkpoint_gossip(log_identity_, gossip, trust_bundle_);
    if (!gossip_verified)
        return gossip_verified.error();
    auto lock = internal::TransparencyGossipWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto fresh = TransparencyGossipArchive::open(directory_, log_identity_, trust_bundle_, trust_bundle_id_,
                                                 current_record_id_, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_head_record_id) {
        return Result<TransparencyGossipRecord>::failure(
            StatusCode::IdentityMismatch,
            "transparency gossip archive changed after caller observed its head",
            fresh.value().current_record_id_);
    }
    if (fresh.value().records_.size() >= options_.maximum_records) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::ResourceLimit,
                                                         "transparency gossip record limit reached");
    }
    const auto witness_count = gossip.witnessed_checkpoint.cosignatures.size();
    std::size_t total_witnesses = 0;
    std::size_t total_proof_subtrees = 0;
    for (const auto& record : fresh.value().records_) {
        total_witnesses += record.gossip.witnessed_checkpoint.cosignatures.size();
        if (record.gossip.consistency_proof) {
            total_proof_subtrees += record.gossip.consistency_proof->old_frontier.size() +
                                    record.gossip.consistency_proof->appended_subtrees.size();
        }
    }
    const auto proof_count = gossip.consistency_proof ? gossip.consistency_proof->old_frontier.size() +
                                                            gossip.consistency_proof->appended_subtrees.size()
                                                      : 0U;
    if (witness_count > options_.maximum_witnesses_per_checkpoint ||
        total_witnesses > options_.maximum_total_witnesses ||
        witness_count > options_.maximum_total_witnesses - total_witnesses ||
        proof_count > options_.maximum_proof_subtrees ||
        total_proof_subtrees > options_.maximum_total_proof_subtrees ||
        proof_count > options_.maximum_total_proof_subtrees - total_proof_subtrees) {
        return Result<TransparencyGossipRecord>::failure(
            StatusCode::ResourceLimit, "transparency gossip publication exceeds witness or proof limits");
    }
    TransparencyGossipRecord record;
    record.sequence = static_cast<std::uint64_t>(fresh.value().records_.size());
    record.parent_id = fresh.value().records_.empty() ? std::string{} : fresh.value().records_.back().id;
    record.log_id = log_identity_.id;
    record.trust_bundle_id = trust_bundle_id_;
    record.gossip = std::move(gossip);
    record.id = internal::transparency_gossip_record_identity(record);
    if (!record.valid()) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::InternalError,
                                                         "constructed transparency gossip record is invalid");
    }
    TransparencyGossipArchive candidate = fresh.value();
    candidate.records_.push_back(record);
    candidate.current_record_id_ = record.id;
    auto sender_chain = validate_sender_chain(candidate.records_);
    if (!sender_chain)
        return sender_chain.error();
    if (!candidate.valid()) {
        return Result<TransparencyGossipRecord>::failure(
            StatusCode::InternalError, "candidate transparency gossip archive failed complete validation");
    }
    if (options_.cancellation.cancelled()) {
        return Result<TransparencyGossipRecord>::failure(StatusCode::Cancelled,
                                                         "transparency gossip publication was cancelled");
    }
    auto appended =
        internal::append_transparency_gossip_record_file(directory_, record, options_.maximum_record_bytes);
    if (!appended)
        return appended.error();
    *this = std::move(candidate);
    return record;
}

Result<TransparencyGossipAuditReport> TransparencyGossipArchive::audit() const {
    if (!valid()) {
        return Result<TransparencyGossipAuditReport>::failure(
            StatusCode::CorruptData, "transparency gossip archive failed complete validation");
    }
    std::vector<TransparencyCheckpointGossip> gossip;
    gossip.reserve(records_.size());
    for (const auto& record : records_)
        gossip.push_back(record.gossip);
    TransparencyGossipAuditOptions options;
    options.maximum_gossip_messages = options_.maximum_records;
    options.maximum_unique_checkpoints = options_.maximum_unique_checkpoints;
    options.maximum_pair_checks = options_.maximum_pair_checks;
    options.maximum_graph_steps = options_.maximum_graph_steps;
    options.cancellation = options_.cancellation;
    return audit_transparency_checkpoint_gossip(log_identity_, gossip, trust_bundle_, options);
}

} // namespace rbfsafe
