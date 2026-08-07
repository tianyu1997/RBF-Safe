#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"
#include "internal/transparency.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
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

internal::Json runtime_snapshot_json(const DeploymentRuntimeSnapshot& snapshot) {
    return internal::Json::Object{
        {"authenticated_artifacts", snapshot.authenticated_artifacts},
        {"command_latency_ns", std::to_string(snapshot.command_latency_ns)},
        {"consecutive_missed_cycles", std::to_string(snapshot.consecutive_missed_cycles)},
        {"controller_digest", snapshot.controller_digest},
        {"control_period_ns", std::to_string(snapshot.control_period_ns)},
        {"deployment_id", snapshot.deployment_id},
        {"fail_closed_transport_active", snapshot.fail_closed_transport_active},
        {"observation_age_ns", std::to_string(snapshot.observation_age_ns)},
        {"platform_digest", snapshot.platform_digest},
        {"robot_digest", snapshot.robot_digest},
        {"runtime_digest", snapshot.runtime_digest},
        {"runtime_monitor_active", snapshot.runtime_monitor_active},
    };
}

internal::Json deployment_anchor_json(const DeploymentTransparencyAnchor& anchor) {
    return internal::Json::Object{
        {"approval_set_id", anchor.approval_set_id},
        {"controller_digest", anchor.controller_digest},
        {"deployment_id", anchor.deployment_id},
        {"id", anchor.id},
        {"platform_digest", anchor.platform_digest},
        {"reviewed_profile_id", anchor.reviewed_profile_id},
        {"robot_digest", anchor.robot_digest},
        {"runtime_digest", anchor.runtime_digest},
        {"storage_schema", static_cast<double>(anchor.storage_schema)},
        {"trust_bundle_id", anchor.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(anchor.trust_bundle_sequence)},
        {"trust_checkpoint_id", anchor.trust_checkpoint_id},
        {"trust_head_record_id", anchor.trust_head_record_id},
        {"trust_root_bundle_id", anchor.trust_root_bundle_id},
    };
}

internal::Json observation_json(const IndependentRuntimeObservation& observation) {
    return internal::Json::Object{
        {"authorization_id", observation.authorization_id},
        {"command_digest", observation.command_digest},
        {"command_index", std::to_string(observation.command_index)},
        {"command_sequence_id", observation.command_sequence_id},
        {"configuration_digest", observation.configuration_digest},
        {"id", observation.id},
        {"ledger_id", observation.ledger_id},
        {"ledger_record_id", observation.ledger_record_id},
        {"monitor_state", static_cast<int>(observation.monitor_state)},
        {"observation_sequence", std::to_string(observation.observation_sequence)},
        {"observed_monotonic_ns", std::to_string(observation.observed_monotonic_ns)},
        {"runtime", runtime_snapshot_json(observation.runtime)},
        {"session_id", observation.session_id},
        {"storage_schema", static_cast<double>(observation.storage_schema)},
    };
}

internal::Json observation_policy_json(const RuntimeObservationPolicy& policy) {
    return internal::Json::Object{
        {"exclude_controller_service", policy.exclude_controller_service},
        {"minimum_attestations", std::to_string(policy.minimum_attestations)},
        {"require_distinct_services", policy.require_distinct_services},
    };
}

internal::Json attestation_json(const RuntimeObservationAttestation& attestation) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(attestation.algorithm)},
        {"authentication_tag", attestation.authentication_tag},
        {"id", attestation.id},
        {"observation_id", attestation.observation_id},
        {"source_key_id", attestation.source_key_id},
        {"source_service_id", attestation.source_service_id},
        {"storage_schema", static_cast<double>(attestation.storage_schema)},
    };
}

internal::Json attestation_set_json(const RuntimeObservationAttestationSet& attestation_set) {
    internal::Json::Array attestations;
    attestations.reserve(attestation_set.attestations.size());
    for (const auto& attestation : attestation_set.attestations)
        attestations.emplace_back(attestation_json(attestation));
    return internal::Json::Object{
        {"attestations", std::move(attestations)},
        {"id", attestation_set.id},
        {"observation", observation_json(attestation_set.observation)},
        {"policy", observation_policy_json(attestation_set.policy)},
        {"storage_schema", static_cast<double>(attestation_set.storage_schema)},
    };
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

internal::Json leaf_json(const TransparencyLogLeaf& leaf) {
    return internal::Json::Object{
        {"deployment_anchor",
         leaf.deployment_anchor ? deployment_anchor_json(*leaf.deployment_anchor) : internal::Json(nullptr)},
        {"id", leaf.id},
        {"index", std::to_string(leaf.index)},
        {"kind", static_cast<int>(leaf.kind)},
        {"log_id", leaf.log_id},
        {"runtime_observation", leaf.runtime_observation ? attestation_set_json(*leaf.runtime_observation)
                                                         : internal::Json(nullptr)},
        {"storage_schema", static_cast<double>(leaf.storage_schema)},
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

internal::Json record_payload(const TransparencyLogRecord& record) {
    return internal::Json::Object{
        {"checkpoint", checkpoint_json(record.checkpoint)},
        {"format", "rbfsafe-transparency-log-record"},
        {"id", record.id},
        {"leaf", leaf_json(record.leaf)},
        {"log_id", record.log_id},
        {"parent_id", record.parent_id},
        {"schema", static_cast<double>(record.storage_schema)},
        {"sequence", std::to_string(record.sequence)},
    };
}

internal::Json record_document(const TransparencyLogRecord& record) {
    auto payload = record_payload(record);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

internal::Json manifest_payload(const TransparencyLogIdentity& identity) {
    return internal::Json::Object{
        {"format", "rbfsafe-transparency-log"},
        {"log_identity", log_identity_json(identity)},
        {"schema", static_cast<double>(kSchema)},
    };
}

internal::Json manifest_document(const TransparencyLogIdentity& identity) {
    auto payload = manifest_payload(identity);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

Result<void> inspect_regular_file(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<void>::failure(StatusCode::IoError,
                                     "transparency-log file is missing or is not a direct regular file",
                                     path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to inspect transparency-log file size",
                                     path.string());
    }
    if (bytes > maximum_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "transparency-log file exceeds configured byte limit", path.string());
    }
    return Result<void>::success();
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    auto inspected = inspect_regular_file(path, maximum_bytes);
    if (!inspected)
        return inspected.error();
    return internal::read_json_file(path);
}

Result<void> inspect_log_root(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError, "failed to enumerate transparency-log root",
                                         directory.string());
        }
        const auto name = iterator->path().filename().string();
        if (name != "manifest.json" && name != "records" && name != ".writer-lock") {
            return Result<void>::failure(StatusCode::CorruptData, "unexpected transparency-log root entry",
                                         name);
        }
        if (name == ".writer-lock") {
            const auto status = std::filesystem::symlink_status(iterator->path(), error);
            if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
                return Result<void>::failure(StatusCode::CorruptData,
                                             "transparency-log writer lock is indirect or invalid", name);
            }
        }
    }
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to enumerate transparency-log root",
                                     directory.string());
    }
    return Result<void>::success();
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "transparency-log value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "transparency-log string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData, "transparency-log value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "transparency-log number field is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData, "transparency-log value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "transparency-log boolean field is invalid",
                                     std::string(key));
    }
    return value->as_bool();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "transparency-log decimal field is invalid", std::string(key));
    }
    return result;
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto value = number_field(object, key);
    if (!value || value.value() < 0.0 || std::floor(value.value()) != value.value() ||
        value.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "transparency-log enum field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<DeploymentRuntimeSnapshot> decode_runtime_snapshot(const internal::Json& object) {
    auto deployment = string_field(object, "deployment_id");
    auto robot = string_field(object, "robot_digest");
    auto controller = string_field(object, "controller_digest");
    auto platform = string_field(object, "platform_digest");
    auto runtime = string_field(object, "runtime_digest");
    auto observation_age = decimal_field(object, "observation_age_ns");
    auto command_latency = decimal_field(object, "command_latency_ns");
    auto control_period = decimal_field(object, "control_period_ns");
    auto missed = decimal_field(object, "consecutive_missed_cycles");
    auto monitor = bool_field(object, "runtime_monitor_active");
    auto transport = bool_field(object, "fail_closed_transport_active");
    auto artifacts = bool_field(object, "authenticated_artifacts");
    if (!deployment || !robot || !controller || !platform || !runtime || !observation_age ||
        !command_latency || !control_period || !missed || !monitor || !transport || !artifacts ||
        missed.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<DeploymentRuntimeSnapshot>::failure(StatusCode::CorruptData,
                                                          "transparency runtime snapshot is incomplete");
    }
    DeploymentRuntimeSnapshot result;
    result.deployment_id = std::move(deployment).value();
    result.robot_digest = std::move(robot).value();
    result.controller_digest = std::move(controller).value();
    result.platform_digest = std::move(platform).value();
    result.runtime_digest = std::move(runtime).value();
    result.observation_age_ns = observation_age.value();
    result.command_latency_ns = command_latency.value();
    result.control_period_ns = control_period.value();
    result.consecutive_missed_cycles = static_cast<std::uint32_t>(missed.value());
    result.runtime_monitor_active = monitor.value();
    result.fail_closed_transport_active = transport.value();
    result.authenticated_artifacts = artifacts.value();
    if (!valid_deployment_runtime_snapshot(result)) {
        return Result<DeploymentRuntimeSnapshot>::failure(StatusCode::CorruptData,
                                                          "transparency runtime snapshot is invalid");
    }
    return result;
}

Result<DeploymentTransparencyAnchor> decode_deployment_anchor(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto deployment = string_field(object, "deployment_id");
    auto profile = string_field(object, "reviewed_profile_id");
    auto approvals = string_field(object, "approval_set_id");
    auto robot = string_field(object, "robot_digest");
    auto controller = string_field(object, "controller_digest");
    auto platform = string_field(object, "platform_digest");
    auto runtime = string_field(object, "runtime_digest");
    auto root = string_field(object, "trust_root_bundle_id");
    auto checkpoint = string_field(object, "trust_checkpoint_id");
    auto bundle = string_field(object, "trust_bundle_id");
    auto sequence = decimal_field(object, "trust_bundle_sequence");
    auto head = string_field(object, "trust_head_record_id");
    if (!storage || !id || !deployment || !profile || !approvals || !robot || !controller || !platform ||
        !runtime || !root || !checkpoint || !bundle || !sequence || !head) {
        return Result<DeploymentTransparencyAnchor>::failure(StatusCode::CorruptData,
                                                             "deployment transparency anchor is incomplete");
    }
    DeploymentTransparencyAnchor result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.deployment_id = std::move(deployment).value();
    result.reviewed_profile_id = std::move(profile).value();
    result.approval_set_id = std::move(approvals).value();
    result.robot_digest = std::move(robot).value();
    result.controller_digest = std::move(controller).value();
    result.platform_digest = std::move(platform).value();
    result.runtime_digest = std::move(runtime).value();
    result.trust_root_bundle_id = std::move(root).value();
    result.trust_checkpoint_id = std::move(checkpoint).value();
    result.trust_bundle_id = std::move(bundle).value();
    result.trust_bundle_sequence = sequence.value();
    result.trust_head_record_id = std::move(head).value();
    if (!result.valid()) {
        return Result<DeploymentTransparencyAnchor>::failure(
            StatusCode::CorruptData, "deployment transparency anchor identity is invalid", result.id);
    }
    return result;
}

Result<IndependentRuntimeObservation> decode_observation(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto session = string_field(object, "session_id");
    auto ledger = string_field(object, "ledger_id");
    auto record = string_field(object, "ledger_record_id");
    auto authorization = string_field(object, "authorization_id");
    auto command_sequence = string_field(object, "command_sequence_id");
    auto command_index = decimal_field(object, "command_index");
    auto command_digest = string_field(object, "command_digest");
    const auto* runtime_value = object.is_object() ? object.find("runtime") : nullptr;
    auto observation_sequence = decimal_field(object, "observation_sequence");
    auto observed = decimal_field(object, "observed_monotonic_ns");
    auto monitor_state =
        enum_field(object, "monitor_state", static_cast<std::size_t>(ExecutionMonitorState::Fault));
    auto configuration = string_field(object, "configuration_digest");
    if (!storage || !id || !session || !ledger || !record || !authorization || !command_sequence ||
        !command_index || !command_digest || runtime_value == nullptr || !observation_sequence || !observed ||
        !monitor_state || !configuration) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::CorruptData, "independent runtime observation is incomplete");
    }
    auto runtime = decode_runtime_snapshot(*runtime_value);
    if (!runtime)
        return runtime.error();
    IndependentRuntimeObservation result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.session_id = std::move(session).value();
    result.ledger_id = std::move(ledger).value();
    result.ledger_record_id = std::move(record).value();
    result.authorization_id = std::move(authorization).value();
    result.command_sequence_id = std::move(command_sequence).value();
    result.command_index = command_index.value();
    result.command_digest = std::move(command_digest).value();
    result.runtime = std::move(runtime).value();
    result.observation_sequence = observation_sequence.value();
    result.observed_monotonic_ns = observed.value();
    result.monitor_state = static_cast<ExecutionMonitorState>(monitor_state.value());
    result.configuration_digest = std::move(configuration).value();
    if (!result.valid()) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::CorruptData, "independent runtime observation identity is invalid", result.id);
    }
    return result;
}

Result<RuntimeObservationPolicy> decode_observation_policy(const internal::Json& object) {
    auto minimum = decimal_field(object, "minimum_attestations");
    auto distinct = bool_field(object, "require_distinct_services");
    auto exclude = bool_field(object, "exclude_controller_service");
    if (!minimum || !distinct || !exclude || minimum.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<RuntimeObservationPolicy>::failure(StatusCode::CorruptData,
                                                         "runtime observation policy is incomplete");
    }
    RuntimeObservationPolicy result;
    result.minimum_attestations = static_cast<std::uint32_t>(minimum.value());
    result.require_distinct_services = distinct.value();
    result.exclude_controller_service = exclude.value();
    if (!valid_runtime_observation_policy(result)) {
        return Result<RuntimeObservationPolicy>::failure(StatusCode::CorruptData,
                                                         "runtime observation policy is invalid");
    }
    return result;
}

Result<RuntimeObservationAttestation> decode_attestation(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto observation = string_field(object, "observation_id");
    auto service = string_field(object, "source_service_id");
    auto key = string_field(object, "source_key_id");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !observation || !service || !key || !algorithm || !tag) {
        return Result<RuntimeObservationAttestation>::failure(
            StatusCode::CorruptData, "runtime observation attestation is incomplete");
    }
    RuntimeObservationAttestation result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.observation_id = std::move(observation).value();
    result.source_service_id = std::move(service).value();
    result.source_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<RuntimeObservationAttestation>::failure(
            StatusCode::CorruptData, "runtime observation attestation identity is invalid", result.id);
    }
    return result;
}

Result<RuntimeObservationAttestationSet> decode_attestation_set(const internal::Json& object,
                                                                const TransparencyLogLoadOptions& options,
                                                                std::size_t& total_attestations) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    const auto* observation_value = object.is_object() ? object.find("observation") : nullptr;
    const auto* policy_value = object.is_object() ? object.find("policy") : nullptr;
    const auto* attestations_value = object.is_object() ? object.find("attestations") : nullptr;
    if (!storage || !id || observation_value == nullptr || policy_value == nullptr ||
        attestations_value == nullptr || !attestations_value->is_array()) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::CorruptData, "runtime observation attestation set is incomplete");
    }
    if (attestations_value->as_array().size() > options.maximum_attestations_per_observation ||
        total_attestations > options.maximum_total_attestations ||
        attestations_value->as_array().size() > options.maximum_total_attestations - total_attestations) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::ResourceLimit, "runtime observation attestation count exceeds configured limits");
    }
    auto observation = decode_observation(*observation_value);
    auto policy = decode_observation_policy(*policy_value);
    if (!observation || !policy)
        return !observation ? observation.error() : policy.error();
    RuntimeObservationAttestationSet result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.observation = std::move(observation).value();
    result.policy = policy.value();
    result.attestations.reserve(attestations_value->as_array().size());
    for (const auto& value : attestations_value->as_array()) {
        if (options.cancellation.cancelled()) {
            return Result<RuntimeObservationAttestationSet>::failure(
                StatusCode::Cancelled, "transparency-log load cancelled while decoding attestations");
        }
        auto attestation = decode_attestation(value);
        if (!attestation)
            return attestation.error();
        result.attestations.push_back(std::move(attestation).value());
    }
    total_attestations += result.attestations.size();
    if (!result.valid()) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::CorruptData, "runtime observation attestation-set identity is invalid", result.id);
    }
    return result;
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
                                                        "transparency-log identity is incomplete");
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
        return Result<TransparencyLogIdentity>::failure(StatusCode::CorruptData,
                                                        "transparency-log identity is invalid", result.id);
    }
    return result;
}

Result<TransparencyLogLeaf> decode_leaf(const internal::Json& object,
                                        const TransparencyLogLoadOptions& options,
                                        std::size_t& total_attestations) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto index = decimal_field(object, "index");
    auto log_id = string_field(object, "log_id");
    auto kind =
        enum_field(object, "kind", static_cast<std::size_t>(TransparencyLeafKind::RuntimeObservation));
    const auto* anchor = object.is_object() ? object.find("deployment_anchor") : nullptr;
    const auto* observation = object.is_object() ? object.find("runtime_observation") : nullptr;
    if (!storage || !id || !index || !log_id || !kind || anchor == nullptr || observation == nullptr) {
        return Result<TransparencyLogLeaf>::failure(StatusCode::CorruptData,
                                                    "transparency-log leaf is incomplete");
    }
    TransparencyLogLeaf result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.index = index.value();
    result.log_id = std::move(log_id).value();
    result.kind = static_cast<TransparencyLeafKind>(kind.value());
    if (!anchor->is_null()) {
        auto decoded = decode_deployment_anchor(*anchor);
        if (!decoded)
            return decoded.error();
        result.deployment_anchor = std::move(decoded).value();
    }
    if (!observation->is_null()) {
        auto decoded = decode_attestation_set(*observation, options, total_attestations);
        if (!decoded)
            return decoded.error();
        result.runtime_observation = std::move(decoded).value();
    }
    if (!result.valid()) {
        return Result<TransparencyLogLeaf>::failure(StatusCode::CorruptData,
                                                    "transparency-log leaf identity is invalid", result.id);
    }
    return result;
}

Result<TransparencyLogCheckpoint> decode_checkpoint(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto log_id = string_field(object, "log_id");
    auto tree_size = decimal_field(object, "tree_size");
    auto root_hash = string_field(object, "root_hash");
    auto previous = string_field(object, "previous_checkpoint_id", true);
    auto service = string_field(object, "signer_service_id");
    auto key = string_field(object, "signer_key_id");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !log_id || !tree_size || !root_hash || !previous || !service || !key ||
        !algorithm || !tag) {
        return Result<TransparencyLogCheckpoint>::failure(StatusCode::CorruptData,
                                                          "transparency-log checkpoint is incomplete");
    }
    TransparencyLogCheckpoint result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.log_id = std::move(log_id).value();
    result.tree_size = tree_size.value();
    result.root_hash = std::move(root_hash).value();
    result.previous_checkpoint_id = std::move(previous).value();
    result.signer_service_id = std::move(service).value();
    result.signer_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<TransparencyLogCheckpoint>::failure(
            StatusCode::CorruptData, "transparency-log checkpoint identity is invalid", result.id);
    }
    return result;
}

Result<TransparencyLogRecord> decode_record(const internal::Json& document,
                                            const TransparencyLogLoadOptions& options,
                                            std::size_t& total_attestations) {
    auto format = string_field(document, "format");
    auto schema = enum_field(document, "schema", kMaximumRecognizableSchema);
    auto library_version = string_field(document, "library_version");
    auto identity = string_field(document, "identity");
    auto id = string_field(document, "id");
    auto sequence = decimal_field(document, "sequence");
    auto parent = string_field(document, "parent_id", true);
    auto log_id = string_field(document, "log_id");
    const auto* leaf_value = document.is_object() ? document.find("leaf") : nullptr;
    const auto* checkpoint_value = document.is_object() ? document.find("checkpoint") : nullptr;
    if (!format || !schema || !library_version || !identity || !id || !sequence || !parent || !log_id ||
        leaf_value == nullptr || checkpoint_value == nullptr) {
        return Result<TransparencyLogRecord>::failure(StatusCode::CorruptData,
                                                      "transparency-log record document is incomplete");
    }
    if (format.value() != "rbfsafe-transparency-log-record" || schema.value() != kSchema) {
        return Result<TransparencyLogRecord>::failure(StatusCode::IncompatibleFormat,
                                                      "unsupported transparency-log record schema");
    }
    auto leaf = decode_leaf(*leaf_value, options, total_attestations);
    auto checkpoint = decode_checkpoint(*checkpoint_value);
    if (!leaf || !checkpoint)
        return !leaf ? leaf.error() : checkpoint.error();
    TransparencyLogRecord result;
    result.storage_schema = static_cast<std::uint32_t>(schema.value());
    result.id = std::move(id).value();
    result.sequence = sequence.value();
    result.parent_id = std::move(parent).value();
    result.log_id = std::move(log_id).value();
    result.leaf = std::move(leaf).value();
    result.checkpoint = std::move(checkpoint).value();
    if (!result.valid() || !internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(record_payload(result).dump(false))) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::CorruptData, "transparency-log record identity or checksum is invalid", result.id);
    }
    return result;
}

std::string record_filename(const TransparencyLogRecord& record) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(20) << record.sequence << '-' << record.id << ".json";
    return stream.str();
}

Result<void> write_immutable_file(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable transparency-log record",
                                         destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable transparency-log record already exists",
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
        return Result<void>::failure(
            StatusCode::IoError, "failed to publish immutable transparency-log record", destination.string());
    }
    return Result<void>::success();
}

bool valid_load_options(const TransparencyLogLoadOptions& options) {
    return options.maximum_records > 0 && options.maximum_attestations_per_observation > 0 &&
           options.maximum_total_attestations > 0 && options.maximum_manifest_bytes > 0 &&
           options.maximum_record_bytes > 0;
}

} // namespace

namespace internal {

TransparencyLogWriteLock::TransparencyLogWriteLock(TransparencyLogWriteLock&& other) noexcept
    : path_(std::move(other.path_)), held_(other.held_) {
    other.held_ = false;
}

TransparencyLogWriteLock::~TransparencyLogWriteLock() {
    if (!held_)
        return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

Result<TransparencyLogWriteLock> TransparencyLogWriteLock::acquire(const std::filesystem::path& directory) {
    std::error_code directory_error;
    const auto directory_status = std::filesystem::symlink_status(directory, directory_error);
    if (directory_error || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status)) {
        return Result<TransparencyLogWriteLock>::failure(
            StatusCode::CorruptData, "transparency-log directory is missing or indirect", directory.string());
    }
    const auto path = directory / ".writer-lock";
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    if (error) {
        return Result<TransparencyLogWriteLock>::failure(
            StatusCode::IoError, "failed to acquire transparency-log writer lock", path.string());
    }
    if (!created) {
        return Result<TransparencyLogWriteLock>::failure(
            StatusCode::ResourceLimit, "transparency-log writer lock is already held", path.string());
    }
    return TransparencyLogWriteLock(path);
}

TransparencyLogWriteLock::TransparencyLogWriteLock(std::filesystem::path path) : path_(std::move(path)) {}

Result<void> append_transparency_log_record_file(const std::filesystem::path& directory,
                                                 const TransparencyLogRecord& record,
                                                 std::uintmax_t maximum_record_bytes) {
    if (directory.empty() || !record.valid() || maximum_record_bytes == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency-log append-file input is invalid");
    }
    const auto content = record_document(record).dump(true) + "\n";
    if (content.size() > maximum_record_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "transparency-log record exceeds configured publication byte limit",
                                     record.id);
    }
    return write_immutable_file(directory / "records" / record_filename(record), content);
}

} // namespace internal

Result<TransparencyLog> TransparencyLog::create(const std::filesystem::path& directory,
                                                TransparencyLogIdentity identity) {
    if (directory.empty() || directory == directory.root_path() || !identity.valid()) {
        return Result<TransparencyLog>::failure(StatusCode::InvalidArgument,
                                                "transparency-log creation input is invalid");
    }
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<TransparencyLog>::failure(
            StatusCode::IoError, "transparency-log destination already exists", directory.string());
    }
    if (error) {
        return Result<TransparencyLog>::failure(
            StatusCode::IoError, "failed to inspect transparency-log destination", directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<TransparencyLog>::failure(StatusCode::IoError,
                                                    "failed to create transparency-log parent",
                                                    directory.parent_path().string());
        }
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    const bool records_created = std::filesystem::create_directories(temporary / "records", error);
    if (error || !records_created) {
        return Result<TransparencyLog>::failure(StatusCode::IoError,
                                                "failed to create temporary transparency-log directory");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    auto manifest_written =
        internal::write_text_file(temporary / "manifest.json", manifest_document(identity).dump(true) + "\n");
    if (!manifest_written) {
        cleanup();
        return manifest_written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<TransparencyLog>::failure(StatusCode::IoError, "failed to publish transparency log",
                                                directory.string());
    }
    TransparencyLog result;
    result.directory_ = directory;
    result.identity_ = std::move(identity);
    result.options_ = TransparencyLogLoadOptions{};
    if (!result.valid()) {
        return Result<TransparencyLog>::failure(StatusCode::InternalError,
                                                "constructed transparency log is invalid");
    }
    return result;
}

Result<TransparencyLog> TransparencyLog::open(const std::filesystem::path& directory,
                                              const TransparencyLogIdentity& expected_identity,
                                              const std::string& expected_checkpoint_id,
                                              const TransparencyLogLoadOptions& options) {
    if (directory.empty() || !expected_identity.valid() ||
        (!expected_checkpoint_id.empty() && !internal::valid_sha256(expected_checkpoint_id)) ||
        !valid_load_options(options)) {
        return Result<TransparencyLog>::failure(StatusCode::InvalidArgument,
                                                "transparency-log open input is invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<TransparencyLog>::failure(StatusCode::Cancelled, "transparency-log open was cancelled");
    }
    std::error_code root_error;
    const auto root_status = std::filesystem::symlink_status(directory, root_error);
    if (root_error || std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                "transparency-log directory is missing or indirect");
    }
    auto root_inspected = inspect_log_root(directory);
    if (!root_inspected)
        return root_inspected.error();
    auto manifest = read_bounded_json(directory / "manifest.json", options.maximum_manifest_bytes);
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = enum_field(manifest.value(), "schema", kMaximumRecognizableSchema);
    auto library_version = string_field(manifest.value(), "library_version");
    auto checksum = string_field(manifest.value(), "identity");
    const auto* identity_value =
        manifest.value().is_object() ? manifest.value().find("log_identity") : nullptr;
    if (!format || !schema || !library_version || !checksum || identity_value == nullptr) {
        return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                "transparency-log manifest is incomplete");
    }
    if (format.value() != "rbfsafe-transparency-log" || schema.value() != kSchema) {
        return Result<TransparencyLog>::failure(StatusCode::IncompatibleFormat,
                                                "unsupported transparency-log manifest schema");
    }
    auto decoded_identity = decode_log_identity(*identity_value);
    if (!decoded_identity)
        return decoded_identity.error();
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(manifest_payload(decoded_identity.value()).dump(false)) ||
        decoded_identity.value().id != expected_identity.id ||
        decoded_identity.value().log_namespace != expected_identity.log_namespace ||
        decoded_identity.value().signer_service_id != expected_identity.signer_service_id ||
        decoded_identity.value().signer_key_id != expected_identity.signer_key_id ||
        decoded_identity.value().signer_public_key != expected_identity.signer_public_key) {
        return Result<TransparencyLog>::failure(
            StatusCode::IdentityMismatch,
            "transparency-log manifest does not match the caller-pinned identity",
            decoded_identity.value().id);
    }

    const auto records_directory = directory / "records";
    std::error_code error;
    const auto records_status = std::filesystem::symlink_status(records_directory, error);
    if (error || std::filesystem::is_symlink(records_status) ||
        !std::filesystem::is_directory(records_status)) {
        return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                "transparency-log records directory is missing or indirect");
    }
    std::vector<std::pair<std::string, TransparencyLogRecord>> decoded;
    std::size_t total_attestations = 0;
    for (std::filesystem::directory_iterator iterator(records_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<TransparencyLog>::failure(StatusCode::IoError,
                                                    "failed to enumerate transparency-log records");
        }
        if (options.cancellation.cancelled()) {
            return Result<TransparencyLog>::failure(StatusCode::Cancelled,
                                                    "transparency-log open was cancelled");
        }
        const auto filename = iterator->path().filename().string();
        const auto entry_status = std::filesystem::symlink_status(iterator->path(), error);
        if (error) {
            return Result<TransparencyLog>::failure(StatusCode::IoError,
                                                    "failed to inspect transparency-log record entry");
        }
        if (!std::filesystem::is_symlink(entry_status) && std::filesystem::is_regular_file(entry_status) &&
            iterator->path().extension() == ".json") {
            if (decoded.size() >= options.maximum_records) {
                return Result<TransparencyLog>::failure(
                    StatusCode::ResourceLimit, "transparency-log record count exceeds configured limit");
            }
            auto document = read_bounded_json(iterator->path(), options.maximum_record_bytes);
            if (!document)
                return document.error();
            auto record = decode_record(document.value(), options, total_attestations);
            if (!record)
                return record.error();
            decoded.emplace_back(filename, std::move(record).value());
        } else if (filename.find(".tmp-") == std::string::npos) {
            return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                    "unexpected transparency-log record entry", filename);
        }
    }
    if (error) {
        return Result<TransparencyLog>::failure(StatusCode::IoError,
                                                "failed to enumerate transparency-log records");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& left, const auto& right) {
        if (left.second.sequence != right.second.sequence)
            return left.second.sequence < right.second.sequence;
        return left.second.id < right.second.id;
    });
    std::vector<TransparencyLogRecord> records;
    records.reserve(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const auto& filename = decoded[index].first;
        const auto& record = decoded[index].second;
        if (record.sequence != index || filename != record_filename(record) ||
            record.log_id != expected_identity.id ||
            (index == 0 &&
             (!record.parent_id.empty() || !record.checkpoint.previous_checkpoint_id.empty())) ||
            (index > 0 &&
             (record.parent_id != decoded[index - 1].second.id ||
              record.checkpoint.previous_checkpoint_id != decoded[index - 1].second.checkpoint.id))) {
            return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                    "transparency-log record chain is invalid", record.id);
        }
        records.push_back(record);
    }
    const std::string current_checkpoint = records.empty() ? std::string{} : records.back().checkpoint.id;
    if (current_checkpoint != expected_checkpoint_id) {
        return Result<TransparencyLog>::failure(
            StatusCode::IdentityMismatch, "transparency-log head does not match the caller-pinned checkpoint",
            current_checkpoint);
    }
    TransparencyLog result;
    result.directory_ = directory;
    result.identity_ = std::move(decoded_identity).value();
    result.records_ = std::move(records);
    result.current_checkpoint_id_ = current_checkpoint;
    result.current_root_hash_ =
        result.records_.empty() ? std::string{} : result.records_.back().checkpoint.root_hash;
    for (std::size_t index = 0; index < result.records_.size(); ++index) {
        if (internal::transparency_append_merkle_leaf(result.merkle_frontier_, result.records_[index].leaf.id,
                                                      static_cast<std::uint64_t>(index))
                .empty()) {
            return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                    "loaded transparency log has an invalid Merkle frontier");
        }
    }
    result.options_ = options;
    if (!result.valid()) {
        return Result<TransparencyLog>::failure(StatusCode::CorruptData,
                                                "loaded transparency log is structurally invalid");
    }
    return result;
}

} // namespace rbfsafe
