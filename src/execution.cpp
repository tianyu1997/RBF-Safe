#include <rbfsafe/execution.h>

#include "internal/certificate_utils.h"
#include "internal/execution.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumCommands = 100'000;
constexpr std::size_t kMaximumDimension = 1'000;
constexpr std::size_t kMaximumApprovals = 100'000;
constexpr std::uint64_t kMaximumBoundNanoseconds = 86'400'000'000'000ULL;

bool valid_text(std::string_view value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_lower_hex(std::string_view text, std::size_t characters) {
    return text.size() == characters && std::all_of(text.begin(), text.end(), [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

bool valid_endpoint_role(ExecutionEndpointRole role) {
    return role >= ExecutionEndpointRole::Controller && role <= ExecutionEndpointRole::RuntimeMonitor;
}

bool valid_monitor_state(ExecutionMonitorState state) {
    return state >= ExecutionMonitorState::ArmedCertifiedSequence && state <= ExecutionMonitorState::Fault;
}

bool valid_review_role(DeploymentReviewRole role) {
    return role >= DeploymentReviewRole::Safety && role <= DeploymentReviewRole::Security;
}

bool valid_configuration(std::span<const double> configuration, std::size_t dimension) {
    return configuration.size() == dimension &&
           std::all_of(configuration.begin(), configuration.end(),
                       [](double value) { return std::isfinite(value); });
}

bool checked_add(std::uint64_t first, std::uint64_t second, std::uint64_t& result) {
    if (second > std::numeric_limits<std::uint64_t>::max() - first)
        return false;
    result = first + second;
    return true;
}

auto approval_order(const ExecutionSessionApproval& approval) {
    return std::tie(approval.signer_service_id, approval.signer_key_id, approval.role, approval.id);
}

bool matches_profile_approval(const ReviewedDeploymentProfile& reviewed,
                              const ExecutionSessionApproval& approval) {
    return std::any_of(reviewed.approval_set().approvals.begin(), reviewed.approval_set().approvals.end(),
                       [&](const DeploymentProfileApproval& profile_approval) {
                           return profile_approval.signer_service_id == approval.signer_service_id &&
                                  profile_approval.signer_key_id == approval.signer_key_id &&
                                  profile_approval.role == approval.role;
                       });
}

Result<void> validate_session_approval_policy(const ReviewedDeploymentProfile& reviewed,
                                              const std::vector<ExecutionSessionApproval>& approvals) {
    const auto& policy = reviewed.profile().review_policy;
    if (approvals.size() < policy.minimum_approvals) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution session approval quorum is not satisfied",
                                     reviewed.profile().id);
    }
    std::set<std::string> services;
    std::set<DeploymentReviewRole> roles;
    for (const auto& approval : approvals) {
        if (!matches_profile_approval(reviewed, approval)) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "execution session signer did not approve the reviewed deployment profile",
                approval.signer_key_id);
        }
        services.insert(approval.signer_service_id);
        roles.insert(approval.role);
    }
    if (policy.require_distinct_services && services.size() < policy.minimum_approvals) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution session requires approvals from distinct services",
                                     reviewed.profile().id);
    }
    for (const auto role : policy.required_roles) {
        if (!roles.contains(role)) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "execution session required review role is missing",
                                         deployment_review_role_name(role));
        }
    }
    return Result<void>::success();
}

Result<Ed25519KeyPair> verified_key_pair(std::string_view service_id, std::string_view expected_key_id,
                                         std::span<const std::byte> secret_key,
                                         std::optional<ExecutionEndpointRole> endpoint_role = std::nullopt) {
    if (!valid_text(service_id) || !internal::valid_sha256(std::string(expected_key_id)) ||
        secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<Ed25519KeyPair>::failure(StatusCode::InvalidArgument,
                                               "execution signing identity is invalid");
    }
    auto pair = ed25519_key_pair_from_seed(secret_key.first(kEd25519SeedBytes));
    if (!pair)
        return pair.error();
    if (!std::equal(pair.value().secret_key.begin(), pair.value().secret_key.end(), secret_key.begin())) {
        return Result<Ed25519KeyPair>::failure(StatusCode::IdentityMismatch,
                                               "Ed25519 secret key seed and public half do not match",
                                               std::string(expected_key_id));
    }
    if (endpoint_role) {
        auto endpoint =
            make_execution_endpoint_key(std::string(service_id), *endpoint_role, pair.value().public_key);
        if (!endpoint)
            return endpoint.error();
        if (endpoint.value().id != expected_key_id) {
            return Result<Ed25519KeyPair>::failure(StatusCode::IdentityMismatch,
                                                   "Ed25519 secret key does not match the execution endpoint",
                                                   std::string(expected_key_id));
        }
    } else {
        auto service_key = make_service_public_key(std::string(service_id), pair.value().public_key);
        if (!service_key)
            return service_key.error();
        if (service_key.value().id != expected_key_id) {
            return Result<Ed25519KeyPair>::failure(
                StatusCode::IdentityMismatch, "Ed25519 secret key does not match the deployment reviewer",
                std::string(expected_key_id));
        }
    }
    return pair;
}

} // namespace

namespace internal {
namespace {

Json configuration_json(std::span<const double> configuration) {
    Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

Json endpoint_json(const ExecutionEndpointKey& endpoint) {
    return Json::Object{
        {"algorithm", static_cast<int>(endpoint.algorithm)},
        {"id", endpoint.id},
        {"public_key", encode_hex(endpoint.public_key)},
        {"role", static_cast<int>(endpoint.role)},
        {"service_id", endpoint.service_id},
    };
}

Json command_json(const ExecutionCommand& command) {
    return Json::Object{
        {"configuration", configuration_json(command.configuration)},
        {"index", std::to_string(command.index)},
        {"scheduled_offset_ns", std::to_string(command.scheduled_offset_ns)},
    };
}

Json runtime_snapshot_json(const DeploymentRuntimeSnapshot& snapshot) {
    return Json::Object{
        {"authenticated_artifacts", snapshot.authenticated_artifacts},
        {"command_latency_ns", std::to_string(snapshot.command_latency_ns)},
        {"consecutive_missed_cycles", static_cast<double>(snapshot.consecutive_missed_cycles)},
        {"control_period_ns", std::to_string(snapshot.control_period_ns)},
        {"controller_digest", snapshot.controller_digest},
        {"deployment_id", snapshot.deployment_id},
        {"fail_closed_transport_active", snapshot.fail_closed_transport_active},
        {"observation_age_ns", std::to_string(snapshot.observation_age_ns)},
        {"platform_digest", snapshot.platform_digest},
        {"robot_digest", snapshot.robot_digest},
        {"runtime_digest", snapshot.runtime_digest},
        {"runtime_monitor_active", snapshot.runtime_monitor_active},
    };
}

Json limits_json(const ExecutionSessionLimits& limits) {
    return Json::Object{
        {"maximum_commands", static_cast<double>(limits.maximum_commands)},
        {"maximum_duration_ns", std::to_string(limits.maximum_duration_ns)},
        {"maximum_start_delay_ns", std::to_string(limits.maximum_start_delay_ns)},
    };
}

} // namespace

std::string execution_endpoint_key_identity(const ExecutionEndpointKey& key) {
    return sha256(Json(Json::Object{
                           {"algorithm", static_cast<int>(key.algorithm)},
                           {"format", "rbfsafe-execution-endpoint-key"},
                           {"public_key", encode_hex(key.public_key)},
                           {"role", static_cast<int>(key.role)},
                           {"schema", 1},
                           {"service_id", key.service_id},
                       })
                      .dump(false));
}

std::string execution_command_digest(const ExecutionCommand& command) {
    return sha256(Json(Json::Object{
                           {"command", command_json(command)},
                           {"format", "rbfsafe-execution-command"},
                           {"schema", 1},
                       })
                      .dump(false));
}

std::string execution_command_sequence_identity(const ExecutionCommandSequence& sequence) {
    Json::Array commands;
    commands.reserve(sequence.commands.size());
    for (const auto& command : sequence.commands)
        commands.emplace_back(command_json(command));
    Json::Array regions;
    regions.reserve(sequence.region_sequence.size());
    for (const auto region : sequence.region_sequence)
        regions.emplace_back(std::to_string(region));
    return sha256(Json(Json::Object{
                           {"atlas_id", sequence.atlas_id},
                           {"commands", std::move(commands)},
                           {"connectivity_certificate_id", sequence.connectivity_certificate_id},
                           {"dimension", static_cast<double>(sequence.dimension)},
                           {"format", "rbfsafe-execution-command-sequence"},
                           {"region_sequence", std::move(regions)},
                           {"robot_digest", sequence.robot_digest},
                           {"scene_digest", sequence.scene_digest},
                           {"schema", static_cast<double>(sequence.storage_schema)},
                       })
                      .dump(false));
}

std::string execution_session_request_identity(const ExecutionSessionRequest& request) {
    return sha256(Json(Json::Object{
                           {"atlas_id", request.atlas_id},
                           {"command_count", static_cast<double>(request.command_count)},
                           {"command_sequence_id", request.command_sequence_id},
                           {"controller", endpoint_json(request.controller)},
                           {"format", "rbfsafe-execution-session-request"},
                           {"limits", limits_json(request.limits)},
                           {"reviewed_profile_approval_set_id", request.reviewed_profile_approval_set_id},
                           {"reviewed_profile_id", request.reviewed_profile_id},
                           {"robot_digest", request.robot_digest},
                           {"runtime_monitor", endpoint_json(request.runtime_monitor)},
                           {"scene_digest", request.scene_digest},
                           {"schema", static_cast<double>(request.storage_schema)},
                           {"session_nonce", request.session_nonce},
                           {"trust_bundle_id", request.trust_bundle_id},
                           {"trust_bundle_sequence", std::to_string(request.trust_bundle_sequence)},
                           {"trust_checkpoint_id", request.trust_checkpoint_id},
                           {"trust_root_bundle_id", request.trust_root_bundle_id},
                       })
                      .dump(false));
}

std::string execution_session_approval_message(const ExecutionSessionApproval& approval) {
    return std::string("rbfsafe-execution-session-approval-v1\n") +
           Json(Json::Object{
                    {"algorithm", static_cast<int>(approval.algorithm)},
                    {"format", "rbfsafe-execution-session-approval"},
                    {"request_id", approval.request_id},
                    {"role", static_cast<int>(approval.role)},
                    {"schema", 1},
                    {"signer_key_id", approval.signer_key_id},
                    {"signer_service_id", approval.signer_service_id},
                })
               .dump(false);
}

std::string execution_session_approval_identity(const ExecutionSessionApproval& approval) {
    return sha256(std::string("rbfsafe-execution-session-approval-identity-v1\n") +
                  execution_session_approval_message(approval) + "\n" + approval.authentication_tag);
}

std::string execution_session_approval_set_identity(const ExecutionSessionApprovalSet& approval_set) {
    Json::Array approvals;
    approvals.reserve(approval_set.approvals.size());
    for (const auto& approval : approval_set.approvals)
        approvals.emplace_back(approval.id);
    return sha256(Json(Json::Object{
                           {"approval_ids", std::move(approvals)},
                           {"format", "rbfsafe-execution-session-approval-set"},
                           {"request_id", approval_set.request_id},
                           {"schema", 1},
                       })
                      .dump(false));
}

std::string
execution_controller_acknowledgement_message(const ExecutionControllerAcknowledgement& acknowledgement) {
    return std::string("rbfsafe-execution-controller-acknowledgement-v1\n") +
           Json(Json::Object{
                    {"accepted_command_count", static_cast<double>(acknowledgement.accepted_command_count)},
                    {"algorithm", static_cast<int>(acknowledgement.algorithm)},
                    {"command_sequence_id", acknowledgement.command_sequence_id},
                    {"controller_key_id", acknowledgement.controller_key_id},
                    {"controller_service_id", acknowledgement.controller_service_id},
                    {"format", "rbfsafe-execution-controller-acknowledgement"},
                    {"request_id", acknowledgement.request_id},
                    {"schema", 1},
                })
               .dump(false);
}

std::string
execution_controller_acknowledgement_identity(const ExecutionControllerAcknowledgement& acknowledgement) {
    return sha256(std::string("rbfsafe-execution-controller-acknowledgement-identity-v1\n") +
                  execution_controller_acknowledgement_message(acknowledgement) + "\n" +
                  acknowledgement.authentication_tag);
}

std::string execution_runtime_observation_identity(const ExecutionRuntimeObservation& observation) {
    return sha256(Json(Json::Object{
                           {"command_sequence_id", observation.command_sequence_id},
                           {"format", "rbfsafe-execution-runtime-observation"},
                           {"monitor_state", static_cast<int>(observation.monitor_state)},
                           {"observation_sequence", std::to_string(observation.observation_sequence)},
                           {"observed_monotonic_ns", std::to_string(observation.observed_monotonic_ns)},
                           {"request_id", observation.request_id},
                           {"runtime", runtime_snapshot_json(observation.runtime)},
                           {"schema", 1},
                       })
                      .dump(false));
}

std::string
execution_monitor_acknowledgement_message(const ExecutionMonitorAcknowledgement& acknowledgement) {
    return std::string("rbfsafe-execution-monitor-acknowledgement-v1\n") +
           Json(Json::Object{
                    {"algorithm", static_cast<int>(acknowledgement.algorithm)},
                    {"format", "rbfsafe-execution-monitor-acknowledgement"},
                    {"monitor_key_id", acknowledgement.monitor_key_id},
                    {"monitor_service_id", acknowledgement.monitor_service_id},
                    {"observation_id", acknowledgement.observation.id},
                    {"schema", 1},
                })
               .dump(false);
}

std::string
execution_monitor_acknowledgement_identity(const ExecutionMonitorAcknowledgement& acknowledgement) {
    return sha256(std::string("rbfsafe-execution-monitor-acknowledgement-identity-v1\n") +
                  execution_monitor_acknowledgement_message(acknowledgement) + "\n" +
                  acknowledgement.authentication_tag);
}

std::string bounded_execution_session_identity(const BoundedExecutionSession& session) {
    return sha256(
        Json(Json::Object{
                 {"approval_set_id", session.approval_set().id},
                 {"command_sequence_id", session.command_sequence().id},
                 {"controller_acknowledgement_id", session.controller_acknowledgement().id},
                 {"format", "rbfsafe-bounded-execution-session"},
                 {"monitor_acknowledgement_id", session.monitor_acknowledgement().id},
                 {"request_id", session.request().id},
                 {"schema", 1},
                 {"start_deadline_monotonic_ns", std::to_string(session.start_deadline_monotonic_ns())},
                 {"valid_from_monotonic_ns", std::to_string(session.valid_from_monotonic_ns())},
                 {"valid_through_monotonic_ns", std::to_string(session.valid_through_monotonic_ns())},
             })
            .dump(false));
}

std::string execution_command_authorization_identity(const ExecutionCommandAuthorization& authorization) {
    return sha256(
        Json(Json::Object{
                 {"command_digest", authorization.command_digest},
                 {"command_index", std::to_string(authorization.command_index)},
                 {"command_sequence_id", authorization.command_sequence_id},
                 {"dispatch_monotonic_ns", std::to_string(authorization.dispatch_monotonic_ns)},
                 {"evidence", static_cast<int>(authorization.evidence)},
                 {"format", "rbfsafe-execution-command-authorization"},
                 {"schema", 1},
                 {"session_id", authorization.session_id},
                 {"valid_from_monotonic_ns", std::to_string(authorization.valid_from_monotonic_ns)},
                 {"valid_through_monotonic_ns", std::to_string(authorization.valid_through_monotonic_ns)},
             })
            .dump(false));
}

} // namespace internal

bool valid_execution_endpoint_key(const ExecutionEndpointKey& key) {
    return internal::valid_sha256(key.id) && valid_text(key.service_id) && valid_endpoint_role(key.role) &&
           key.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           key.id == internal::execution_endpoint_key_identity(key);
}

Result<ExecutionEndpointKey> make_execution_endpoint_key(std::string service_id, ExecutionEndpointRole role,
                                                         std::span<const std::byte> ed25519_public_key) {
    if (!valid_text(service_id) || !valid_endpoint_role(role) ||
        ed25519_public_key.size() != kEd25519PublicKeyBytes) {
        return Result<ExecutionEndpointKey>::failure(StatusCode::InvalidArgument,
                                                     "execution endpoint key input is invalid");
    }
    ExecutionEndpointKey result;
    result.service_id = std::move(service_id);
    result.role = role;
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::copy(ed25519_public_key.begin(), ed25519_public_key.end(), result.public_key.begin());
    result.id = internal::execution_endpoint_key_identity(result);
    return result;
}

Result<ExecutionCommandSequence>
ExecutionCommandSequence::create(const SafeAtlas& atlas, std::vector<Configuration> configurations,
                                 std::vector<std::uint64_t> scheduled_offsets_ns,
                                 const TrajectoryAuditOptions& options) {
    if (atlas.dimension() == 0 || atlas.dimension() > kMaximumDimension ||
        !internal::valid_sha256(atlas.robot_digest()) || !internal::valid_sha256(atlas.scene_digest()) ||
        !internal::valid_sha256(atlas.version_info().id) || configurations.size() < 2 ||
        configurations.size() > kMaximumCommands || configurations.size() != scheduled_offsets_ns.size() ||
        options.maximum_region_tests == 0) {
        return Result<ExecutionCommandSequence>::failure(StatusCode::InvalidArgument,
                                                         "execution command sequence input is invalid");
    }
    if (scheduled_offsets_ns.front() != 0 ||
        !std::is_sorted(scheduled_offsets_ns.begin(), scheduled_offsets_ns.end()) ||
        std::adjacent_find(scheduled_offsets_ns.begin(), scheduled_offsets_ns.end()) !=
            scheduled_offsets_ns.end() ||
        !std::all_of(configurations.begin(), configurations.end(), [&](const Configuration& configuration) {
            return valid_configuration(configuration, atlas.dimension());
        })) {
        return Result<ExecutionCommandSequence>::failure(StatusCode::InvalidArgument,
                                                         "execution command schedule is invalid");
    }
    auto audit = TrajectoryAuditor{}.audit(atlas, configurations, options);
    if (!audit)
        return audit.error();
    if (audit.value().status != TrajectoryAuditStatus::Certified ||
        !audit.value().uncovered_intervals.empty()) {
        return Result<ExecutionCommandSequence>::failure(
            StatusCode::IdentityMismatch, "execution command sequence is not continuously certified",
            atlas.version_info().id);
    }
    auto route = atlas.route(configurations.front(), configurations.back());
    if (!route)
        return route.error();
    if (!route.value() || route.value()->certificate.level != EvidenceLevel::CertifiedConnectivity ||
        internal::certificate_identity(route.value()->certificate) != route.value()->certificate.id) {
        return Result<ExecutionCommandSequence>::failure(
            StatusCode::IdentityMismatch, "execution command endpoints lack a connectivity certificate",
            atlas.version_info().id);
    }
    ExecutionCommandSequence result;
    result.storage_schema = 1;
    result.atlas_id = atlas.version_info().id;
    result.robot_digest = atlas.robot_digest();
    result.scene_digest = atlas.scene_digest();
    result.connectivity_certificate_id = route.value()->certificate.id;
    result.dimension = atlas.dimension();
    result.region_sequence = std::move(audit).value().region_sequence;
    result.commands.reserve(configurations.size());
    for (std::size_t index = 0; index < configurations.size(); ++index) {
        ExecutionCommand command;
        command.index = index;
        command.scheduled_offset_ns = scheduled_offsets_ns[index];
        command.configuration = std::move(configurations[index]);
        result.commands.push_back(std::move(command));
    }
    result.id = internal::execution_command_sequence_identity(result);
    if (!result.valid()) {
        return Result<ExecutionCommandSequence>::failure(StatusCode::InternalError,
                                                         "constructed execution command sequence is invalid");
    }
    return result;
}

bool ExecutionCommandSequence::valid() const {
    if (storage_schema != 1 || !internal::valid_sha256(id) || !internal::valid_sha256(atlas_id) ||
        !internal::valid_sha256(robot_digest) || !internal::valid_sha256(scene_digest) ||
        !internal::valid_sha256(connectivity_certificate_id) || dimension == 0 ||
        dimension > kMaximumDimension || commands.size() < 2 || commands.size() > kMaximumCommands ||
        region_sequence.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];
        if (command.index != index || !valid_configuration(command.configuration, dimension) ||
            (index == 0 && command.scheduled_offset_ns != 0) ||
            (index > 0 && command.scheduled_offset_ns <= commands[index - 1].scheduled_offset_ns)) {
            return false;
        }
    }
    return id == internal::execution_command_sequence_identity(*this);
}

Result<void> ExecutionCommandSequence::verify_compatible(const SafeAtlas& atlas,
                                                         const TrajectoryAuditOptions& options) const {
    if (!valid() || atlas.dimension() == 0 || options.maximum_region_tests == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution command compatibility input is invalid");
    }
    if (dimension != atlas.dimension() || atlas_id != atlas.version_info().id ||
        robot_digest != atlas.robot_digest() || scene_digest != atlas.scene_digest()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution command sequence belongs to another Atlas", id);
    }
    std::vector<Configuration> trajectory;
    trajectory.reserve(commands.size());
    for (const auto& command : commands)
        trajectory.push_back(command.configuration);
    auto audit = TrajectoryAuditor{}.audit(atlas, trajectory, options);
    if (!audit)
        return audit.error();
    if (audit.value().status != TrajectoryAuditStatus::Certified ||
        !audit.value().uncovered_intervals.empty() || audit.value().region_sequence != region_sequence) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution command certification does not replay exactly", id);
    }
    auto route = atlas.route(trajectory.front(), trajectory.back());
    if (!route)
        return route.error();
    if (!route.value() || route.value()->certificate.level != EvidenceLevel::CertifiedConnectivity ||
        internal::certificate_identity(route.value()->certificate) != route.value()->certificate.id ||
        route.value()->certificate.id != connectivity_certificate_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution connectivity certificate does not replay exactly", id);
    }
    return Result<void>::success();
}

bool valid_execution_session_limits(const ExecutionSessionLimits& limits) {
    return limits.maximum_start_delay_ns > 0 && limits.maximum_start_delay_ns <= kMaximumBoundNanoseconds &&
           limits.maximum_duration_ns > 0 && limits.maximum_duration_ns <= kMaximumBoundNanoseconds &&
           limits.maximum_start_delay_ns <= limits.maximum_duration_ns && limits.maximum_commands >= 2 &&
           limits.maximum_commands <= kMaximumCommands;
}

Result<ExecutionSessionRequest>
ExecutionSessionRequest::create(const ReviewedDeploymentProfile& reviewed,
                                const ExecutionCommandSequence& command_sequence,
                                ExecutionSessionRequestInput input) {
    if (!reviewed.valid() || !command_sequence.valid() || !internal::valid_sha256(input.session_nonce) ||
        !valid_execution_endpoint_key(input.controller) ||
        !valid_execution_endpoint_key(input.runtime_monitor) ||
        input.controller.role != ExecutionEndpointRole::Controller ||
        input.runtime_monitor.role != ExecutionEndpointRole::RuntimeMonitor ||
        input.controller.service_id == input.runtime_monitor.service_id ||
        input.controller.id == input.runtime_monitor.id || !valid_execution_session_limits(input.limits) ||
        command_sequence.commands.size() > input.limits.maximum_commands ||
        command_sequence.robot_digest != reviewed.profile().robot_digest ||
        input.limits.maximum_start_delay_ns >
            reviewed.profile().runtime_constraints.maximum_observation_age_ns ||
        input.limits.maximum_duration_ns >
            reviewed.profile().runtime_constraints.maximum_observation_age_ns) {
        return Result<ExecutionSessionRequest>::failure(StatusCode::InvalidArgument,
                                                        "execution session request input is invalid");
    }
    for (std::size_t index = 1; index < command_sequence.commands.size(); ++index) {
        const auto period = command_sequence.commands[index].scheduled_offset_ns -
                            command_sequence.commands[index - 1].scheduled_offset_ns;
        if (period > reviewed.profile().runtime_constraints.maximum_control_period_ns) {
            return Result<ExecutionSessionRequest>::failure(
                StatusCode::IdentityMismatch, "execution command schedule exceeds reviewed control period",
                command_sequence.id);
        }
    }
    if (command_sequence.commands.back().scheduled_offset_ns > input.limits.maximum_duration_ns) {
        return Result<ExecutionSessionRequest>::failure(StatusCode::IdentityMismatch,
                                                        "execution command schedule exceeds session duration",
                                                        command_sequence.id);
    }
    ExecutionSessionRequest result;
    result.storage_schema = 1;
    result.session_nonce = std::move(input.session_nonce);
    result.reviewed_profile_id = reviewed.profile().id;
    result.reviewed_profile_approval_set_id = reviewed.approval_set().id;
    result.trust_root_bundle_id = reviewed.profile().trust_root_bundle_id;
    result.trust_checkpoint_id = reviewed.profile().trust_checkpoint_id;
    result.trust_bundle_id = reviewed.profile().trust_bundle_id;
    result.trust_bundle_sequence = reviewed.profile().trust_bundle_sequence;
    result.command_sequence_id = command_sequence.id;
    result.atlas_id = command_sequence.atlas_id;
    result.robot_digest = command_sequence.robot_digest;
    result.scene_digest = command_sequence.scene_digest;
    result.command_count = command_sequence.commands.size();
    result.controller = std::move(input.controller);
    result.runtime_monitor = std::move(input.runtime_monitor);
    result.limits = input.limits;
    result.id = internal::execution_session_request_identity(result);
    return result;
}

bool ExecutionSessionRequest::valid() const {
    return storage_schema == 1 && internal::valid_sha256(id) && internal::valid_sha256(session_nonce) &&
           internal::valid_sha256(reviewed_profile_id) &&
           internal::valid_sha256(reviewed_profile_approval_set_id) &&
           internal::valid_sha256(trust_root_bundle_id) && internal::valid_sha256(trust_checkpoint_id) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           internal::valid_sha256(command_sequence_id) && internal::valid_sha256(atlas_id) &&
           internal::valid_sha256(robot_digest) && internal::valid_sha256(scene_digest) &&
           command_count >= 2 && command_count <= kMaximumCommands &&
           valid_execution_endpoint_key(controller) && valid_execution_endpoint_key(runtime_monitor) &&
           controller.role == ExecutionEndpointRole::Controller &&
           runtime_monitor.role == ExecutionEndpointRole::RuntimeMonitor &&
           controller.service_id != runtime_monitor.service_id && controller.id != runtime_monitor.id &&
           valid_execution_session_limits(limits) && command_count <= limits.maximum_commands &&
           id == internal::execution_session_request_identity(*this);
}

bool valid_execution_session_approval(const ExecutionSessionApproval& approval) {
    return internal::valid_sha256(approval.id) && internal::valid_sha256(approval.request_id) &&
           valid_text(approval.signer_service_id) && internal::valid_sha256(approval.signer_key_id) &&
           valid_review_role(approval.role) &&
           approval.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_lower_hex(approval.authentication_tag, kEd25519SignatureBytes * 2) &&
           approval.id == internal::execution_session_approval_identity(approval);
}

bool valid_execution_session_approval_set(const ExecutionSessionApprovalSet& approval_set) {
    if (!internal::valid_sha256(approval_set.id) || !internal::valid_sha256(approval_set.request_id) ||
        approval_set.approvals.empty() || approval_set.approvals.size() > kMaximumApprovals ||
        !std::all_of(approval_set.approvals.begin(), approval_set.approvals.end(),
                     valid_execution_session_approval) ||
        !std::is_sorted(approval_set.approvals.begin(), approval_set.approvals.end(),
                        [](const auto& first, const auto& second) {
                            return approval_order(first) < approval_order(second);
                        })) {
        return false;
    }
    std::set<std::pair<std::string, std::string>> signers;
    for (const auto& approval : approval_set.approvals) {
        if (approval.request_id != approval_set.request_id ||
            !signers.emplace(approval.signer_service_id, approval.signer_key_id).second) {
            return false;
        }
    }
    return approval_set.id == internal::execution_session_approval_set_identity(approval_set);
}

Result<ExecutionSessionApproval>
sign_execution_session_approval(const ExecutionSessionRequest& request,
                                const DeploymentProfileApproval& profile_approval,
                                std::span<const std::byte> ed25519_secret_key) {
    if (!request.valid() || !valid_deployment_profile_approval(profile_approval)) {
        return Result<ExecutionSessionApproval>::failure(StatusCode::InvalidArgument,
                                                         "execution session approval input is invalid");
    }
    auto pair = verified_key_pair(profile_approval.signer_service_id, profile_approval.signer_key_id,
                                  ed25519_secret_key);
    if (!pair)
        return pair.error();
    ExecutionSessionApproval result;
    result.request_id = request.id;
    result.signer_service_id = profile_approval.signer_service_id;
    result.signer_key_id = profile_approval.signer_key_id;
    result.role = profile_approval.role;
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::execution_session_approval_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::execution_session_approval_identity(result);
    return result;
}

Result<ExecutionSessionApprovalSet>
assemble_execution_session_approvals(const ExecutionSessionRequest& request,
                                     const ReviewedDeploymentProfile& reviewed,
                                     std::vector<ExecutionSessionApproval> approvals) {
    if (!request.valid() || !reviewed.valid() || approvals.empty() || approvals.size() > kMaximumApprovals ||
        request.reviewed_profile_id != reviewed.profile().id ||
        request.reviewed_profile_approval_set_id != reviewed.approval_set().id) {
        return Result<ExecutionSessionApprovalSet>::failure(
            StatusCode::InvalidArgument, "execution session approval-set input is invalid");
    }
    std::sort(approvals.begin(), approvals.end(), [](const auto& first, const auto& second) {
        return approval_order(first) < approval_order(second);
    });
    ExecutionSessionApprovalSet result;
    result.request_id = request.id;
    result.approvals = std::move(approvals);
    result.id = internal::execution_session_approval_set_identity(result);
    if (!valid_execution_session_approval_set(result)) {
        return Result<ExecutionSessionApprovalSet>::failure(
            StatusCode::InvalidArgument, "execution session approvals are invalid or duplicated");
    }
    auto policy = validate_session_approval_policy(reviewed, result.approvals);
    if (!policy)
        return policy.error();
    return result;
}

Result<void> verify_execution_session_approvals(const ExecutionSessionRequest& request,
                                                const ReviewedDeploymentProfile& reviewed,
                                                const ExecutionSessionApprovalSet& approval_set,
                                                const ServiceTrustBundle& trust_bundle) {
    if (!request.valid() || !reviewed.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution session approval verification input is invalid");
    }
    if (!valid_execution_session_approval_set(approval_set)) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "execution session approval-set identity is invalid");
    }
    if (approval_set.request_id != request.id || request.reviewed_profile_id != reviewed.profile().id ||
        request.reviewed_profile_approval_set_id != reviewed.approval_set().id ||
        request.trust_bundle_id != trust_bundle.id() ||
        request.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution session review trust binding is invalid", request.id);
    }
    auto policy = validate_session_approval_policy(reviewed, approval_set.approvals);
    if (!policy)
        return policy;
    for (const auto& approval : approval_set.approvals) {
        auto trusted =
            trusted_service_public_key(trust_bundle, approval.signer_service_id, approval.signer_key_id,
                                       ArtifactTransferOperation::Publish, request.trust_bundle_sequence);
        if (!trusted)
            return trusted.error();
        auto signature = internal::decode_hex(approval.authentication_tag, kEd25519SignatureBytes);
        if (!signature)
            return signature.error();
        const auto message = internal::execution_session_approval_message(approval);
        auto verified = ed25519_verify(std::as_bytes(std::span(message.data(), message.size())),
                                       signature.value(), trusted.value().public_key);
        if (!verified)
            return verified.error();
    }
    return Result<void>::success();
}

bool valid_execution_controller_acknowledgement(const ExecutionControllerAcknowledgement& acknowledgement) {
    return internal::valid_sha256(acknowledgement.id) && internal::valid_sha256(acknowledgement.request_id) &&
           internal::valid_sha256(acknowledgement.command_sequence_id) &&
           valid_text(acknowledgement.controller_service_id) &&
           internal::valid_sha256(acknowledgement.controller_key_id) &&
           acknowledgement.accepted_command_count >= 2 &&
           acknowledgement.accepted_command_count <= kMaximumCommands &&
           acknowledgement.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_lower_hex(acknowledgement.authentication_tag, kEd25519SignatureBytes * 2) &&
           acknowledgement.id == internal::execution_controller_acknowledgement_identity(acknowledgement);
}

Result<ExecutionControllerAcknowledgement>
sign_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                          std::span<const std::byte> ed25519_secret_key) {
    if (!request.valid()) {
        return Result<ExecutionControllerAcknowledgement>::failure(
            StatusCode::InvalidArgument, "execution controller acknowledgement input is invalid");
    }
    auto pair = verified_key_pair(request.controller.service_id, request.controller.id, ed25519_secret_key,
                                  ExecutionEndpointRole::Controller);
    if (!pair)
        return pair.error();
    ExecutionControllerAcknowledgement result;
    result.request_id = request.id;
    result.command_sequence_id = request.command_sequence_id;
    result.controller_service_id = request.controller.service_id;
    result.controller_key_id = request.controller.id;
    result.accepted_command_count = request.command_count;
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::execution_controller_acknowledgement_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::execution_controller_acknowledgement_identity(result);
    return result;
}

Result<void>
verify_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                            const ExecutionControllerAcknowledgement& acknowledgement) {
    if (!request.valid() || !valid_execution_controller_acknowledgement(acknowledgement)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution controller acknowledgement verification input is invalid");
    }
    if (acknowledgement.request_id != request.id ||
        acknowledgement.command_sequence_id != request.command_sequence_id ||
        acknowledgement.controller_service_id != request.controller.service_id ||
        acknowledgement.controller_key_id != request.controller.id ||
        acknowledgement.accepted_command_count != request.command_count) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution controller acknowledgement binding is invalid", request.id);
    }
    auto signature = internal::decode_hex(acknowledgement.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::execution_controller_acknowledgement_message(acknowledgement);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          request.controller.public_key);
}

Result<ExecutionRuntimeObservation>
ExecutionRuntimeObservation::create(const ExecutionSessionRequest& request,
                                    ExecutionRuntimeObservationInput input) {
    if (!request.valid() || !valid_deployment_runtime_snapshot(input.runtime) ||
        input.observation_sequence == 0 || input.observed_monotonic_ns == 0 ||
        !valid_monitor_state(input.monitor_state)) {
        return Result<ExecutionRuntimeObservation>::failure(StatusCode::InvalidArgument,
                                                            "execution runtime observation input is invalid");
    }
    ExecutionRuntimeObservation result;
    result.request_id = request.id;
    result.command_sequence_id = request.command_sequence_id;
    result.runtime = std::move(input.runtime);
    result.observation_sequence = input.observation_sequence;
    result.observed_monotonic_ns = input.observed_monotonic_ns;
    result.monitor_state = input.monitor_state;
    result.id = internal::execution_runtime_observation_identity(result);
    return result;
}

bool ExecutionRuntimeObservation::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(request_id) &&
           internal::valid_sha256(command_sequence_id) && valid_deployment_runtime_snapshot(runtime) &&
           observation_sequence > 0 && observed_monotonic_ns > 0 && valid_monitor_state(monitor_state) &&
           id == internal::execution_runtime_observation_identity(*this);
}

bool valid_execution_monitor_acknowledgement(const ExecutionMonitorAcknowledgement& acknowledgement) {
    return internal::valid_sha256(acknowledgement.id) && acknowledgement.observation.valid() &&
           valid_text(acknowledgement.monitor_service_id) &&
           internal::valid_sha256(acknowledgement.monitor_key_id) &&
           acknowledgement.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_lower_hex(acknowledgement.authentication_tag, kEd25519SignatureBytes * 2) &&
           acknowledgement.id == internal::execution_monitor_acknowledgement_identity(acknowledgement);
}

Result<ExecutionMonitorAcknowledgement>
sign_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                       ExecutionRuntimeObservation observation,
                                       std::span<const std::byte> ed25519_secret_key) {
    if (!request.valid() || !observation.valid() || observation.request_id != request.id ||
        observation.command_sequence_id != request.command_sequence_id) {
        return Result<ExecutionMonitorAcknowledgement>::failure(
            StatusCode::InvalidArgument, "execution monitor acknowledgement input is invalid");
    }
    auto pair = verified_key_pair(request.runtime_monitor.service_id, request.runtime_monitor.id,
                                  ed25519_secret_key, ExecutionEndpointRole::RuntimeMonitor);
    if (!pair)
        return pair.error();
    ExecutionMonitorAcknowledgement result;
    result.observation = std::move(observation);
    result.monitor_service_id = request.runtime_monitor.service_id;
    result.monitor_key_id = request.runtime_monitor.id;
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::execution_monitor_acknowledgement_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::execution_monitor_acknowledgement_identity(result);
    return result;
}

Result<void>
verify_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                         const ExecutionMonitorAcknowledgement& acknowledgement) {
    if (!request.valid() || !valid_execution_monitor_acknowledgement(acknowledgement)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution monitor acknowledgement verification input is invalid");
    }
    if (acknowledgement.observation.request_id != request.id ||
        acknowledgement.observation.command_sequence_id != request.command_sequence_id ||
        acknowledgement.monitor_service_id != request.runtime_monitor.service_id ||
        acknowledgement.monitor_key_id != request.runtime_monitor.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "execution monitor acknowledgement binding is invalid", request.id);
    }
    auto signature = internal::decode_hex(acknowledgement.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::execution_monitor_acknowledgement_message(acknowledgement);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          request.runtime_monitor.public_key);
}

bool ExecutionCommandAuthorization::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(session_id) &&
           internal::valid_sha256(command_sequence_id) && internal::valid_sha256(command_digest) &&
           dispatch_monotonic_ns >= valid_from_monotonic_ns &&
           dispatch_monotonic_ns <= valid_through_monotonic_ns &&
           valid_from_monotonic_ns <= valid_through_monotonic_ns &&
           evidence == EvidenceLevel::RuntimeExecutable &&
           id == internal::execution_command_authorization_identity(*this);
}

Result<BoundedExecutionSession> BoundedExecutionSession::create(
    ExecutionSessionRequest request, ExecutionCommandSequence command_sequence,
    ExecutionSessionApprovalSet approval_set, ExecutionControllerAcknowledgement controller_acknowledgement,
    ExecutionMonitorAcknowledgement monitor_acknowledgement, const ReviewedDeploymentProfile& reviewed,
    const ServiceTrustBundle& trust_bundle, const SafeAtlas& atlas) {
    if (!request.valid() || !command_sequence.valid() ||
        !valid_execution_session_approval_set(approval_set) ||
        !valid_execution_controller_acknowledgement(controller_acknowledgement) ||
        !valid_execution_monitor_acknowledgement(monitor_acknowledgement) || !reviewed.valid() ||
        !trust_bundle.valid()) {
        return Result<BoundedExecutionSession>::failure(StatusCode::InvalidArgument,
                                                        "bounded execution session input is invalid");
    }
    if (request.command_sequence_id != command_sequence.id ||
        request.command_count != command_sequence.commands.size() ||
        request.atlas_id != command_sequence.atlas_id ||
        request.robot_digest != command_sequence.robot_digest ||
        request.scene_digest != command_sequence.scene_digest ||
        request.reviewed_profile_id != reviewed.profile().id ||
        request.reviewed_profile_approval_set_id != reviewed.approval_set().id ||
        request.trust_root_bundle_id != reviewed.profile().trust_root_bundle_id ||
        request.trust_checkpoint_id != reviewed.profile().trust_checkpoint_id ||
        request.trust_bundle_id != reviewed.profile().trust_bundle_id ||
        request.trust_bundle_sequence != reviewed.profile().trust_bundle_sequence) {
        return Result<BoundedExecutionSession>::failure(
            StatusCode::IdentityMismatch, "bounded execution session dependency binding is invalid",
            request.id);
    }
    auto compatible = command_sequence.verify_compatible(atlas);
    if (!compatible)
        return compatible.error();
    auto approvals = verify_execution_session_approvals(request, reviewed, approval_set, trust_bundle);
    if (!approvals)
        return approvals.error();
    auto controller = verify_execution_controller_acknowledgement(request, controller_acknowledgement);
    if (!controller)
        return controller.error();
    auto monitor = verify_execution_monitor_acknowledgement(request, monitor_acknowledgement);
    if (!monitor)
        return monitor.error();
    const auto& observation = monitor_acknowledgement.observation;
    if (observation.monitor_state != ExecutionMonitorState::ArmedCertifiedSequence) {
        return Result<BoundedExecutionSession>::failure(
            StatusCode::IdentityMismatch, "runtime monitor did not acknowledge an armed certified sequence",
            observation.id);
    }
    auto runtime_assessment = reviewed.assess(observation.runtime);
    if (!runtime_assessment)
        return runtime_assessment.error();
    if (runtime_assessment.value().status != DeploymentProfileAssessmentStatus::Conformant ||
        runtime_assessment.value().evidence != EvidenceLevel::Unknown ||
        runtime_assessment.value().authorizes_execution()) {
        return Result<BoundedExecutionSession>::failure(
            StatusCode::IdentityMismatch,
            "runtime observation is not conformant with the reviewed deployment profile",
            runtime_assessment.value().id);
    }
    const auto maximum_age = reviewed.profile().runtime_constraints.maximum_observation_age_ns;
    if (observation.runtime.observation_age_ns > maximum_age ||
        request.limits.maximum_duration_ns > maximum_age - observation.runtime.observation_age_ns) {
        return Result<BoundedExecutionSession>::failure(
            StatusCode::IdentityMismatch, "execution session outlives reviewed observation freshness",
            observation.id);
    }
    BoundedExecutionSession result;
    result.request_ = std::move(request);
    result.command_sequence_ = std::move(command_sequence);
    result.approval_set_ = std::move(approval_set);
    result.controller_acknowledgement_ = std::move(controller_acknowledgement);
    result.monitor_acknowledgement_ = std::move(monitor_acknowledgement);
    result.valid_from_monotonic_ns_ = result.monitor_acknowledgement_.observation.observed_monotonic_ns;
    if (!checked_add(result.valid_from_monotonic_ns_, result.request_.limits.maximum_start_delay_ns,
                     result.start_deadline_monotonic_ns_) ||
        !checked_add(result.valid_from_monotonic_ns_, result.request_.limits.maximum_duration_ns,
                     result.valid_through_monotonic_ns_)) {
        return Result<BoundedExecutionSession>::failure(StatusCode::InvalidArgument,
                                                        "execution session monotonic time bounds overflow");
    }
    result.id_ = internal::bounded_execution_session_identity(result);
    if (!result.valid()) {
        return Result<BoundedExecutionSession>::failure(StatusCode::InternalError,
                                                        "constructed bounded execution session is invalid");
    }
    return result;
}

bool BoundedExecutionSession::valid() const {
    return internal::valid_sha256(id_) && request_.valid() && command_sequence_.valid() &&
           valid_execution_session_approval_set(approval_set_) &&
           valid_execution_controller_acknowledgement(controller_acknowledgement_) &&
           valid_execution_monitor_acknowledgement(monitor_acknowledgement_) &&
           request_.command_sequence_id == command_sequence_.id &&
           request_.command_count == command_sequence_.commands.size() &&
           approval_set_.request_id == request_.id && controller_acknowledgement_.request_id == request_.id &&
           monitor_acknowledgement_.observation.request_id == request_.id && valid_from_monotonic_ns_ > 0 &&
           start_deadline_monotonic_ns_ >= valid_from_monotonic_ns_ &&
           start_deadline_monotonic_ns_ <= valid_through_monotonic_ns_ &&
           id_ == internal::bounded_execution_session_identity(*this);
}

Result<std::optional<ExecutionCommandAuthorization>>
BoundedExecutionSession::authorize_command(std::uint64_t command_index, std::span<const double> configuration,
                                           std::uint64_t dispatch_monotonic_ns) const {
    if (!valid() || !valid_configuration(configuration, command_sequence_.dimension) ||
        dispatch_monotonic_ns == 0) {
        return Result<std::optional<ExecutionCommandAuthorization>>::failure(
            StatusCode::InvalidArgument, "execution command authorization input is invalid");
    }
    if (command_index >= command_sequence_.commands.size())
        return std::optional<ExecutionCommandAuthorization>{};
    const auto& command = command_sequence_.commands[command_index];
    if (!std::equal(configuration.begin(), configuration.end(), command.configuration.begin())) {
        return std::optional<ExecutionCommandAuthorization>{};
    }
    std::uint64_t valid_from = 0;
    if (!checked_add(valid_from_monotonic_ns_, command.scheduled_offset_ns, valid_from)) {
        return Result<std::optional<ExecutionCommandAuthorization>>::failure(
            StatusCode::InternalError, "execution command time window overflow");
    }
    std::uint64_t latency_deadline = 0;
    if (!checked_add(valid_from, monitor_acknowledgement_.observation.runtime.command_latency_ns,
                     latency_deadline)) {
        return Result<std::optional<ExecutionCommandAuthorization>>::failure(
            StatusCode::InternalError, "execution command latency window overflow");
    }
    std::uint64_t valid_through = std::min(latency_deadline, valid_through_monotonic_ns_);
    if (command_index == 0)
        valid_through = std::min(valid_through, start_deadline_monotonic_ns_);
    if (dispatch_monotonic_ns < valid_from || dispatch_monotonic_ns > valid_through) {
        return std::optional<ExecutionCommandAuthorization>{};
    }
    ExecutionCommandAuthorization result;
    result.session_id = id_;
    result.command_sequence_id = command_sequence_.id;
    result.command_index = command_index;
    result.command_digest = internal::execution_command_digest(command);
    result.dispatch_monotonic_ns = dispatch_monotonic_ns;
    result.valid_from_monotonic_ns = valid_from;
    result.valid_through_monotonic_ns = valid_through;
    result.evidence = EvidenceLevel::RuntimeExecutable;
    result.id = internal::execution_command_authorization_identity(result);
    return std::optional<ExecutionCommandAuthorization>{std::move(result)};
}

Result<void> BoundedExecutionSession::save(const std::filesystem::path& path,
                                           const SaveOptions& options) const {
    return save_bounded_execution_session(*this, path, options);
}

Result<BoundedExecutionSession>
BoundedExecutionSession::load(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
                              const ServiceTrustHistory& trust_history,
                              const ServiceTrustCheckpoint& trust_checkpoint,
                              const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
                              const BoundedExecutionSessionLoadOptions& options) {
    return load_bounded_execution_session(path, reviewed, trust_history, trust_checkpoint,
                                          expected_checkpoint_id, atlas, options);
}

std::string execution_endpoint_role_name(ExecutionEndpointRole role) {
    switch (role) {
    case ExecutionEndpointRole::Controller:
        return "controller";
    case ExecutionEndpointRole::RuntimeMonitor:
        return "runtime_monitor";
    }
    return "unknown";
}

std::string execution_monitor_state_name(ExecutionMonitorState state) {
    switch (state) {
    case ExecutionMonitorState::ArmedCertifiedSequence:
        return "armed_certified_sequence";
    case ExecutionMonitorState::Disarmed:
        return "disarmed";
    case ExecutionMonitorState::Fault:
        return "fault";
    }
    return "unknown";
}

} // namespace rbfsafe
