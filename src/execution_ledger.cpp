#include <rbfsafe/execution_ledger.h>

#include "internal/certificate_utils.h"
#include "internal/execution.h"
#include "internal/execution_ledger.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumTextBytes = 4096;

bool valid_text(std::string_view value, bool allow_empty = false) {
    return (allow_empty || !value.empty()) && value.size() <= kMaximumTextBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_completion_outcome(ExecutionCompletionOutcome outcome) {
    return outcome >= ExecutionCompletionOutcome::Completed &&
           outcome <= ExecutionCompletionOutcome::Rejected;
}

bool valid_dependency_kind(ExecutionDependencyKind kind) {
    return kind >= ExecutionDependencyKind::ReviewedProfile &&
           kind <= ExecutionDependencyKind::TrustCheckpoint;
}

bool valid_record_type(ExecutionLedgerRecordType type) {
    return type >= ExecutionLedgerRecordType::SessionOpened &&
           type <= ExecutionLedgerRecordType::DependencyRevoked;
}

bool valid_ledger_status(ExecutionLedgerStatus status) {
    return status >= ExecutionLedgerStatus::Open && status <= ExecutionLedgerStatus::Failed;
}

bool terminal_status(ExecutionLedgerStatus status) {
    return status == ExecutionLedgerStatus::Completed || status == ExecutionLedgerStatus::Cancelled ||
           status == ExecutionLedgerStatus::Expired || status == ExecutionLedgerStatus::Revoked ||
           status == ExecutionLedgerStatus::Failed;
}

Result<Ed25519KeyPair> verified_controller_key_pair(const BoundedExecutionSession& session,
                                                    std::span<const std::byte> secret_key) {
    if (!session.valid() || secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<Ed25519KeyPair>::failure(StatusCode::InvalidArgument,
                                               "execution completion signing input is invalid");
    }
    auto pair = ed25519_key_pair_from_seed(secret_key.first(kEd25519SeedBytes));
    if (!pair)
        return pair.error();
    if (!std::equal(pair.value().secret_key.begin(), pair.value().secret_key.end(), secret_key.begin())) {
        return Result<Ed25519KeyPair>::failure(
            StatusCode::IdentityMismatch, "execution controller secret-key seed and public half do not match",
            session.request().controller.id);
    }
    auto endpoint = make_execution_endpoint_key(session.request().controller.service_id,
                                                ExecutionEndpointRole::Controller, pair.value().public_key);
    if (!endpoint)
        return endpoint.error();
    if (endpoint.value().id != session.request().controller.id) {
        return Result<Ed25519KeyPair>::failure(
            StatusCode::IdentityMismatch,
            "execution controller secret key does not match the session endpoint",
            session.request().controller.id);
    }
    return pair;
}

Result<void> verify_historical_checkpoint(const ServiceTrustHistory& history,
                                          const ServiceTrustCheckpoint& checkpoint) {
    if (!history.valid() || !checkpoint.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution-ledger checkpoint input is invalid");
    }
    if (checkpoint.root_bundle_id != history.root_bundle_id() || checkpoint.head_sequence == 0 ||
        checkpoint.head_sequence > history.records().size()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution-ledger checkpoint is outside the supplied trust history",
                                     checkpoint.id);
    }
    const auto& head_record = history.records()[static_cast<std::size_t>(checkpoint.head_sequence - 1)];
    if (head_record.sequence != checkpoint.head_sequence || head_record.id != checkpoint.head_record_id ||
        head_record.bundle_id != checkpoint.head_bundle_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution-ledger checkpoint does not match its historical trust record",
                                     checkpoint.id);
    }
    auto head_bundle = history.bundle(checkpoint.head_bundle_id);
    if (!head_bundle)
        return head_bundle.error();
    if (head_bundle.value().sequence() != checkpoint.head_sequence) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution-ledger checkpoint sequence does not match its trust bundle",
                                     checkpoint.id);
    }
    std::set<std::string> services;
    for (const auto& signature : checkpoint.signatures) {
        auto key = head_bundle.value().key(signature.signer_service_id, signature.signer_key_id);
        if (!key)
            return key.error();
        const bool sequence_authorized = key.value() &&
                                         checkpoint.head_sequence >= key.value()->valid_from_sequence &&
                                         (key.value()->valid_through_sequence == 0 ||
                                          checkpoint.head_sequence <= key.value()->valid_through_sequence);
        if (!key.value() || key.value()->state != ServiceKeyState::Active || !key.value()->allow_rotate ||
            !sequence_authorized) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "execution-ledger checkpoint signer is not active at the historical head",
                signature.signer_key_id);
        }
        auto decoded = internal::decode_hex(signature.authentication_tag, kEd25519SignatureBytes);
        if (!decoded)
            return decoded.error();
        const auto message = internal::service_trust_checkpoint_signature_message(checkpoint, signature);
        auto verified = ed25519_verify(std::as_bytes(std::span(message.data(), message.size())),
                                       decoded.value(), key.value()->public_key);
        if (!verified)
            return verified;
        services.insert(signature.signer_service_id);
    }
    const auto& policy = head_bundle.value().rotation_policy();
    if (checkpoint.signatures.size() < policy.minimum_signatures ||
        (policy.require_distinct_services && services.size() < policy.minimum_signatures)) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "execution-ledger checkpoint signatures do not satisfy the historical policy", checkpoint.id);
    }
    return Result<void>::success();
}

Result<ServiceTrustBundle> verify_bound_session(const BoundedExecutionSession& session,
                                                const ReviewedDeploymentProfile& reviewed,
                                                const ServiceTrustHistory& trust_history,
                                                const SafeAtlas& atlas) {
    if (!session.valid() || !reviewed.valid() || !trust_history.valid() || atlas.dimension() == 0) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "execution-ledger dependency input is invalid");
    }
    if (session.request().reviewed_profile_id != reviewed.profile().id ||
        session.request().reviewed_profile_approval_set_id != reviewed.approval_set().id ||
        session.request().trust_root_bundle_id != trust_history.root_bundle_id() ||
        session.request().atlas_id != atlas.version_info().id ||
        session.request().robot_digest != atlas.robot_digest() ||
        session.request().scene_digest != atlas.scene_digest()) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::IdentityMismatch, "execution ledger dependencies do not match the bounded session",
            session.id());
    }
    auto historical_bundle = trust_history.bundle(session.request().trust_bundle_id);
    if (!historical_bundle)
        return historical_bundle.error();
    if (historical_bundle.value().sequence() != session.request().trust_bundle_sequence) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::IdentityMismatch, "bounded session historical trust sequence does not replay",
            session.id());
    }
    auto recreated = BoundedExecutionSession::create(
        session.request(), session.command_sequence(), session.approval_set(),
        session.controller_acknowledgement(), session.monitor_acknowledgement(), reviewed,
        historical_bundle.value(), atlas);
    if (!recreated)
        return recreated.error();
    if (recreated.value().id() != session.id()) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::IdentityMismatch, "bounded session does not recreate under the supplied dependencies",
            session.id());
    }
    return historical_bundle;
}

Result<ServiceTrustBundle>
verify_current_context(const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
                       const ServiceTrustHistory& history, const ServiceTrustCheckpoint& checkpoint,
                       const std::string& expected_checkpoint_id, const SafeAtlas& atlas) {
    auto historical = verify_bound_session(session, reviewed, history, atlas);
    if (!historical)
        return historical.error();
    auto checkpoint_status = verify_service_trust_checkpoint(history, checkpoint, expected_checkpoint_id);
    if (!checkpoint_status)
        return checkpoint_status.error();
    if (checkpoint.root_bundle_id != session.request().trust_root_bundle_id ||
        checkpoint.head_sequence < session.request().trust_bundle_sequence) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::IdentityMismatch,
            "current trust checkpoint predates or leaves the bounded-session trust root", checkpoint.id);
    }
    return history.current_bundle();
}

Result<std::optional<std::string>> inactive_reviewer_key(const BoundedExecutionSession& session,
                                                         const ServiceTrustBundle& current_bundle) {
    if (!session.valid() || !current_bundle.valid()) {
        return Result<std::optional<std::string>>::failure(StatusCode::InvalidArgument,
                                                           "execution reviewer-state input is invalid");
    }
    for (const auto& approval : session.approval_set().approvals) {
        auto key = current_bundle.key(approval.signer_service_id, approval.signer_key_id);
        if (!key)
            return key.error();
        const bool sequence_valid = key.value() &&
                                    current_bundle.sequence() >= key.value()->valid_from_sequence &&
                                    (key.value()->valid_through_sequence == 0 ||
                                     current_bundle.sequence() <= key.value()->valid_through_sequence);
        if (!key.value() || key.value()->state != ServiceKeyState::Active || !key.value()->allow_publish ||
            !sequence_valid) {
            return std::optional<std::string>{approval.signer_key_id};
        }
    }
    return std::optional<std::string>{};
}

bool dependency_matches_session(const BoundedExecutionSession& session, ExecutionDependencyKind kind,
                                std::string_view subject_id,
                                const std::vector<ExecutionLedgerRecord>& records) {
    switch (kind) {
    case ExecutionDependencyKind::ReviewedProfile:
        return subject_id == session.request().reviewed_profile_id;
    case ExecutionDependencyKind::Atlas:
        return subject_id == session.request().atlas_id;
    case ExecutionDependencyKind::Scene:
        return subject_id == session.request().scene_digest;
    case ExecutionDependencyKind::ControllerKey:
        return subject_id == session.request().controller.id;
    case ExecutionDependencyKind::RuntimeMonitorKey:
        return subject_id == session.request().runtime_monitor.id;
    case ExecutionDependencyKind::ReviewerKey:
        return std::any_of(session.approval_set().approvals.begin(), session.approval_set().approvals.end(),
                           [&](const auto& approval) { return approval.signer_key_id == subject_id; });
    case ExecutionDependencyKind::TrustCheckpoint:
        if (subject_id == session.request().trust_checkpoint_id)
            return true;
        return std::any_of(records.begin(), records.end(), [&](const auto& record) {
            return record.trust_checkpoint && record.trust_checkpoint->id == subject_id;
        });
    }
    return false;
}

Result<ExecutionLedgerSummary> build_summary(std::string_view ledger_id, std::string_view session_id,
                                             std::string_view current_record_id,
                                             const std::vector<ExecutionLedgerRecord>& records,
                                             std::size_t command_count,
                                             std::uint64_t valid_through_monotonic_ns) {
    if (!internal::valid_sha256(std::string(ledger_id)) || !internal::valid_sha256(std::string(session_id)) ||
        !internal::valid_sha256(std::string(current_record_id)) || records.empty() || command_count < 2 ||
        valid_through_monotonic_ns == 0) {
        return Result<ExecutionLedgerSummary>::failure(StatusCode::CorruptData,
                                                       "execution-ledger summary input is invalid");
    }
    ExecutionLedgerSummary result;
    result.ledger_id = std::string(ledger_id);
    result.session_id = std::string(session_id);
    result.current_record_id = std::string(current_record_id);
    result.record_count = records.size();
    result.status = ExecutionLedgerStatus::Open;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        if (!record.valid() || record.sequence != index || record.ledger_id != ledger_id ||
            record.session_id != session_id ||
            (index == 0 &&
             (record.type != ExecutionLedgerRecordType::SessionOpened || !record.parent_id.empty())) ||
            (index > 0 && record.parent_id != records[index - 1].id)) {
            return Result<ExecutionLedgerSummary>::failure(
                StatusCode::CorruptData, "execution-ledger record chain is invalid", record.id);
        }
        if (index == 0)
            continue;
        if (record.observed_monotonic_ns < records[index - 1].observed_monotonic_ns) {
            return Result<ExecutionLedgerSummary>::failure(
                StatusCode::CorruptData, "execution-ledger monotonic observation time regressed", record.id);
        }
        if (terminal_status(result.status)) {
            return Result<ExecutionLedgerSummary>::failure(
                StatusCode::CorruptData, "execution-ledger record appears after a terminal event", record.id);
        }
        switch (record.type) {
        case ExecutionLedgerRecordType::SessionOpened:
            return Result<ExecutionLedgerSummary>::failure(
                StatusCode::CorruptData, "execution-ledger contains a second session-open record", record.id);
        case ExecutionLedgerRecordType::CommandAuthorized:
            if (result.status != ExecutionLedgerStatus::Open || !record.authorization ||
                record.authorization->command_index != result.next_command_index ||
                record.authorization->command_index >= command_count) {
                return Result<ExecutionLedgerSummary>::failure(
                    StatusCode::CorruptData, "execution-ledger command authorization order is invalid",
                    record.id);
            }
            ++result.authorization_count;
            result.outstanding_command_index = record.authorization->command_index;
            result.status = ExecutionLedgerStatus::AwaitingCompletion;
            break;
        case ExecutionLedgerRecordType::ControllerCompletion: {
            if (result.status != ExecutionLedgerStatus::AwaitingCompletion || !record.completion ||
                !result.outstanding_command_index ||
                record.completion->command_index != *result.outstanding_command_index) {
                return Result<ExecutionLedgerSummary>::failure(
                    StatusCode::CorruptData, "execution-ledger controller-completion order is invalid",
                    record.id);
            }
            const auto& authorization = records[index - 1].authorization;
            if (!authorization || record.completion->authorization_id != authorization->id) {
                return Result<ExecutionLedgerSummary>::failure(
                    StatusCode::CorruptData, "execution-ledger completion does not match its authorization",
                    record.id);
            }
            ++result.completion_count;
            result.outstanding_command_index.reset();
            if (record.completion->outcome != ExecutionCompletionOutcome::Completed) {
                result.status = ExecutionLedgerStatus::Failed;
                break;
            }
            ++result.next_command_index;
            if (record.completion->completed_monotonic_ns > valid_through_monotonic_ns) {
                result.status = ExecutionLedgerStatus::Expired;
            } else if (result.next_command_index == command_count) {
                result.status = ExecutionLedgerStatus::Completed;
            } else {
                result.status = ExecutionLedgerStatus::Open;
            }
            break;
        }
        case ExecutionLedgerRecordType::SessionCancelled:
            result.outstanding_command_index.reset();
            result.status = ExecutionLedgerStatus::Cancelled;
            break;
        case ExecutionLedgerRecordType::SessionExpired:
            if (record.observed_monotonic_ns <= valid_through_monotonic_ns) {
                return Result<ExecutionLedgerSummary>::failure(
                    StatusCode::CorruptData, "execution-ledger expiration precedes the closed session end",
                    record.id);
            }
            result.outstanding_command_index.reset();
            result.status = ExecutionLedgerStatus::Expired;
            break;
        case ExecutionLedgerRecordType::DependencyRevoked:
            result.outstanding_command_index.reset();
            result.status = ExecutionLedgerStatus::Revoked;
            break;
        }
    }
    result.id = internal::execution_ledger_summary_identity(result);
    return result;
}

ExecutionLedgerCommandDecision make_decision(const ExecutionLedger& ledger,
                                             std::optional<ExecutionCommandAuthorization> authorization) {
    ExecutionLedgerCommandDecision result;
    result.ledger_id = ledger.id();
    result.current_record_id = ledger.current_record_id();
    result.status = ledger.summary().status;
    result.authorization = std::move(authorization);
    result.id = internal::execution_ledger_command_decision_identity(result);
    return result;
}

} // namespace

namespace internal {
namespace {

Json completion_payload(const ExecutionControllerCompletion& completion) {
    return Json::Object{
        {"algorithm", static_cast<int>(completion.algorithm)},
        {"authorization_id", completion.authorization_id},
        {"command_digest", completion.command_digest},
        {"command_index", std::to_string(completion.command_index)},
        {"command_sequence_id", completion.command_sequence_id},
        {"completed_monotonic_ns", std::to_string(completion.completed_monotonic_ns)},
        {"controller_key_id", completion.controller_key_id},
        {"controller_service_id", completion.controller_service_id},
        {"format", "rbfsafe-execution-controller-completion"},
        {"outcome", static_cast<int>(completion.outcome)},
        {"result_digest", completion.result_digest},
        {"schema", static_cast<double>(completion.storage_schema)},
        {"session_id", completion.session_id},
    };
}

Json revocation_payload(const ExecutionDependencyRevocation& revocation) {
    return Json::Object{
        {"detail", revocation.detail},
        {"kind", static_cast<int>(revocation.kind)},
        {"subject_id", revocation.subject_id},
    };
}

} // namespace

std::string execution_controller_completion_message(const ExecutionControllerCompletion& completion) {
    return std::string("rbfsafe-execution-controller-completion-v1\n") +
           completion_payload(completion).dump(false);
}

std::string execution_controller_completion_identity(const ExecutionControllerCompletion& completion) {
    return sha256(std::string("rbfsafe-execution-controller-completion-identity-v1\n") +
                  execution_controller_completion_message(completion) + "\n" + completion.authentication_tag);
}

std::string execution_ledger_identity(const std::string& session_id) {
    return sha256(Json(Json::Object{
                           {"format", "rbfsafe-execution-ledger"},
                           {"schema", 1},
                           {"session_id", session_id},
                       })
                      .dump(false));
}

std::string execution_ledger_record_identity(const ExecutionLedgerRecord& record) {
    return sha256(
        Json(Json::Object{
                 {"authorization_id", record.authorization ? record.authorization->id : ""},
                 {"completion_id", record.completion ? record.completion->id : ""},
                 {"detail", record.detail},
                 {"format", "rbfsafe-execution-ledger-record"},
                 {"ledger_id", record.ledger_id},
                 {"observed_monotonic_ns", std::to_string(record.observed_monotonic_ns)},
                 {"parent_id", record.parent_id},
                 {"revocation",
                  record.revocation ? revocation_payload(*record.revocation) : Json(Json::Object{})},
                 {"schema", static_cast<double>(record.storage_schema)},
                 {"sequence", std::to_string(record.sequence)},
                 {"session_id", record.session_id},
                 {"trust_checkpoint_id", record.trust_checkpoint ? record.trust_checkpoint->id : ""},
                 {"type", static_cast<int>(record.type)},
             })
            .dump(false));
}

std::string execution_ledger_summary_identity(const ExecutionLedgerSummary& summary) {
    return sha256(
        Json(
            Json::Object{
                {"authorization_count", static_cast<double>(summary.authorization_count)},
                {"completion_count", static_cast<double>(summary.completion_count)},
                {"current_record_id", summary.current_record_id},
                {"format", "rbfsafe-execution-ledger-summary"},
                {"ledger_id", summary.ledger_id},
                {"next_command_index", static_cast<double>(summary.next_command_index)},
                {"outstanding_command_index",
                 summary.outstanding_command_index ? std::to_string(*summary.outstanding_command_index) : ""},
                {"record_count", static_cast<double>(summary.record_count)},
                {"schema", 1},
                {"session_id", summary.session_id},
                {"status", static_cast<int>(summary.status)},
            })
            .dump(false));
}

std::string execution_ledger_command_decision_identity(const ExecutionLedgerCommandDecision& decision) {
    return sha256(Json(Json::Object{
                           {"authorization_id", decision.authorization ? decision.authorization->id : ""},
                           {"current_record_id", decision.current_record_id},
                           {"format", "rbfsafe-execution-ledger-command-decision"},
                           {"ledger_id", decision.ledger_id},
                           {"schema", 1},
                           {"status", static_cast<int>(decision.status)},
                       })
                      .dump(false));
}

std::string execution_ledger_audit_report_identity(const ExecutionLedgerAuditReport& report) {
    return sha256(Json(Json::Object{
                           {"authorization_count", static_cast<double>(report.authorization_count)},
                           {"completion_count", static_cast<double>(report.completion_count)},
                           {"current_record_id", report.current_record_id},
                           {"format", "rbfsafe-execution-ledger-audit"},
                           {"latest_checkpoint_id", report.latest_checkpoint_id},
                           {"ledger_id", report.ledger_id},
                           {"schema", 1},
                           {"session_id", report.session_id},
                           {"status", static_cast<int>(report.status)},
                           {"verified_checkpoints", static_cast<double>(report.verified_checkpoints)},
                           {"verified_records", static_cast<double>(report.verified_records)},
                       })
                      .dump(false));
}

} // namespace internal

bool ExecutionControllerCompletion::valid() const {
    return storage_schema == 1 && internal::valid_sha256(id) && internal::valid_sha256(session_id) &&
           internal::valid_sha256(authorization_id) && internal::valid_sha256(command_sequence_id) &&
           internal::valid_sha256(command_digest) && valid_text(controller_service_id) &&
           internal::valid_sha256(controller_key_id) && valid_completion_outcome(outcome) &&
           completed_monotonic_ns > 0 && internal::valid_sha256(result_digest) &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           internal::decode_hex(authentication_tag, kEd25519SignatureBytes) &&
           id == internal::execution_controller_completion_identity(*this);
}

Result<ExecutionControllerCompletion> sign_execution_controller_completion(
    const BoundedExecutionSession& session, const ExecutionCommandAuthorization& authorization,
    ExecutionControllerCompletionInput input, std::span<const std::byte> ed25519_secret_key) {
    if (!session.valid() || !authorization.valid() || authorization.session_id != session.id() ||
        authorization.command_sequence_id != session.command_sequence().id ||
        authorization.command_index >= session.command_sequence().commands.size() ||
        authorization.command_digest !=
            internal::execution_command_digest(
                session.command_sequence().commands[authorization.command_index]) ||
        !valid_completion_outcome(input.outcome) ||
        input.completed_monotonic_ns < authorization.dispatch_monotonic_ns ||
        !internal::valid_sha256(input.result_digest)) {
        return Result<ExecutionControllerCompletion>::failure(
            StatusCode::InvalidArgument, "execution controller-completion input is invalid");
    }
    auto pair = verified_controller_key_pair(session, ed25519_secret_key);
    if (!pair)
        return pair.error();
    ExecutionControllerCompletion result;
    result.session_id = session.id();
    result.authorization_id = authorization.id;
    result.command_sequence_id = authorization.command_sequence_id;
    result.command_index = authorization.command_index;
    result.command_digest = authorization.command_digest;
    result.controller_service_id = session.request().controller.service_id;
    result.controller_key_id = session.request().controller.id;
    result.outcome = input.outcome;
    result.completed_monotonic_ns = input.completed_monotonic_ns;
    result.result_digest = std::move(input.result_digest);
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::execution_controller_completion_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::execution_controller_completion_identity(result);
    return result;
}

Result<void> verify_execution_controller_completion(const BoundedExecutionSession& session,
                                                    const ExecutionCommandAuthorization& authorization,
                                                    const ExecutionControllerCompletion& completion) {
    if (!session.valid() || !authorization.valid() || !completion.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution controller-completion verification input is invalid");
    }
    if (completion.session_id != session.id() || completion.authorization_id != authorization.id ||
        completion.command_sequence_id != authorization.command_sequence_id ||
        completion.command_index != authorization.command_index ||
        completion.command_digest != authorization.command_digest ||
        completion.controller_service_id != session.request().controller.service_id ||
        completion.controller_key_id != session.request().controller.id ||
        completion.completed_monotonic_ns < authorization.dispatch_monotonic_ns) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution controller completion does not match the exact authorization",
                                     completion.id);
    }
    auto signature = internal::decode_hex(completion.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::execution_controller_completion_message(completion);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          session.request().controller.public_key);
}

bool valid_execution_dependency_revocation(const ExecutionDependencyRevocation& revocation) {
    return valid_dependency_kind(revocation.kind) && internal::valid_sha256(revocation.subject_id) &&
           valid_text(revocation.detail);
}

bool ExecutionLedgerRecord::valid() const {
    if (storage_schema != 1 || !internal::valid_sha256(id) || !internal::valid_sha256(ledger_id) ||
        !internal::valid_sha256(session_id) || !valid_record_type(type) || observed_monotonic_ns == 0 ||
        (sequence == 0 ? !parent_id.empty() : !internal::valid_sha256(parent_id)) ||
        !valid_text(detail, true)) {
        return false;
    }
    switch (type) {
    case ExecutionLedgerRecordType::SessionOpened:
        if (sequence != 0 || authorization || completion || trust_checkpoint || revocation || !detail.empty())
            return false;
        break;
    case ExecutionLedgerRecordType::CommandAuthorized:
        if (sequence == 0 || !authorization || completion || !trust_checkpoint || revocation ||
            !detail.empty() || !authorization->valid() || !trust_checkpoint->valid() ||
            observed_monotonic_ns != authorization->dispatch_monotonic_ns)
            return false;
        break;
    case ExecutionLedgerRecordType::ControllerCompletion:
        if (sequence == 0 || authorization || !completion || trust_checkpoint || revocation ||
            !detail.empty() || !completion->valid() ||
            observed_monotonic_ns != completion->completed_monotonic_ns)
            return false;
        break;
    case ExecutionLedgerRecordType::SessionCancelled:
        if (sequence == 0 || authorization || completion || trust_checkpoint || revocation ||
            !valid_text(detail))
            return false;
        break;
    case ExecutionLedgerRecordType::SessionExpired:
        if (sequence == 0 || authorization || completion || trust_checkpoint || revocation || !detail.empty())
            return false;
        break;
    case ExecutionLedgerRecordType::DependencyRevoked:
        if (sequence == 0 || authorization || completion || !revocation || !detail.empty() ||
            !valid_execution_dependency_revocation(*revocation) ||
            (trust_checkpoint && !trust_checkpoint->valid()))
            return false;
        break;
    }
    return id == internal::execution_ledger_record_identity(*this);
}

bool ExecutionLedgerSummary::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(ledger_id) &&
           internal::valid_sha256(session_id) && internal::valid_sha256(current_record_id) &&
           valid_ledger_status(status) && record_count > 0 && completion_count <= authorization_count &&
           next_command_index <= completion_count && completion_count - next_command_index <= 1 &&
           ((status == ExecutionLedgerStatus::AwaitingCompletion) == outstanding_command_index.has_value()) &&
           (!outstanding_command_index || *outstanding_command_index == next_command_index) &&
           id == internal::execution_ledger_summary_identity(*this);
}

bool ExecutionLedgerCommandDecision::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(ledger_id) &&
           internal::valid_sha256(current_record_id) && valid_ledger_status(status) &&
           (!authorization ||
            (authorization->valid() && status == ExecutionLedgerStatus::AwaitingCompletion)) &&
           id == internal::execution_ledger_command_decision_identity(*this);
}

bool ExecutionLedgerAuditReport::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(ledger_id) &&
           internal::valid_sha256(session_id) && internal::valid_sha256(current_record_id) &&
           valid_ledger_status(status) && verified_records > 0 && completion_count <= authorization_count &&
           (verified_checkpoints == 0 ? latest_checkpoint_id.empty()
                                      : internal::valid_sha256(latest_checkpoint_id)) &&
           id == internal::execution_ledger_audit_report_identity(*this);
}

bool ExecutionLedger::valid() const {
    if (directory_.empty() || !internal::valid_sha256(id_) || !internal::valid_sha256(session_id_) ||
        !internal::valid_sha256(current_record_id_) || command_count_ < 2 || valid_from_monotonic_ns_ == 0 ||
        valid_from_monotonic_ns_ > valid_through_monotonic_ns_ || records_.empty() ||
        records_.size() > options_.maximum_records ||
        records_.front().observed_monotonic_ns != valid_from_monotonic_ns_ ||
        id_ != internal::execution_ledger_identity(session_id_) || current_record_id_ != records_.back().id) {
        return false;
    }
    auto replayed = build_summary(id_, session_id_, current_record_id_, records_, command_count_,
                                  valid_through_monotonic_ns_);
    return replayed && replayed.value().valid();
}

ExecutionLedgerSummary ExecutionLedger::summary() const {
    auto replayed = build_summary(id_, session_id_, current_record_id_, records_, command_count_,
                                  valid_through_monotonic_ns_);
    return replayed ? std::move(replayed).value() : ExecutionLedgerSummary{};
}

Result<ExecutionLedgerAuditReport> ExecutionLedger::audit(const BoundedExecutionSession& session,
                                                          const ReviewedDeploymentProfile& reviewed,
                                                          const ServiceTrustHistory& trust_history,
                                                          const SafeAtlas& atlas) const {
    if (!valid()) {
        return Result<ExecutionLedgerAuditReport>::failure(StatusCode::CorruptData,
                                                           "execution ledger is structurally invalid");
    }
    if (session.id() != session_id_ || session.command_sequence().commands.size() != command_count_ ||
        session.valid_from_monotonic_ns() != valid_from_monotonic_ns_ ||
        session.valid_through_monotonic_ns() != valid_through_monotonic_ns_) {
        return Result<ExecutionLedgerAuditReport>::failure(
            StatusCode::IdentityMismatch, "execution ledger belongs to another bounded session", id_);
    }
    auto historical = verify_bound_session(session, reviewed, trust_history, atlas);
    if (!historical)
        return historical.error();
    std::optional<ExecutionCommandAuthorization> outstanding;
    std::uint64_t last_checkpoint_sequence = 0;
    std::string last_checkpoint_id;
    std::size_t checkpoints = 0;
    for (const auto& record : records_) {
        if (record.authorization) {
            auto replayed = session.authorize_command(
                record.authorization->command_index,
                session.command_sequence().commands[record.authorization->command_index].configuration,
                record.authorization->dispatch_monotonic_ns);
            if (!replayed || !replayed.value() || replayed.value()->id != record.authorization->id) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::IdentityMismatch, "execution-ledger authorization does not replay exactly",
                    record.id);
            }
            auto checkpoint = verify_historical_checkpoint(trust_history, *record.trust_checkpoint);
            if (!checkpoint)
                return checkpoint.error();
            if (record.trust_checkpoint->head_sequence < last_checkpoint_sequence ||
                (record.trust_checkpoint->head_sequence == last_checkpoint_sequence &&
                 last_checkpoint_sequence > 0 && record.trust_checkpoint->id != last_checkpoint_id)) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::IdentityMismatch,
                    "execution-ledger checkpoint sequence rolled back or forked",
                    record.trust_checkpoint->id);
            }
            auto bundle = trust_history.bundle(record.trust_checkpoint->head_bundle_id);
            if (!bundle)
                return bundle.error();
            auto inactive = inactive_reviewer_key(session, bundle.value());
            if (!inactive)
                return inactive.error();
            if (inactive.value()) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::IdentityMismatch,
                    "execution command was authorized after a reviewer key became inactive",
                    *inactive.value());
            }
            last_checkpoint_sequence = record.trust_checkpoint->head_sequence;
            last_checkpoint_id = record.trust_checkpoint->id;
            ++checkpoints;
            outstanding = *record.authorization;
        } else if (record.completion) {
            if (!outstanding) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::CorruptData, "execution completion has no outstanding authorization",
                    record.id);
            }
            auto verified = verify_execution_controller_completion(session, *outstanding, *record.completion);
            if (!verified)
                return verified.error();
            outstanding.reset();
        } else if (record.revocation && record.trust_checkpoint) {
            auto checkpoint = verify_historical_checkpoint(trust_history, *record.trust_checkpoint);
            if (!checkpoint)
                return checkpoint.error();
            if (record.trust_checkpoint->head_sequence < last_checkpoint_sequence ||
                (record.trust_checkpoint->head_sequence == last_checkpoint_sequence &&
                 last_checkpoint_sequence > 0 && record.trust_checkpoint->id != last_checkpoint_id)) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::IdentityMismatch,
                    "execution-ledger checkpoint sequence rolled back or forked",
                    record.trust_checkpoint->id);
            }
            auto bundle = trust_history.bundle(record.trust_checkpoint->head_bundle_id);
            if (!bundle)
                return bundle.error();
            auto inactive = inactive_reviewer_key(session, bundle.value());
            if (!inactive)
                return inactive.error();
            if (record.revocation->kind != ExecutionDependencyKind::ReviewerKey || !inactive.value() ||
                *inactive.value() != record.revocation->subject_id) {
                return Result<ExecutionLedgerAuditReport>::failure(
                    StatusCode::CorruptData,
                    "checkpoint-backed revocation does not match an inactive reviewer", record.id);
            }
            last_checkpoint_sequence = record.trust_checkpoint->head_sequence;
            last_checkpoint_id = record.trust_checkpoint->id;
            ++checkpoints;
        }
        if (record.revocation && !dependency_matches_session(session, record.revocation->kind,
                                                             record.revocation->subject_id, records_)) {
            return Result<ExecutionLedgerAuditReport>::failure(
                StatusCode::IdentityMismatch,
                "execution-ledger revocation does not name a session dependency",
                record.revocation->subject_id);
        }
    }
    const auto replayed_summary = summary();
    ExecutionLedgerAuditReport result;
    result.ledger_id = id_;
    result.session_id = session_id_;
    result.current_record_id = current_record_id_;
    result.status = replayed_summary.status;
    result.verified_records = records_.size();
    result.verified_checkpoints = checkpoints;
    result.authorization_count = replayed_summary.authorization_count;
    result.completion_count = replayed_summary.completion_count;
    result.latest_checkpoint_id = std::move(last_checkpoint_id);
    result.id = internal::execution_ledger_audit_report_identity(result);
    if (!result.valid()) {
        return Result<ExecutionLedgerAuditReport>::failure(
            StatusCode::InternalError, "constructed execution-ledger audit report is invalid");
    }
    return result;
}

Result<ExecutionLedgerRecord> ExecutionLedger::append_record_unlocked(ExecutionLedger fresh,
                                                                      ExecutionLedgerRecord record) {
    if (!fresh.valid() || record.sequence != fresh.records_.size() ||
        record.parent_id != fresh.current_record_id_ || record.ledger_id != fresh.id_ ||
        record.session_id != fresh.session_id_ || !record.valid()) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InternalError,
                                                      "execution-ledger append record is invalid", record.id);
    }
    if (fresh.records_.size() >= fresh.options_.maximum_records) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::ResourceLimit,
                                                      "execution-ledger record limit reached");
    }
    ExecutionLedger candidate = fresh;
    candidate.records_.push_back(record);
    candidate.current_record_id_ = record.id;
    if (!candidate.valid()) {
        return Result<ExecutionLedgerRecord>::failure(
            StatusCode::InternalError, "execution ledger would become invalid after append", record.id);
    }
    auto written = internal::append_execution_ledger_record_file(fresh.directory_, record);
    if (!written)
        return written.error();
    *this = std::move(candidate);
    return record;
}

Result<ExecutionLedgerCommandDecision> ExecutionLedger::authorize_command(
    const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
    const ServiceTrustHistory& current_trust_history, const ServiceTrustCheckpoint& current_trust_checkpoint,
    const std::string& expected_current_checkpoint_id, const SafeAtlas& atlas, std::uint64_t command_index,
    std::span<const double> configuration, std::uint64_t dispatch_monotonic_ns,
    const std::string& expected_current_record_id) {
    if (!internal::valid_sha256(expected_current_record_id) ||
        !internal::valid_sha256(expected_current_checkpoint_id) || dispatch_monotonic_ns == 0) {
        return Result<ExecutionLedgerCommandDecision>::failure(
            StatusCode::InvalidArgument, "execution-ledger authorization input is invalid");
    }
    auto lock = internal::ExecutionLedgerWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ExecutionLedger::open(directory_, session, reviewed, current_trust_history, atlas, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_current_record_id) {
        return Result<ExecutionLedgerCommandDecision>::failure(StatusCode::IdentityMismatch,
                                                               "execution-ledger current record changed",
                                                               fresh.value().current_record_id_);
    }
    const auto before = fresh.value().summary();
    if (dispatch_monotonic_ns < fresh.value().records_.back().observed_monotonic_ns) {
        return Result<ExecutionLedgerCommandDecision>::failure(
            StatusCode::InvalidArgument, "execution-ledger authorization time regresses",
            fresh.value().current_record_id_);
    }
    if (terminal_status(before.status)) {
        return Result<ExecutionLedgerCommandDecision>::failure(StatusCode::IdentityMismatch,
                                                               "execution ledger is already terminal",
                                                               fresh.value().current_record_id_);
    }
    if (dispatch_monotonic_ns > session.valid_through_monotonic_ns()) {
        ExecutionLedgerRecord record;
        record.sequence = fresh.value().records_.size();
        record.parent_id = fresh.value().current_record_id_;
        record.ledger_id = fresh.value().id_;
        record.session_id = fresh.value().session_id_;
        record.type = ExecutionLedgerRecordType::SessionExpired;
        record.observed_monotonic_ns = dispatch_monotonic_ns;
        record.id = internal::execution_ledger_record_identity(record);
        auto appended = append_record_unlocked(std::move(fresh).value(), std::move(record));
        if (!appended)
            return appended.error();
        auto decision = make_decision(*this, std::nullopt);
        return decision;
    }
    auto current_bundle =
        verify_current_context(session, reviewed, current_trust_history, current_trust_checkpoint,
                               expected_current_checkpoint_id, atlas);
    if (!current_bundle)
        return current_bundle.error();
    std::uint64_t previous_checkpoint_sequence = 0;
    std::string previous_checkpoint_id;
    for (auto iterator = fresh.value().records_.rbegin(); iterator != fresh.value().records_.rend();
         ++iterator) {
        if (iterator->trust_checkpoint) {
            previous_checkpoint_sequence = iterator->trust_checkpoint->head_sequence;
            previous_checkpoint_id = iterator->trust_checkpoint->id;
            break;
        }
    }
    if (current_trust_checkpoint.head_sequence < previous_checkpoint_sequence ||
        (current_trust_checkpoint.head_sequence == previous_checkpoint_sequence &&
         previous_checkpoint_sequence > 0 && current_trust_checkpoint.id != previous_checkpoint_id)) {
        return Result<ExecutionLedgerCommandDecision>::failure(
            StatusCode::IdentityMismatch, "current trust checkpoint rolled back or forked from the ledger",
            current_trust_checkpoint.id);
    }
    auto inactive = inactive_reviewer_key(session, current_bundle.value());
    if (!inactive)
        return inactive.error();
    if (inactive.value()) {
        ExecutionDependencyRevocation revocation;
        revocation.kind = ExecutionDependencyKind::ReviewerKey;
        revocation.subject_id = *inactive.value();
        revocation.detail = "reviewer key is not active at the caller-pinned current checkpoint";
        ExecutionLedgerRecord record;
        record.sequence = fresh.value().records_.size();
        record.parent_id = fresh.value().current_record_id_;
        record.ledger_id = fresh.value().id_;
        record.session_id = fresh.value().session_id_;
        record.type = ExecutionLedgerRecordType::DependencyRevoked;
        record.observed_monotonic_ns = dispatch_monotonic_ns;
        record.trust_checkpoint = current_trust_checkpoint;
        record.revocation = std::move(revocation);
        record.id = internal::execution_ledger_record_identity(record);
        auto appended = append_record_unlocked(std::move(fresh).value(), std::move(record));
        if (!appended)
            return appended.error();
        auto decision = make_decision(*this, std::nullopt);
        return decision;
    }
    if (before.status != ExecutionLedgerStatus::Open || command_index != before.next_command_index) {
        return Result<ExecutionLedgerCommandDecision>::failure(
            StatusCode::IdentityMismatch,
            "execution-ledger command is duplicated, outstanding, or out of order",
            fresh.value().current_record_id_);
    }
    auto authorization = session.authorize_command(command_index, configuration, dispatch_monotonic_ns);
    if (!authorization)
        return authorization.error();
    if (!authorization.value()) {
        auto decision = make_decision(fresh.value(), std::nullopt);
        *this = std::move(fresh).value();
        return decision;
    }
    ExecutionLedgerRecord record;
    record.sequence = fresh.value().records_.size();
    record.parent_id = fresh.value().current_record_id_;
    record.ledger_id = fresh.value().id_;
    record.session_id = fresh.value().session_id_;
    record.type = ExecutionLedgerRecordType::CommandAuthorized;
    record.observed_monotonic_ns = dispatch_monotonic_ns;
    record.authorization = *authorization.value();
    record.trust_checkpoint = current_trust_checkpoint;
    record.id = internal::execution_ledger_record_identity(record);
    auto appended = append_record_unlocked(std::move(fresh).value(), std::move(record));
    if (!appended)
        return appended.error();
    auto decision = make_decision(*this, *authorization.value());
    if (!decision.valid()) {
        return Result<ExecutionLedgerCommandDecision>::failure(
            StatusCode::InternalError, "constructed execution-ledger command decision is invalid");
    }
    return decision;
}

Result<ExecutionLedgerRecord> ExecutionLedger::record_completion(
    const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
    const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
    const ExecutionControllerCompletion& completion, const std::string& expected_current_record_id) {
    if (!completion.valid() || !internal::valid_sha256(expected_current_record_id)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger completion input is invalid");
    }
    auto lock = internal::ExecutionLedgerWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ExecutionLedger::open(directory_, session, reviewed, trust_history, atlas, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_current_record_id) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution-ledger current record changed",
                                                      fresh.value().current_record_id_);
    }
    const auto before = fresh.value().summary();
    if (before.status != ExecutionLedgerStatus::AwaitingCompletion ||
        !fresh.value().records_.back().authorization) {
        return Result<ExecutionLedgerRecord>::failure(
            StatusCode::IdentityMismatch, "execution ledger has no outstanding command authorization",
            fresh.value().current_record_id_);
    }
    auto verified = verify_execution_controller_completion(
        session, *fresh.value().records_.back().authorization, completion);
    if (!verified)
        return verified.error();
    ExecutionLedgerRecord record;
    record.sequence = fresh.value().records_.size();
    record.parent_id = fresh.value().current_record_id_;
    record.ledger_id = fresh.value().id_;
    record.session_id = fresh.value().session_id_;
    record.type = ExecutionLedgerRecordType::ControllerCompletion;
    record.observed_monotonic_ns = completion.completed_monotonic_ns;
    record.completion = completion;
    record.id = internal::execution_ledger_record_identity(record);
    return append_record_unlocked(std::move(fresh).value(), std::move(record));
}

Result<ExecutionLedgerRecord> ExecutionLedger::cancel(const BoundedExecutionSession& session,
                                                      const ReviewedDeploymentProfile& reviewed,
                                                      const ServiceTrustHistory& trust_history,
                                                      const SafeAtlas& atlas,
                                                      std::uint64_t observed_monotonic_ns, std::string detail,
                                                      const std::string& expected_current_record_id) {
    if (observed_monotonic_ns == 0 || !valid_text(detail) ||
        !internal::valid_sha256(expected_current_record_id)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger cancellation input is invalid");
    }
    auto lock = internal::ExecutionLedgerWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ExecutionLedger::open(directory_, session, reviewed, trust_history, atlas, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_current_record_id) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution-ledger current record changed",
                                                      fresh.value().current_record_id_);
    }
    if (terminal_status(fresh.value().summary().status)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution ledger is already terminal",
                                                      fresh.value().current_record_id_);
    }
    if (observed_monotonic_ns < fresh.value().records_.back().observed_monotonic_ns) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger cancellation time regresses",
                                                      fresh.value().current_record_id_);
    }
    ExecutionLedgerRecord record;
    record.sequence = fresh.value().records_.size();
    record.parent_id = fresh.value().current_record_id_;
    record.ledger_id = fresh.value().id_;
    record.session_id = fresh.value().session_id_;
    record.type = ExecutionLedgerRecordType::SessionCancelled;
    record.observed_monotonic_ns = observed_monotonic_ns;
    record.detail = std::move(detail);
    record.id = internal::execution_ledger_record_identity(record);
    return append_record_unlocked(std::move(fresh).value(), std::move(record));
}

Result<ExecutionLedgerRecord>
ExecutionLedger::expire(const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
                        const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                        std::uint64_t observed_monotonic_ns, const std::string& expected_current_record_id) {
    if (observed_monotonic_ns <= session.valid_through_monotonic_ns() ||
        !internal::valid_sha256(expected_current_record_id)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger expiration input is invalid");
    }
    auto lock = internal::ExecutionLedgerWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ExecutionLedger::open(directory_, session, reviewed, trust_history, atlas, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_current_record_id) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution-ledger current record changed",
                                                      fresh.value().current_record_id_);
    }
    if (terminal_status(fresh.value().summary().status)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution ledger is already terminal",
                                                      fresh.value().current_record_id_);
    }
    if (observed_monotonic_ns < fresh.value().records_.back().observed_monotonic_ns) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger expiration time regresses",
                                                      fresh.value().current_record_id_);
    }
    ExecutionLedgerRecord record;
    record.sequence = fresh.value().records_.size();
    record.parent_id = fresh.value().current_record_id_;
    record.ledger_id = fresh.value().id_;
    record.session_id = fresh.value().session_id_;
    record.type = ExecutionLedgerRecordType::SessionExpired;
    record.observed_monotonic_ns = observed_monotonic_ns;
    record.id = internal::execution_ledger_record_identity(record);
    return append_record_unlocked(std::move(fresh).value(), std::move(record));
}

Result<ExecutionLedgerRecord> ExecutionLedger::revoke_dependency(
    const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
    const ServiceTrustHistory& trust_history, const SafeAtlas& atlas, ExecutionDependencyKind kind,
    std::string subject_id, std::uint64_t observed_monotonic_ns, std::string detail,
    const std::string& expected_current_record_id) {
    ExecutionDependencyRevocation revocation{kind, std::move(subject_id), std::move(detail)};
    if (observed_monotonic_ns == 0 || !valid_execution_dependency_revocation(revocation) ||
        !internal::valid_sha256(expected_current_record_id)) {
        return Result<ExecutionLedgerRecord>::failure(
            StatusCode::InvalidArgument, "execution-ledger dependency-revocation input is invalid");
    }
    auto lock = internal::ExecutionLedgerWriteLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ExecutionLedger::open(directory_, session, reviewed, trust_history, atlas, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_record_id_ != expected_current_record_id) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution-ledger current record changed",
                                                      fresh.value().current_record_id_);
    }
    if (terminal_status(fresh.value().summary().status)) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IdentityMismatch,
                                                      "execution ledger is already terminal",
                                                      fresh.value().current_record_id_);
    }
    if (observed_monotonic_ns < fresh.value().records_.back().observed_monotonic_ns) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::InvalidArgument,
                                                      "execution-ledger revocation time regresses",
                                                      fresh.value().current_record_id_);
    }
    if (!dependency_matches_session(session, revocation.kind, revocation.subject_id,
                                    fresh.value().records_)) {
        return Result<ExecutionLedgerRecord>::failure(
            StatusCode::IdentityMismatch, "revocation subject is not an exact bounded-session dependency",
            revocation.subject_id);
    }
    ExecutionLedgerRecord record;
    record.sequence = fresh.value().records_.size();
    record.parent_id = fresh.value().current_record_id_;
    record.ledger_id = fresh.value().id_;
    record.session_id = fresh.value().session_id_;
    record.type = ExecutionLedgerRecordType::DependencyRevoked;
    record.observed_monotonic_ns = observed_monotonic_ns;
    record.revocation = std::move(revocation);
    record.id = internal::execution_ledger_record_identity(record);
    return append_record_unlocked(std::move(fresh).value(), std::move(record));
}

std::string execution_completion_outcome_name(ExecutionCompletionOutcome outcome) {
    switch (outcome) {
    case ExecutionCompletionOutcome::Completed:
        return "completed";
    case ExecutionCompletionOutcome::Failed:
        return "failed";
    case ExecutionCompletionOutcome::Rejected:
        return "rejected";
    }
    return "unknown";
}

std::string execution_dependency_kind_name(ExecutionDependencyKind kind) {
    switch (kind) {
    case ExecutionDependencyKind::ReviewedProfile:
        return "reviewed_profile";
    case ExecutionDependencyKind::Atlas:
        return "atlas";
    case ExecutionDependencyKind::Scene:
        return "scene";
    case ExecutionDependencyKind::ControllerKey:
        return "controller_key";
    case ExecutionDependencyKind::RuntimeMonitorKey:
        return "runtime_monitor_key";
    case ExecutionDependencyKind::ReviewerKey:
        return "reviewer_key";
    case ExecutionDependencyKind::TrustCheckpoint:
        return "trust_checkpoint";
    }
    return "unknown";
}

std::string execution_ledger_record_type_name(ExecutionLedgerRecordType type) {
    switch (type) {
    case ExecutionLedgerRecordType::SessionOpened:
        return "session_opened";
    case ExecutionLedgerRecordType::CommandAuthorized:
        return "command_authorized";
    case ExecutionLedgerRecordType::ControllerCompletion:
        return "controller_completion";
    case ExecutionLedgerRecordType::SessionCancelled:
        return "session_cancelled";
    case ExecutionLedgerRecordType::SessionExpired:
        return "session_expired";
    case ExecutionLedgerRecordType::DependencyRevoked:
        return "dependency_revoked";
    }
    return "unknown";
}

std::string execution_ledger_status_name(ExecutionLedgerStatus status) {
    switch (status) {
    case ExecutionLedgerStatus::Open:
        return "open";
    case ExecutionLedgerStatus::AwaitingCompletion:
        return "awaiting_completion";
    case ExecutionLedgerStatus::Completed:
        return "completed";
    case ExecutionLedgerStatus::Cancelled:
        return "cancelled";
    case ExecutionLedgerStatus::Expired:
        return "expired";
    case ExecutionLedgerStatus::Revoked:
        return "revoked";
    case ExecutionLedgerStatus::Failed:
        return "failed";
    }
    return "unknown";
}

} // namespace rbfsafe
