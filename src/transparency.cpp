#include <rbfsafe/transparency.h>

#include "internal/certificate_utils.h"
#include "internal/deployment.h"
#include "internal/execution.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"
#include "internal/transparency.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;
using internal::sha256;

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumAttestations = 100'000;

bool valid_text(std::string_view value, std::size_t maximum = kMaximumIdentifierBytes) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return character >= 0x20U && character != 0x7fU; });
}

bool valid_monitor_state(ExecutionMonitorState state) {
    return state == ExecutionMonitorState::ArmedCertifiedSequence ||
           state == ExecutionMonitorState::Disarmed || state == ExecutionMonitorState::Fault;
}

bool valid_leaf_kind(TransparencyLeafKind kind) {
    return kind == TransparencyLeafKind::DeploymentAnchor || kind == TransparencyLeafKind::RuntimeObservation;
}

Json runtime_snapshot_json(const DeploymentRuntimeSnapshot& snapshot) {
    return Json(Json::Object{
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
    });
}

Json deployment_anchor_json(const DeploymentTransparencyAnchor& anchor, bool include_id) {
    Json::Object object{
        {"approval_set_id", anchor.approval_set_id},
        {"controller_digest", anchor.controller_digest},
        {"deployment_id", anchor.deployment_id},
        {"platform_digest", anchor.platform_digest},
        {"reviewed_profile_id", anchor.reviewed_profile_id},
        {"robot_digest", anchor.robot_digest},
        {"runtime_digest", anchor.runtime_digest},
        {"storage_schema", std::to_string(anchor.storage_schema)},
        {"trust_bundle_id", anchor.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(anchor.trust_bundle_sequence)},
        {"trust_checkpoint_id", anchor.trust_checkpoint_id},
        {"trust_head_record_id", anchor.trust_head_record_id},
        {"trust_root_bundle_id", anchor.trust_root_bundle_id},
    };
    if (include_id)
        object.emplace("id", anchor.id);
    return Json(std::move(object));
}

Json independent_observation_json(const IndependentRuntimeObservation& observation, bool include_id) {
    Json::Object object{
        {"authorization_id", observation.authorization_id},
        {"command_digest", observation.command_digest},
        {"command_index", std::to_string(observation.command_index)},
        {"command_sequence_id", observation.command_sequence_id},
        {"configuration_digest", observation.configuration_digest},
        {"ledger_id", observation.ledger_id},
        {"ledger_record_id", observation.ledger_record_id},
        {"monitor_state", static_cast<double>(static_cast<std::uint8_t>(observation.monitor_state))},
        {"observation_sequence", std::to_string(observation.observation_sequence)},
        {"observed_monotonic_ns", std::to_string(observation.observed_monotonic_ns)},
        {"runtime", runtime_snapshot_json(observation.runtime)},
        {"session_id", observation.session_id},
        {"storage_schema", std::to_string(observation.storage_schema)},
    };
    if (include_id)
        object.emplace("id", observation.id);
    return Json(std::move(object));
}

Json runtime_policy_json(const RuntimeObservationPolicy& policy) {
    return Json(Json::Object{
        {"exclude_controller_service", policy.exclude_controller_service},
        {"minimum_attestations", std::to_string(policy.minimum_attestations)},
        {"require_distinct_services", policy.require_distinct_services},
    });
}

Json runtime_attestation_json(const RuntimeObservationAttestation& attestation, bool include_id,
                              bool include_tag) {
    Json::Object object{
        {"algorithm", static_cast<double>(static_cast<std::uint8_t>(attestation.algorithm))},
        {"observation_id", attestation.observation_id},
        {"source_key_id", attestation.source_key_id},
        {"source_service_id", attestation.source_service_id},
        {"storage_schema", std::to_string(attestation.storage_schema)},
    };
    if (include_tag)
        object.emplace("authentication_tag", attestation.authentication_tag);
    if (include_id)
        object.emplace("id", attestation.id);
    return Json(std::move(object));
}

Json runtime_attestation_set_json(const RuntimeObservationAttestationSet& attestation_set, bool include_id) {
    Json::Array attestations;
    attestations.reserve(attestation_set.attestations.size());
    for (const auto& attestation : attestation_set.attestations)
        attestations.emplace_back(runtime_attestation_json(attestation, true, true));
    Json::Object object{
        {"attestations", Json(std::move(attestations))},
        {"observation", independent_observation_json(attestation_set.observation, true)},
        {"policy", runtime_policy_json(attestation_set.policy)},
        {"storage_schema", std::to_string(attestation_set.storage_schema)},
    };
    if (include_id)
        object.emplace("id", attestation_set.id);
    return Json(std::move(object));
}

Json transparency_identity_json(const TransparencyLogIdentity& identity, bool include_id) {
    Json::Object object{
        {"log_namespace", identity.log_namespace},
        {"signer_key_id", identity.signer_key_id},
        {"signer_public_key", internal::encode_hex(identity.signer_public_key)},
        {"signer_service_id", identity.signer_service_id},
        {"storage_schema", std::to_string(identity.storage_schema)},
    };
    if (include_id)
        object.emplace("id", identity.id);
    return Json(std::move(object));
}

Json transparency_leaf_json(const TransparencyLogLeaf& leaf, bool include_id) {
    Json::Object object{
        {"deployment_anchor",
         leaf.deployment_anchor ? deployment_anchor_json(*leaf.deployment_anchor, true) : Json(nullptr)},
        {"index", std::to_string(leaf.index)},
        {"kind", static_cast<double>(static_cast<std::uint8_t>(leaf.kind))},
        {"log_id", leaf.log_id},
        {"runtime_observation", leaf.runtime_observation
                                    ? runtime_attestation_set_json(*leaf.runtime_observation, true)
                                    : Json(nullptr)},
        {"storage_schema", std::to_string(leaf.storage_schema)},
    };
    if (include_id)
        object.emplace("id", leaf.id);
    return Json(std::move(object));
}

Json transparency_checkpoint_json(const TransparencyLogCheckpoint& checkpoint, bool include_id,
                                  bool include_tag) {
    Json::Object object{
        {"algorithm", static_cast<double>(static_cast<std::uint8_t>(checkpoint.algorithm))},
        {"log_id", checkpoint.log_id},
        {"previous_checkpoint_id", checkpoint.previous_checkpoint_id},
        {"root_hash", checkpoint.root_hash},
        {"signer_key_id", checkpoint.signer_key_id},
        {"signer_service_id", checkpoint.signer_service_id},
        {"storage_schema", std::to_string(checkpoint.storage_schema)},
        {"tree_size", std::to_string(checkpoint.tree_size)},
    };
    if (include_tag)
        object.emplace("authentication_tag", checkpoint.authentication_tag);
    if (include_id)
        object.emplace("id", checkpoint.id);
    return Json(std::move(object));
}

Json transparency_record_json(const TransparencyLogRecord& record, bool include_id) {
    Json::Object object{
        {"checkpoint", transparency_checkpoint_json(record.checkpoint, true, true)},
        {"leaf", transparency_leaf_json(record.leaf, true)},
        {"log_id", record.log_id},
        {"parent_id", record.parent_id},
        {"sequence", std::to_string(record.sequence)},
        {"storage_schema", std::to_string(record.storage_schema)},
    };
    if (include_id)
        object.emplace("id", record.id);
    return Json(std::move(object));
}

Json inclusion_proof_json(const TransparencyInclusionProof& proof, bool include_id) {
    Json::Array siblings;
    siblings.reserve(proof.sibling_hashes.size());
    for (const auto& sibling : proof.sibling_hashes)
        siblings.emplace_back(sibling);
    Json::Object object{
        {"checkpoint_id", proof.checkpoint_id},
        {"leaf_id", proof.leaf_id},
        {"leaf_index", std::to_string(proof.leaf_index)},
        {"log_id", proof.log_id},
        {"root_hash", proof.root_hash},
        {"sibling_hashes", Json(std::move(siblings))},
        {"storage_schema", std::to_string(proof.storage_schema)},
        {"tree_size", std::to_string(proof.tree_size)},
    };
    if (include_id)
        object.emplace("id", proof.id);
    return Json(std::move(object));
}

Json consistency_witness_json(const TransparencyConsistencyWitness& witness, bool include_id) {
    Json::Array leaves;
    leaves.reserve(witness.ordered_leaf_ids.size());
    for (const auto& leaf : witness.ordered_leaf_ids)
        leaves.emplace_back(leaf);
    Json::Object object{
        {"log_id", witness.log_id},
        {"new_checkpoint_id", witness.new_checkpoint_id},
        {"new_root_hash", witness.new_root_hash},
        {"new_tree_size", std::to_string(witness.new_tree_size)},
        {"old_checkpoint_id", witness.old_checkpoint_id},
        {"old_root_hash", witness.old_root_hash},
        {"old_tree_size", std::to_string(witness.old_tree_size)},
        {"ordered_leaf_ids", Json(std::move(leaves))},
        {"storage_schema", std::to_string(witness.storage_schema)},
    };
    if (include_id)
        object.emplace("id", witness.id);
    return Json(std::move(object));
}

Json merkle_subtree_json(const TransparencyMerkleSubtree& subtree) {
    return Json::Object{
        {"hash", subtree.hash},
        {"level", std::to_string(subtree.level)},
    };
}

Json compact_consistency_proof_json(const TransparencyCompactConsistencyProof& proof, bool include_id) {
    Json::Array old_frontier;
    old_frontier.reserve(proof.old_frontier.size());
    for (const auto& subtree : proof.old_frontier)
        old_frontier.emplace_back(merkle_subtree_json(subtree));
    Json::Array appended_subtrees;
    appended_subtrees.reserve(proof.appended_subtrees.size());
    for (const auto& subtree : proof.appended_subtrees)
        appended_subtrees.emplace_back(merkle_subtree_json(subtree));
    Json::Object object{
        {"appended_subtrees", Json(std::move(appended_subtrees))},
        {"log_id", proof.log_id},
        {"new_checkpoint_id", proof.new_checkpoint_id},
        {"new_root_hash", proof.new_root_hash},
        {"new_tree_size", std::to_string(proof.new_tree_size)},
        {"old_checkpoint_id", proof.old_checkpoint_id},
        {"old_frontier", Json(std::move(old_frontier))},
        {"old_root_hash", proof.old_root_hash},
        {"old_tree_size", std::to_string(proof.old_tree_size)},
        {"storage_schema", std::to_string(proof.storage_schema)},
    };
    if (include_id)
        object.emplace("id", proof.id);
    return Json(std::move(object));
}

Json audit_report_json(const TransparencyLogAuditReport& report, bool include_id) {
    Json::Object object{
        {"current_checkpoint_id", report.current_checkpoint_id},
        {"current_root_hash", report.current_root_hash},
        {"deployment_anchor_count", std::to_string(report.deployment_anchor_count)},
        {"log_id", report.log_id},
        {"runtime_observation_count", std::to_string(report.runtime_observation_count)},
        {"verified_records", std::to_string(report.verified_records)},
    };
    if (include_id)
        object.emplace("id", report.id);
    return Json(std::move(object));
}

std::string merkle_node_hash(const std::string& left, const std::string& right) {
    return sha256(std::string("rbfsafe-transparency-merkle-node-v1\n") + left + right);
}

Result<TransparencyLogCheckpoint> sign_checkpoint(const TransparencyLogIdentity& identity,
                                                  TransparencyLogCheckpoint checkpoint,
                                                  std::span<const std::byte> secret_key) {
    if (!identity.valid() || checkpoint.log_id != identity.id ||
        secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<TransparencyLogCheckpoint>::failure(StatusCode::InvalidArgument,
                                                          "transparency checkpoint signing input is invalid");
    }
    checkpoint.signer_service_id = identity.signer_service_id;
    checkpoint.signer_key_id = identity.signer_key_id;
    checkpoint.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::transparency_log_checkpoint_message(checkpoint);
    auto signature = ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), secret_key);
    if (!signature)
        return signature.error();
    checkpoint.authentication_tag = internal::encode_hex(signature.value());
    checkpoint.id = internal::transparency_log_checkpoint_identity(checkpoint);
    auto verified = verify_transparency_log_checkpoint(identity, checkpoint);
    if (!verified)
        return verified.error();
    return checkpoint;
}

Result<void> validate_attestation_set_policy(const BoundedExecutionSession& session,
                                             const RuntimeObservationAttestationSet& attestation_set) {
    if (attestation_set.attestations.size() < attestation_set.policy.minimum_attestations) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "runtime observation attestation quorum is not satisfied",
                                     attestation_set.id);
    }
    std::set<std::string> services;
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& attestation : attestation_set.attestations) {
        if (!keys.emplace(attestation.source_service_id, attestation.source_key_id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "runtime observation contains a duplicate source key",
                                         attestation.source_key_id);
        }
        if (attestation_set.policy.require_distinct_services &&
            !services.emplace(attestation.source_service_id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "runtime observation sources are not service-distinct",
                                         attestation.source_service_id);
        }
        services.emplace(attestation.source_service_id);
        if (attestation_set.policy.exclude_controller_service &&
            attestation.source_service_id == session.request().controller.service_id) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "controller service cannot attest an independent runtime observation",
                attestation.source_service_id);
        }
    }
    return Result<void>::success();
}

} // namespace

std::string internal::deployment_transparency_anchor_identity(const DeploymentTransparencyAnchor& anchor) {
    return sha256(std::string("rbfsafe-deployment-transparency-anchor-identity-v1\n") +
                  deployment_anchor_json(anchor, false).dump(false));
}

std::string
internal::independent_runtime_observation_identity(const IndependentRuntimeObservation& observation) {
    return sha256(std::string("rbfsafe-independent-runtime-observation-identity-v1\n") +
                  independent_observation_json(observation, false).dump(false));
}

std::string
internal::runtime_observation_attestation_message(const RuntimeObservationAttestation& attestation) {
    return std::string("rbfsafe-runtime-observation-attestation-signature-v1\n") +
           runtime_attestation_json(attestation, false, false).dump(false);
}

std::string
internal::runtime_observation_attestation_identity(const RuntimeObservationAttestation& attestation) {
    return sha256(std::string("rbfsafe-runtime-observation-attestation-identity-v1\n") +
                  runtime_attestation_json(attestation, false, true).dump(false));
}

std::string internal::runtime_observation_attestation_set_identity(
    const RuntimeObservationAttestationSet& attestation_set) {
    return sha256(std::string("rbfsafe-runtime-observation-attestation-set-identity-v1\n") +
                  runtime_attestation_set_json(attestation_set, false).dump(false));
}

std::string internal::transparency_log_identity_value(const TransparencyLogIdentity& identity) {
    return sha256(std::string("rbfsafe-transparency-log-identity-v1\n") +
                  transparency_identity_json(identity, false).dump(false));
}

std::string internal::transparency_log_leaf_identity(const TransparencyLogLeaf& leaf) {
    return sha256(std::string("rbfsafe-transparency-log-leaf-identity-v1\n") +
                  transparency_leaf_json(leaf, false).dump(false));
}

std::string internal::transparency_log_checkpoint_message(const TransparencyLogCheckpoint& checkpoint) {
    return std::string("rbfsafe-transparency-log-checkpoint-signature-v1\n") +
           transparency_checkpoint_json(checkpoint, false, false).dump(false);
}

std::string internal::transparency_log_checkpoint_identity(const TransparencyLogCheckpoint& checkpoint) {
    return sha256(std::string("rbfsafe-transparency-log-checkpoint-identity-v1\n") +
                  transparency_checkpoint_json(checkpoint, false, true).dump(false));
}

std::string internal::transparency_log_record_identity(const TransparencyLogRecord& record) {
    return sha256(std::string("rbfsafe-transparency-log-record-identity-v1\n") +
                  transparency_record_json(record, false).dump(false));
}

std::string internal::transparency_inclusion_proof_identity(const TransparencyInclusionProof& proof) {
    return sha256(std::string("rbfsafe-transparency-inclusion-proof-identity-v1\n") +
                  inclusion_proof_json(proof, false).dump(false));
}

std::string
internal::transparency_consistency_witness_identity(const TransparencyConsistencyWitness& witness) {
    return sha256(std::string("rbfsafe-transparency-consistency-witness-identity-v1\n") +
                  consistency_witness_json(witness, false).dump(false));
}

std::string
internal::transparency_compact_consistency_proof_identity(const TransparencyCompactConsistencyProof& proof) {
    return sha256(std::string("rbfsafe-transparency-compact-consistency-proof-identity-v1\n") +
                  compact_consistency_proof_json(proof, false).dump(false));
}

std::string internal::transparency_log_audit_report_identity(const TransparencyLogAuditReport& report) {
    return sha256(std::string("rbfsafe-transparency-log-audit-report-identity-v1\n") +
                  audit_report_json(report, false).dump(false));
}

std::string internal::transparency_leaf_hash(const std::string& leaf_id) {
    return sha256(std::string("rbfsafe-transparency-merkle-leaf-v1\n") + leaf_id);
}

std::string internal::transparency_merkle_root(const std::vector<std::string>& ordered_leaf_ids) {
    if (ordered_leaf_ids.empty())
        return {};
    std::vector<std::string> level;
    level.reserve(ordered_leaf_ids.size());
    for (const auto& leaf_id : ordered_leaf_ids)
        level.push_back(transparency_leaf_hash(leaf_id));
    while (level.size() > 1) {
        std::vector<std::string> next;
        next.reserve((level.size() + 1U) / 2U);
        for (std::size_t index = 0; index < level.size(); index += 2U) {
            if (index + 1U < level.size())
                next.push_back(merkle_node_hash(level[index], level[index + 1U]));
            else
                next.push_back(level[index]);
        }
        level = std::move(next);
    }
    return level.front();
}

std::string internal::transparency_merkle_frontier_root(const std::array<std::string, 64>& frontier) {
    std::string root;
    for (const auto& subtree : frontier) {
        if (subtree.empty())
            continue;
        root = root.empty() ? subtree : merkle_node_hash(subtree, root);
    }
    return root;
}

std::string internal::transparency_append_merkle_leaf(std::array<std::string, 64>& frontier,
                                                      const std::string& leaf_id,
                                                      std::uint64_t previous_tree_size) {
    if (!internal::valid_sha256(leaf_id)) {
        return {};
    }
    return transparency_append_merkle_subtree(frontier, transparency_leaf_hash(leaf_id), 0,
                                              previous_tree_size);
}

std::string internal::transparency_append_merkle_subtree(std::array<std::string, 64>& frontier,
                                                         const std::string& subtree_hash,
                                                         std::uint8_t subtree_level,
                                                         std::uint64_t previous_tree_size) {
    if (!internal::valid_sha256(subtree_hash) || subtree_level >= frontier.size()) {
        return {};
    }
    const std::uint64_t subtree_size = std::uint64_t{1} << subtree_level;
    if ((previous_tree_size & (subtree_size - 1U)) != 0U ||
        previous_tree_size > std::numeric_limits<std::uint64_t>::max() - subtree_size) {
        return {};
    }
    std::string subtree = subtree_hash;
    std::size_t level = subtree_level;
    std::uint64_t occupied = previous_tree_size >> subtree_level;
    while ((occupied & 1U) != 0U) {
        if (level >= frontier.size() || frontier[level].empty())
            return {};
        subtree = merkle_node_hash(frontier[level], subtree);
        frontier[level].clear();
        occupied >>= 1U;
        ++level;
    }
    if (level >= frontier.size() || !frontier[level].empty())
        return {};
    frontier[level] = std::move(subtree);
    return transparency_merkle_frontier_root(frontier);
}

Result<DeploymentTransparencyAnchor> DeploymentTransparencyAnchor::create(
    const ReviewedDeploymentProfile& reviewed, const ServiceTrustHistory& trust_history,
    const ServiceTrustCheckpoint& trust_checkpoint, const std::string& expected_checkpoint_id) {
    if (!reviewed.valid() || !trust_history.valid() || !trust_checkpoint.valid() ||
        !internal::valid_sha256(expected_checkpoint_id)) {
        return Result<DeploymentTransparencyAnchor>::failure(
            StatusCode::InvalidArgument, "deployment transparency anchor input is invalid");
    }
    auto verified = verify_service_trust_checkpoint(trust_history, trust_checkpoint, expected_checkpoint_id);
    if (!verified)
        return verified.error();
    const auto& profile = reviewed.profile();
    if (profile.trust_root_bundle_id != trust_checkpoint.root_bundle_id ||
        profile.trust_checkpoint_id != trust_checkpoint.id ||
        profile.trust_bundle_id != trust_checkpoint.head_bundle_id ||
        profile.trust_bundle_sequence != trust_checkpoint.head_sequence ||
        trust_history.current_bundle_id() != trust_checkpoint.head_bundle_id ||
        trust_history.records().empty() ||
        trust_history.records().back().id != trust_checkpoint.head_record_id) {
        return Result<DeploymentTransparencyAnchor>::failure(
            StatusCode::IdentityMismatch,
            "reviewed deployment profile is not bound to the verified trust checkpoint", profile.id);
    }
    DeploymentTransparencyAnchor anchor;
    anchor.deployment_id = profile.deployment_id;
    anchor.reviewed_profile_id = profile.id;
    anchor.approval_set_id = reviewed.approval_set().id;
    anchor.robot_digest = profile.robot_digest;
    anchor.controller_digest = profile.controller_digest;
    anchor.platform_digest = profile.platform_digest;
    anchor.runtime_digest = profile.runtime_digest;
    anchor.trust_root_bundle_id = profile.trust_root_bundle_id;
    anchor.trust_checkpoint_id = profile.trust_checkpoint_id;
    anchor.trust_bundle_id = profile.trust_bundle_id;
    anchor.trust_bundle_sequence = profile.trust_bundle_sequence;
    anchor.trust_head_record_id = trust_checkpoint.head_record_id;
    anchor.id = internal::deployment_transparency_anchor_identity(anchor);
    if (!anchor.valid()) {
        return Result<DeploymentTransparencyAnchor>::failure(
            StatusCode::InternalError, "constructed deployment transparency anchor is invalid");
    }
    return anchor;
}

bool DeploymentTransparencyAnchor::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && valid_text(deployment_id) &&
           internal::valid_sha256(reviewed_profile_id) && internal::valid_sha256(approval_set_id) &&
           internal::valid_sha256(robot_digest) && internal::valid_sha256(controller_digest) &&
           internal::valid_sha256(platform_digest) && internal::valid_sha256(runtime_digest) &&
           internal::valid_sha256(trust_root_bundle_id) && internal::valid_sha256(trust_checkpoint_id) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           internal::valid_sha256(trust_head_record_id) &&
           id == internal::deployment_transparency_anchor_identity(*this);
}

Result<IndependentRuntimeObservation>
IndependentRuntimeObservation::create(const BoundedExecutionSession& session, const ExecutionLedger& ledger,
                                      const ExecutionCommandAuthorization& authorization,
                                      IndependentRuntimeObservationInput input) {
    if (!session.valid() || !ledger.valid() || !authorization.valid() ||
        !valid_deployment_runtime_snapshot(input.runtime) || input.observation_sequence == 0 ||
        input.observed_monotonic_ns == 0 || !valid_monitor_state(input.monitor_state) ||
        !internal::valid_sha256(input.configuration_digest)) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::InvalidArgument, "independent runtime observation input is invalid");
    }
    if (ledger.session_id() != session.id() || authorization.session_id != session.id() ||
        authorization.command_sequence_id != session.command_sequence().id ||
        authorization.command_index >= session.command_sequence().commands.size() ||
        authorization.command_digest !=
            internal::execution_command_digest(
                session.command_sequence().commands[authorization.command_index]) ||
        ledger.records().empty()) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::IdentityMismatch, "runtime observation dependencies do not match the session",
            authorization.id);
    }
    const auto& current = ledger.records().back();
    if (current.id != ledger.current_record_id() ||
        current.type != ExecutionLedgerRecordType::CommandAuthorized || !current.authorization ||
        current.authorization->id != authorization.id) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::IdentityMismatch,
            "runtime observation is not bound to the outstanding ledger authorization",
            ledger.current_record_id());
    }
    if (input.observed_monotonic_ns < authorization.valid_from_monotonic_ns ||
        input.observed_monotonic_ns > authorization.valid_through_monotonic_ns) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::InvalidArgument, "runtime observation lies outside the exact authorization window",
            authorization.id);
    }
    IndependentRuntimeObservation observation;
    observation.session_id = session.id();
    observation.ledger_id = ledger.id();
    observation.ledger_record_id = current.id;
    observation.authorization_id = authorization.id;
    observation.command_sequence_id = authorization.command_sequence_id;
    observation.command_index = authorization.command_index;
    observation.command_digest = authorization.command_digest;
    observation.runtime = std::move(input.runtime);
    observation.observation_sequence = input.observation_sequence;
    observation.observed_monotonic_ns = input.observed_monotonic_ns;
    observation.monitor_state = input.monitor_state;
    observation.configuration_digest = std::move(input.configuration_digest);
    observation.id = internal::independent_runtime_observation_identity(observation);
    if (!observation.valid()) {
        return Result<IndependentRuntimeObservation>::failure(
            StatusCode::InternalError, "constructed independent runtime observation is invalid");
    }
    return observation;
}

bool IndependentRuntimeObservation::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) &&
           internal::valid_sha256(session_id) && internal::valid_sha256(ledger_id) &&
           internal::valid_sha256(ledger_record_id) && internal::valid_sha256(authorization_id) &&
           internal::valid_sha256(command_sequence_id) && internal::valid_sha256(command_digest) &&
           valid_deployment_runtime_snapshot(runtime) && observation_sequence > 0 &&
           observed_monotonic_ns > 0 && valid_monitor_state(monitor_state) &&
           internal::valid_sha256(configuration_digest) &&
           id == internal::independent_runtime_observation_identity(*this);
}

bool RuntimeObservationAttestation::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) &&
           internal::valid_sha256(observation_id) && valid_text(source_service_id) &&
           internal::valid_sha256(source_key_id) && algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           authentication_tag.size() == kEd25519SignatureBytes * 2U &&
           std::all_of(authentication_tag.begin(), authentication_tag.end(),
                       [](unsigned char value) {
                           return std::isdigit(value) != 0 || (value >= 'a' && value <= 'f');
                       }) &&
           id == internal::runtime_observation_attestation_identity(*this);
}

bool valid_runtime_observation_policy(const RuntimeObservationPolicy& policy) {
    return policy.minimum_attestations > 0 && policy.minimum_attestations <= kMaximumAttestations;
}

bool RuntimeObservationAttestationSet::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !observation.valid() ||
        !valid_runtime_observation_policy(policy) || attestations.size() < policy.minimum_attestations ||
        attestations.size() > kMaximumAttestations ||
        id != internal::runtime_observation_attestation_set_identity(*this)) {
        return false;
    }
    for (std::size_t index = 0; index < attestations.size(); ++index) {
        if (!attestations[index].valid() || attestations[index].observation_id != observation.id ||
            (index > 0 &&
             std::tie(attestations[index - 1].source_service_id, attestations[index - 1].source_key_id) >=
                 std::tie(attestations[index].source_service_id, attestations[index].source_key_id))) {
            return false;
        }
        if (policy.require_distinct_services && index > 0 &&
            attestations[index - 1].source_service_id == attestations[index].source_service_id) {
            return false;
        }
    }
    return true;
}

Result<RuntimeObservationAttestation>
sign_runtime_observation(const IndependentRuntimeObservation& observation, std::string source_service_id,
                         std::string source_key_id, std::span<const std::byte> ed25519_secret_key) {
    if (!observation.valid() || !valid_text(source_service_id) || !internal::valid_sha256(source_key_id) ||
        ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<RuntimeObservationAttestation>::failure(StatusCode::InvalidArgument,
                                                              "runtime observation signing input is invalid");
    }
    RuntimeObservationAttestation attestation;
    attestation.observation_id = observation.id;
    attestation.source_service_id = std::move(source_service_id);
    attestation.source_key_id = std::move(source_key_id);
    const auto message = internal::runtime_observation_attestation_message(attestation);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    attestation.authentication_tag = internal::encode_hex(signature.value());
    attestation.id = internal::runtime_observation_attestation_identity(attestation);
    if (!attestation.valid()) {
        return Result<RuntimeObservationAttestation>::failure(
            StatusCode::InternalError, "constructed runtime observation attestation is invalid");
    }
    return attestation;
}

Result<void> verify_runtime_observation_attestation(const IndependentRuntimeObservation& observation,
                                                    const RuntimeObservationAttestation& attestation,
                                                    const ServiceTrustBundle& trust_bundle) {
    if (!observation.valid() || !attestation.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "runtime observation verification input is invalid");
    }
    if (attestation.observation_id != observation.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "runtime observation attestation targets another observation",
                                     attestation.id);
    }
    auto trusted =
        trusted_service_public_key(trust_bundle, attestation.source_service_id, attestation.source_key_id,
                                   ArtifactTransferOperation::Publish, trust_bundle.sequence());
    if (!trusted)
        return trusted.error();
    if (trusted.value().state != ServiceKeyState::Active) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "runtime observation source key is not active", trusted.value().id);
    }
    auto signature = internal::decode_hex(attestation.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::runtime_observation_attestation_message(attestation);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

Result<RuntimeObservationAttestationSet> assemble_runtime_observation_attestations(
    const BoundedExecutionSession& session, IndependentRuntimeObservation observation,
    RuntimeObservationPolicy policy, std::vector<RuntimeObservationAttestation> attestations,
    const ServiceTrustBundle& trust_bundle) {
    if (!session.valid() || !observation.valid() || !valid_runtime_observation_policy(policy) ||
        !trust_bundle.valid() || attestations.size() > kMaximumAttestations) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::InvalidArgument, "runtime observation attestation-set input is invalid");
    }
    if (observation.session_id != session.id()) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::IdentityMismatch, "runtime observation targets another execution session",
            observation.id);
    }
    std::sort(attestations.begin(), attestations.end(), [](const auto& first, const auto& second) {
        return std::tie(first.source_service_id, first.source_key_id) <
               std::tie(second.source_service_id, second.source_key_id);
    });
    RuntimeObservationAttestationSet result;
    result.observation = std::move(observation);
    result.policy = policy;
    result.attestations = std::move(attestations);
    for (const auto& attestation : result.attestations) {
        auto verified = verify_runtime_observation_attestation(result.observation, attestation, trust_bundle);
        if (!verified)
            return verified.error();
    }
    auto policy_verified = validate_attestation_set_policy(session, result);
    if (!policy_verified)
        return policy_verified.error();
    result.id = internal::runtime_observation_attestation_set_identity(result);
    if (!result.valid()) {
        return Result<RuntimeObservationAttestationSet>::failure(
            StatusCode::IdentityMismatch, "runtime observation attestation set is not canonical");
    }
    return result;
}

Result<void> verify_runtime_observation_attestations(const BoundedExecutionSession& session,
                                                     const RuntimeObservationAttestationSet& attestation_set,
                                                     const ServiceTrustBundle& trust_bundle) {
    if (!session.valid() || !attestation_set.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "runtime observation set verification input is invalid");
    }
    if (attestation_set.observation.session_id != session.id()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "runtime observation set targets another execution session",
                                     attestation_set.id);
    }
    auto policy_verified = validate_attestation_set_policy(session, attestation_set);
    if (!policy_verified)
        return policy_verified.error();
    for (const auto& attestation : attestation_set.attestations) {
        auto verified =
            verify_runtime_observation_attestation(attestation_set.observation, attestation, trust_bundle);
        if (!verified)
            return verified.error();
    }
    return Result<void>::success();
}

Result<TransparencyLogIdentity>
TransparencyLogIdentity::create(std::string log_namespace, std::string signer_service_id,
                                std::string signer_key_id, std::span<const std::byte> signer_public_key) {
    if (!valid_text(log_namespace) || !valid_text(signer_service_id) ||
        !internal::valid_sha256(signer_key_id) || signer_public_key.size() != kEd25519PublicKeyBytes) {
        return Result<TransparencyLogIdentity>::failure(StatusCode::InvalidArgument,
                                                        "transparency-log identity input is invalid");
    }
    TransparencyLogIdentity identity;
    identity.log_namespace = std::move(log_namespace);
    identity.signer_service_id = std::move(signer_service_id);
    identity.signer_key_id = std::move(signer_key_id);
    std::copy(signer_public_key.begin(), signer_public_key.end(), identity.signer_public_key.begin());
    identity.id = internal::transparency_log_identity_value(identity);
    if (!identity.valid()) {
        return Result<TransparencyLogIdentity>::failure(StatusCode::InternalError,
                                                        "constructed transparency-log identity is invalid");
    }
    return identity;
}

bool TransparencyLogIdentity::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && valid_text(log_namespace) &&
           valid_text(signer_service_id) && internal::valid_sha256(signer_key_id) &&
           id == internal::transparency_log_identity_value(*this);
}

bool TransparencyLogLeaf::valid() const {
    const bool payload_valid = (kind == TransparencyLeafKind::DeploymentAnchor && deployment_anchor &&
                                deployment_anchor->valid() && !runtime_observation) ||
                               (kind == TransparencyLeafKind::RuntimeObservation && runtime_observation &&
                                runtime_observation->valid() && !deployment_anchor);
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           valid_leaf_kind(kind) && payload_valid && id == internal::transparency_log_leaf_identity(*this);
}

bool TransparencyLogCheckpoint::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           tree_size > 0 && internal::valid_sha256(root_hash) &&
           (tree_size == 1 ? previous_checkpoint_id.empty()
                           : internal::valid_sha256(previous_checkpoint_id)) &&
           valid_text(signer_service_id) && internal::valid_sha256(signer_key_id) &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           authentication_tag.size() == kEd25519SignatureBytes * 2U &&
           std::all_of(authentication_tag.begin(), authentication_tag.end(),
                       [](unsigned char value) {
                           return std::isdigit(value) != 0 || (value >= 'a' && value <= 'f');
                       }) &&
           id == internal::transparency_log_checkpoint_identity(*this);
}

bool TransparencyLogRecord::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           (sequence == 0 ? parent_id.empty() : internal::valid_sha256(parent_id)) && leaf.valid() &&
           checkpoint.valid() && leaf.index == sequence && leaf.log_id == log_id &&
           checkpoint.log_id == log_id && checkpoint.tree_size == sequence + 1U &&
           id == internal::transparency_log_record_identity(*this);
}

bool TransparencyInclusionProof::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !internal::valid_sha256(log_id) ||
        !internal::valid_sha256(checkpoint_id) || !internal::valid_sha256(leaf_id) || tree_size == 0 ||
        leaf_index >= tree_size || !internal::valid_sha256(root_hash) || sibling_hashes.size() > 64U ||
        id != internal::transparency_inclusion_proof_identity(*this)) {
        return false;
    }
    return std::all_of(sibling_hashes.begin(), sibling_hashes.end(), internal::valid_sha256);
}

bool TransparencyConsistencyWitness::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !internal::valid_sha256(log_id) ||
        !internal::valid_sha256(old_checkpoint_id) || !internal::valid_sha256(new_checkpoint_id) ||
        old_tree_size == 0 || old_tree_size >= new_tree_size || ordered_leaf_ids.size() != new_tree_size ||
        !internal::valid_sha256(old_root_hash) || !internal::valid_sha256(new_root_hash) ||
        id != internal::transparency_consistency_witness_identity(*this)) {
        return false;
    }
    return std::all_of(ordered_leaf_ids.begin(), ordered_leaf_ids.end(), internal::valid_sha256);
}

bool TransparencyMerkleSubtree::valid() const { return level < 64U && internal::valid_sha256(hash); }

bool TransparencyCompactConsistencyProof::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !internal::valid_sha256(log_id) ||
        !internal::valid_sha256(old_checkpoint_id) || !internal::valid_sha256(new_checkpoint_id) ||
        old_tree_size == 0 || old_tree_size >= new_tree_size || !internal::valid_sha256(old_root_hash) ||
        !internal::valid_sha256(new_root_hash) || old_frontier.empty() || old_frontier.size() > 64U ||
        appended_subtrees.empty() || appended_subtrees.size() > 128U ||
        id != internal::transparency_compact_consistency_proof_identity(*this)) {
        return false;
    }
    std::size_t frontier_index = 0;
    for (std::uint8_t level = 0; level < 64U; ++level) {
        if (((old_tree_size >> level) & 1U) == 0U)
            continue;
        if (frontier_index >= old_frontier.size() || old_frontier[frontier_index].level != level ||
            !old_frontier[frontier_index].valid()) {
            return false;
        }
        ++frontier_index;
    }
    if (frontier_index != old_frontier.size())
        return false;
    std::uint64_t represented_size = old_tree_size;
    for (const auto& subtree : appended_subtrees) {
        if (!subtree.valid())
            return false;
        const std::uint64_t subtree_size = std::uint64_t{1} << subtree.level;
        if ((represented_size & (subtree_size - 1U)) != 0U ||
            represented_size > std::numeric_limits<std::uint64_t>::max() - subtree_size) {
            return false;
        }
        represented_size += subtree_size;
        if (represented_size > new_tree_size)
            return false;
    }
    return represented_size == new_tree_size;
}

bool TransparencyLogAuditReport::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(log_id) &&
           (verified_records == 0 ? current_checkpoint_id.empty() && current_root_hash.empty()
                                  : internal::valid_sha256(current_checkpoint_id) &&
                                        internal::valid_sha256(current_root_hash)) &&
           deployment_anchor_count + runtime_observation_count == verified_records &&
           id == internal::transparency_log_audit_report_identity(*this);
}

Result<void> verify_transparency_log_checkpoint(const TransparencyLogIdentity& identity,
                                                const TransparencyLogCheckpoint& checkpoint) {
    if (!identity.valid() || !checkpoint.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency checkpoint verification input is invalid");
    }
    if (checkpoint.log_id != identity.id || checkpoint.signer_service_id != identity.signer_service_id ||
        checkpoint.signer_key_id != identity.signer_key_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency checkpoint signer or log identity mismatch",
                                     checkpoint.id);
    }
    auto signature = internal::decode_hex(checkpoint.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::transparency_log_checkpoint_message(checkpoint);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          identity.signer_public_key);
}

Result<void> verify_transparency_inclusion(const TransparencyLogIdentity& identity,
                                           const TransparencyLogCheckpoint& checkpoint,
                                           const TransparencyLogLeaf& leaf,
                                           const TransparencyInclusionProof& proof) {
    if (!identity.valid() || !checkpoint.valid() || !leaf.valid() || !proof.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency inclusion verification input is invalid");
    }
    auto checkpoint_verified = verify_transparency_log_checkpoint(identity, checkpoint);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    if (leaf.log_id != identity.id || proof.log_id != identity.id || proof.checkpoint_id != checkpoint.id ||
        proof.leaf_id != leaf.id || proof.leaf_index != leaf.index ||
        proof.tree_size != checkpoint.tree_size || proof.root_hash != checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency inclusion proof binding mismatch", proof.id);
    }
    std::uint64_t index = proof.leaf_index;
    std::uint64_t width = proof.tree_size;
    std::size_t sibling_index = 0;
    std::string hash = internal::transparency_leaf_hash(leaf.id);
    while (width > 1U) {
        if ((index & 1U) != 0U) {
            if (sibling_index >= proof.sibling_hashes.size()) {
                return Result<void>::failure(StatusCode::CorruptData,
                                             "transparency inclusion proof is truncated", proof.id);
            }
            hash = merkle_node_hash(proof.sibling_hashes[sibling_index++], hash);
        } else if (index + 1U < width) {
            if (sibling_index >= proof.sibling_hashes.size()) {
                return Result<void>::failure(StatusCode::CorruptData,
                                             "transparency inclusion proof is truncated", proof.id);
            }
            hash = merkle_node_hash(hash, proof.sibling_hashes[sibling_index++]);
        }
        index /= 2U;
        width = (width + 1U) / 2U;
    }
    if (sibling_index != proof.sibling_hashes.size() || hash != checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency inclusion proof root mismatch", proof.id);
    }
    return Result<void>::success();
}

Result<void> verify_transparency_consistency(const TransparencyLogIdentity& identity,
                                             const TransparencyLogCheckpoint& old_checkpoint,
                                             const TransparencyLogCheckpoint& new_checkpoint,
                                             const TransparencyConsistencyWitness& witness) {
    if (!identity.valid() || !old_checkpoint.valid() || !new_checkpoint.valid() || !witness.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "transparency consistency verification input is invalid");
    }
    auto old_verified = verify_transparency_log_checkpoint(identity, old_checkpoint);
    if (!old_verified)
        return old_verified.error();
    auto new_verified = verify_transparency_log_checkpoint(identity, new_checkpoint);
    if (!new_verified)
        return new_verified.error();
    if (witness.log_id != identity.id || witness.old_checkpoint_id != old_checkpoint.id ||
        witness.new_checkpoint_id != new_checkpoint.id || witness.old_tree_size != old_checkpoint.tree_size ||
        witness.new_tree_size != new_checkpoint.tree_size ||
        witness.old_root_hash != old_checkpoint.root_hash ||
        witness.new_root_hash != new_checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency consistency witness binding mismatch", witness.id);
    }
    const std::vector<std::string> old_leaves(witness.ordered_leaf_ids.begin(),
                                              witness.ordered_leaf_ids.begin() +
                                                  static_cast<std::ptrdiff_t>(witness.old_tree_size));
    if (internal::transparency_merkle_root(old_leaves) != old_checkpoint.root_hash ||
        internal::transparency_merkle_root(witness.ordered_leaf_ids) != new_checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "transparency consistency witness root mismatch", witness.id);
    }
    return Result<void>::success();
}

Result<void> verify_transparency_compact_consistency(const TransparencyLogIdentity& identity,
                                                     const TransparencyLogCheckpoint& old_checkpoint,
                                                     const TransparencyLogCheckpoint& new_checkpoint,
                                                     const TransparencyCompactConsistencyProof& proof) {
    if (!identity.valid() || !old_checkpoint.valid() || !new_checkpoint.valid() || !proof.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "compact transparency consistency verification input is invalid");
    }
    auto old_verified = verify_transparency_log_checkpoint(identity, old_checkpoint);
    if (!old_verified)
        return old_verified.error();
    auto new_verified = verify_transparency_log_checkpoint(identity, new_checkpoint);
    if (!new_verified)
        return new_verified.error();
    if (proof.log_id != identity.id || proof.old_checkpoint_id != old_checkpoint.id ||
        proof.new_checkpoint_id != new_checkpoint.id || proof.old_tree_size != old_checkpoint.tree_size ||
        proof.new_tree_size != new_checkpoint.tree_size || proof.old_root_hash != old_checkpoint.root_hash ||
        proof.new_root_hash != new_checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "compact transparency consistency proof binding mismatch", proof.id);
    }
    std::array<std::string, 64> frontier{};
    for (const auto& subtree : proof.old_frontier)
        frontier[subtree.level] = subtree.hash;
    if (internal::transparency_merkle_frontier_root(frontier) != old_checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "compact transparency consistency old frontier root mismatch", proof.id);
    }
    std::uint64_t represented_size = proof.old_tree_size;
    std::string root = old_checkpoint.root_hash;
    for (const auto& subtree : proof.appended_subtrees) {
        root = internal::transparency_append_merkle_subtree(frontier, subtree.hash, subtree.level,
                                                            represented_size);
        if (root.empty()) {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "compact transparency consistency subtree sequence is invalid",
                                         proof.id);
        }
        represented_size += std::uint64_t{1} << subtree.level;
    }
    if (represented_size != proof.new_tree_size || root != new_checkpoint.root_hash) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "compact transparency consistency new root mismatch", proof.id);
    }
    return Result<void>::success();
}

bool TransparencyLog::valid() const {
    if (directory_.empty() || !identity_.valid() || records_.size() > options_.maximum_records) {
        return false;
    }
    if (records_.empty())
        return current_checkpoint_id_.empty() && current_root_hash_.empty() &&
               std::all_of(merkle_frontier_.begin(), merkle_frontier_.end(),
                           [](const std::string& hash) { return hash.empty(); });
    std::array<std::string, 64> frontier{};
    std::string parent_record;
    std::string previous_checkpoint;
    std::size_t total_attestations = 0;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (!record.valid() || record.sequence != index || record.parent_id != parent_record ||
            record.log_id != identity_.id ||
            record.checkpoint.previous_checkpoint_id != previous_checkpoint ||
            !verify_transparency_log_checkpoint(identity_, record.checkpoint)) {
            return false;
        }
        if (record.leaf.runtime_observation) {
            const auto count = record.leaf.runtime_observation->attestations.size();
            if (count > options_.maximum_attestations_per_observation ||
                total_attestations > options_.maximum_total_attestations ||
                count > options_.maximum_total_attestations - total_attestations) {
                return false;
            }
            total_attestations += count;
        }
        const auto root = internal::transparency_append_merkle_leaf(frontier, record.leaf.id,
                                                                    static_cast<std::uint64_t>(index));
        if (root.empty() || record.checkpoint.root_hash != root) {
            return false;
        }
        parent_record = record.id;
        previous_checkpoint = record.checkpoint.id;
    }
    return current_checkpoint_id_ == records_.back().checkpoint.id &&
           current_root_hash_ == records_.back().checkpoint.root_hash && merkle_frontier_ == frontier;
}

Result<TransparencyLogRecord>
TransparencyLog::publish_deployment_anchor(DeploymentTransparencyAnchor anchor,
                                           std::span<const std::byte> signer_secret_key,
                                           const std::string& expected_current_checkpoint_id) {
    if (!anchor.valid()) {
        return Result<TransparencyLogRecord>::failure(StatusCode::InvalidArgument,
                                                      "deployment transparency anchor is invalid");
    }
    TransparencyLogLeaf leaf;
    leaf.kind = TransparencyLeafKind::DeploymentAnchor;
    leaf.deployment_anchor = std::move(anchor);
    return publish_leaf(std::move(leaf), signer_secret_key, expected_current_checkpoint_id);
}

Result<TransparencyLogRecord>
TransparencyLog::publish_runtime_observation(RuntimeObservationAttestationSet observation,
                                             std::span<const std::byte> signer_secret_key,
                                             const std::string& expected_current_checkpoint_id) {
    if (!observation.valid()) {
        return Result<TransparencyLogRecord>::failure(StatusCode::InvalidArgument,
                                                      "runtime observation attestation set is invalid");
    }
    TransparencyLogLeaf leaf;
    leaf.kind = TransparencyLeafKind::RuntimeObservation;
    leaf.runtime_observation = std::move(observation);
    return publish_leaf(std::move(leaf), signer_secret_key, expected_current_checkpoint_id);
}

Result<TransparencyLogRecord>
TransparencyLog::publish_leaf(TransparencyLogLeaf leaf, std::span<const std::byte> signer_secret_key,
                              const std::string& expected_current_checkpoint_id) {
    if (!valid()) {
        return Result<TransparencyLogRecord>::failure(StatusCode::CorruptData,
                                                      "open transparency log state is invalid");
    }
    if (signer_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::InvalidArgument, "transparency checkpoint signer secret key size is invalid");
    }
    if (expected_current_checkpoint_id != current_checkpoint_id_) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::IdentityMismatch,
            "transparency publication expected checkpoint does not match the open log",
            expected_current_checkpoint_id);
    }
    auto lock = internal::TransparencyLogWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto fresh = TransparencyLog::open(directory_, identity_, current_checkpoint_id_, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_checkpoint_id_ != expected_current_checkpoint_id) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::IdentityMismatch, "transparency log changed after the caller observed its head",
            fresh.value().current_checkpoint_id_);
    }
    if (fresh.value().records_.size() >= options_.maximum_records) {
        return Result<TransparencyLogRecord>::failure(StatusCode::ResourceLimit,
                                                      "transparency log record limit reached");
    }
    leaf.index = static_cast<std::uint64_t>(fresh.value().records_.size());
    leaf.log_id = identity_.id;
    leaf.id = internal::transparency_log_leaf_identity(leaf);
    if (!leaf.valid()) {
        return Result<TransparencyLogRecord>::failure(StatusCode::InvalidArgument,
                                                      "transparency leaf payload is invalid");
    }

    auto next_frontier = fresh.value().merkle_frontier_;
    const auto next_root = internal::transparency_append_merkle_leaf(
        next_frontier, leaf.id, static_cast<std::uint64_t>(fresh.value().records_.size()));
    if (next_root.empty()) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::ResourceLimit, "transparency Merkle frontier cannot represent another leaf");
    }

    TransparencyLogCheckpoint checkpoint;
    checkpoint.log_id = identity_.id;
    checkpoint.tree_size = static_cast<std::uint64_t>(fresh.value().records_.size()) + 1U;
    checkpoint.root_hash = next_root;
    checkpoint.previous_checkpoint_id = fresh.value().current_checkpoint_id_;
    auto signed_checkpoint = sign_checkpoint(identity_, std::move(checkpoint), signer_secret_key);
    if (!signed_checkpoint)
        return signed_checkpoint.error();

    TransparencyLogRecord record;
    record.sequence = leaf.index;
    record.parent_id = fresh.value().records_.empty() ? std::string{} : fresh.value().records_.back().id;
    record.log_id = identity_.id;
    record.leaf = std::move(leaf);
    record.checkpoint = std::move(signed_checkpoint).value();
    record.id = internal::transparency_log_record_identity(record);
    if (!record.valid()) {
        return Result<TransparencyLogRecord>::failure(StatusCode::InternalError,
                                                      "constructed transparency record is invalid");
    }

    TransparencyLog candidate = fresh.value();
    candidate.records_.push_back(record);
    candidate.current_checkpoint_id_ = record.checkpoint.id;
    candidate.current_root_hash_ = record.checkpoint.root_hash;
    candidate.merkle_frontier_ = std::move(next_frontier);
    if (!candidate.valid()) {
        return Result<TransparencyLogRecord>::failure(
            StatusCode::InternalError, "candidate transparency log failed complete validation");
    }
    auto appended =
        internal::append_transparency_log_record_file(directory_, record, options_.maximum_record_bytes);
    if (!appended)
        return appended.error();
    *this = std::move(candidate);
    return record;
}

Result<TransparencyInclusionProof> TransparencyLog::inclusion_proof(std::uint64_t leaf_index) const {
    if (!valid() || leaf_index >= records_.size()) {
        return Result<TransparencyInclusionProof>::failure(StatusCode::InvalidArgument,
                                                           "transparency inclusion leaf index is invalid");
    }
    std::vector<std::string> level;
    level.reserve(records_.size());
    for (const auto& record : records_)
        level.push_back(internal::transparency_leaf_hash(record.leaf.id));
    std::uint64_t index = leaf_index;
    TransparencyInclusionProof proof;
    proof.log_id = identity_.id;
    proof.checkpoint_id = current_checkpoint_id_;
    proof.leaf_id = records_[static_cast<std::size_t>(leaf_index)].leaf.id;
    proof.leaf_index = leaf_index;
    proof.tree_size = static_cast<std::uint64_t>(records_.size());
    proof.root_hash = current_root_hash_;
    while (level.size() > 1U) {
        const auto index_value = static_cast<std::size_t>(index);
        if ((index & 1U) != 0U)
            proof.sibling_hashes.push_back(level[index_value - 1U]);
        else if (index_value + 1U < level.size())
            proof.sibling_hashes.push_back(level[index_value + 1U]);
        std::vector<std::string> next;
        next.reserve((level.size() + 1U) / 2U);
        for (std::size_t level_index = 0; level_index < level.size(); level_index += 2U) {
            if (level_index + 1U < level.size())
                next.push_back(merkle_node_hash(level[level_index], level[level_index + 1U]));
            else
                next.push_back(level[level_index]);
        }
        level = std::move(next);
        index /= 2U;
    }
    proof.id = internal::transparency_inclusion_proof_identity(proof);
    auto verified = verify_transparency_inclusion(identity_, records_.back().checkpoint,
                                                  records_[static_cast<std::size_t>(leaf_index)].leaf, proof);
    if (!verified)
        return verified.error();
    return proof;
}

Result<TransparencyConsistencyWitness>
TransparencyLog::consistency_witness(std::uint64_t old_tree_size) const {
    if (!valid() || old_tree_size == 0 || old_tree_size >= records_.size()) {
        return Result<TransparencyConsistencyWitness>::failure(
            StatusCode::InvalidArgument, "transparency consistency old tree size is invalid");
    }
    TransparencyConsistencyWitness witness;
    witness.log_id = identity_.id;
    witness.old_tree_size = old_tree_size;
    witness.new_tree_size = static_cast<std::uint64_t>(records_.size());
    witness.old_checkpoint_id = records_[static_cast<std::size_t>(old_tree_size - 1U)].checkpoint.id;
    witness.new_checkpoint_id = records_.back().checkpoint.id;
    witness.old_root_hash = records_[static_cast<std::size_t>(old_tree_size - 1U)].checkpoint.root_hash;
    witness.new_root_hash = records_.back().checkpoint.root_hash;
    witness.ordered_leaf_ids.reserve(records_.size());
    for (const auto& record : records_)
        witness.ordered_leaf_ids.push_back(record.leaf.id);
    witness.id = internal::transparency_consistency_witness_identity(witness);
    auto verified = verify_transparency_consistency(
        identity_, records_[static_cast<std::size_t>(old_tree_size - 1U)].checkpoint,
        records_.back().checkpoint, witness);
    if (!verified)
        return verified.error();
    return witness;
}

Result<TransparencyCompactConsistencyProof>
TransparencyLog::compact_consistency_proof(std::uint64_t old_tree_size) const {
    if (!valid() || old_tree_size == 0 || old_tree_size >= records_.size()) {
        return Result<TransparencyCompactConsistencyProof>::failure(
            StatusCode::InvalidArgument, "compact transparency consistency old tree size is invalid");
    }
    TransparencyCompactConsistencyProof proof;
    proof.log_id = identity_.id;
    proof.old_tree_size = old_tree_size;
    proof.new_tree_size = static_cast<std::uint64_t>(records_.size());
    const auto& old_checkpoint = records_[static_cast<std::size_t>(old_tree_size - 1U)].checkpoint;
    const auto& new_checkpoint = records_.back().checkpoint;
    proof.old_checkpoint_id = old_checkpoint.id;
    proof.new_checkpoint_id = new_checkpoint.id;
    proof.old_root_hash = old_checkpoint.root_hash;
    proof.new_root_hash = new_checkpoint.root_hash;

    std::array<std::string, 64> frontier{};
    for (std::uint64_t index = 0; index < old_tree_size; ++index) {
        if (internal::transparency_append_merkle_leaf(
                frontier, records_[static_cast<std::size_t>(index)].leaf.id, index)
                .empty()) {
            return Result<TransparencyCompactConsistencyProof>::failure(
                StatusCode::InternalError, "failed to reconstruct compact consistency old frontier");
        }
    }
    for (std::uint8_t level = 0; level < 64U; ++level) {
        if (!frontier[level].empty())
            proof.old_frontier.push_back({level, frontier[level]});
    }

    std::uint64_t cursor = old_tree_size;
    while (cursor < proof.new_tree_size) {
        const std::uint64_t remaining = proof.new_tree_size - cursor;
        std::uint8_t level = 0;
        while (level < 63U) {
            const std::uint64_t next_size = std::uint64_t{1} << (level + 1U);
            if (next_size > remaining || (cursor & (next_size - 1U)) != 0U)
                break;
            ++level;
        }
        const std::uint64_t subtree_size = std::uint64_t{1} << level;
        std::vector<std::string> leaf_ids;
        leaf_ids.reserve(static_cast<std::size_t>(subtree_size));
        for (std::uint64_t offset = 0; offset < subtree_size; ++offset) {
            leaf_ids.push_back(records_[static_cast<std::size_t>(cursor + offset)].leaf.id);
        }
        const auto subtree_hash = internal::transparency_merkle_root(leaf_ids);
        if (subtree_hash.empty() ||
            internal::transparency_append_merkle_subtree(frontier, subtree_hash, level, cursor).empty()) {
            return Result<TransparencyCompactConsistencyProof>::failure(
                StatusCode::InternalError, "failed to construct compact consistency appended subtree");
        }
        proof.appended_subtrees.push_back({level, subtree_hash});
        cursor += subtree_size;
    }
    proof.id = internal::transparency_compact_consistency_proof_identity(proof);
    auto verified = verify_transparency_compact_consistency(identity_, old_checkpoint, new_checkpoint, proof);
    if (!verified)
        return verified.error();
    return proof;
}

Result<TransparencyLogAuditReport> TransparencyLog::audit() const {
    if (!valid()) {
        return Result<TransparencyLogAuditReport>::failure(StatusCode::CorruptData,
                                                           "transparency log failed complete audit");
    }
    TransparencyLogAuditReport report;
    report.log_id = identity_.id;
    report.current_checkpoint_id = current_checkpoint_id_;
    report.current_root_hash = current_root_hash_;
    report.verified_records = records_.size();
    for (const auto& record : records_) {
        if (record.leaf.kind == TransparencyLeafKind::DeploymentAnchor)
            ++report.deployment_anchor_count;
        else
            ++report.runtime_observation_count;
    }
    report.id = internal::transparency_log_audit_report_identity(report);
    if (!report.valid()) {
        return Result<TransparencyLogAuditReport>::failure(
            StatusCode::InternalError, "constructed transparency audit report is invalid");
    }
    return report;
}

std::string transparency_leaf_kind_name(TransparencyLeafKind kind) {
    switch (kind) {
    case TransparencyLeafKind::DeploymentAnchor:
        return "deployment_anchor";
    case TransparencyLeafKind::RuntimeObservation:
        return "runtime_observation";
    }
    return "unknown";
}

} // namespace rbfsafe
