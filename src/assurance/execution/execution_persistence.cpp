#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/json.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumStringBytes = 4'096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const auto value : configuration)
        values.emplace_back(value);
    return values;
}

internal::Json endpoint_json(const ExecutionEndpointKey& endpoint) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string public_key;
    public_key.reserve(endpoint.public_key.size() * 2);
    for (const auto byte : endpoint.public_key) {
        const auto value = std::to_integer<unsigned int>(byte);
        public_key.push_back(digits[value >> 4U]);
        public_key.push_back(digits[value & 0x0fU]);
    }
    return internal::Json::Object{
        {"algorithm", static_cast<int>(endpoint.algorithm)},
        {"id", endpoint.id},
        {"public_key", std::move(public_key)},
        {"role", static_cast<int>(endpoint.role)},
        {"service_id", endpoint.service_id},
    };
}

internal::Json limits_json(const ExecutionSessionLimits& limits) {
    return internal::Json::Object{
        {"maximum_commands", std::to_string(limits.maximum_commands)},
        {"maximum_duration_ns", std::to_string(limits.maximum_duration_ns)},
        {"maximum_start_delay_ns", std::to_string(limits.maximum_start_delay_ns)},
    };
}

internal::Json command_sequence_json(const ExecutionCommandSequence& sequence) {
    internal::Json::Array commands;
    commands.reserve(sequence.commands.size());
    for (const auto& command : sequence.commands) {
        commands.emplace_back(internal::Json::Object{
            {"configuration", configuration_json(command.configuration)},
            {"index", std::to_string(command.index)},
            {"scheduled_offset_ns", std::to_string(command.scheduled_offset_ns)},
        });
    }
    internal::Json::Array regions;
    regions.reserve(sequence.region_sequence.size());
    for (const auto region : sequence.region_sequence)
        regions.emplace_back(std::to_string(region));
    return internal::Json::Object{
        {"atlas_id", sequence.atlas_id},
        {"commands", std::move(commands)},
        {"connectivity_certificate_id", sequence.connectivity_certificate_id},
        {"dimension", std::to_string(sequence.dimension)},
        {"id", sequence.id},
        {"region_sequence", std::move(regions)},
        {"robot_digest", sequence.robot_digest},
        {"scene_digest", sequence.scene_digest},
        {"storage_schema", static_cast<double>(sequence.storage_schema)},
    };
}

internal::Json request_json(const ExecutionSessionRequest& request) {
    return internal::Json::Object{
        {"atlas_id", request.atlas_id},
        {"command_count", std::to_string(request.command_count)},
        {"command_sequence_id", request.command_sequence_id},
        {"controller", endpoint_json(request.controller)},
        {"id", request.id},
        {"limits", limits_json(request.limits)},
        {"reviewed_profile_approval_set_id", request.reviewed_profile_approval_set_id},
        {"reviewed_profile_id", request.reviewed_profile_id},
        {"robot_digest", request.robot_digest},
        {"runtime_monitor", endpoint_json(request.runtime_monitor)},
        {"scene_digest", request.scene_digest},
        {"session_nonce", request.session_nonce},
        {"storage_schema", static_cast<double>(request.storage_schema)},
        {"trust_bundle_id", request.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(request.trust_bundle_sequence)},
        {"trust_checkpoint_id", request.trust_checkpoint_id},
        {"trust_root_bundle_id", request.trust_root_bundle_id},
    };
}

internal::Json approval_json(const ExecutionSessionApproval& approval) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(approval.algorithm)},
        {"authentication_tag", approval.authentication_tag},
        {"id", approval.id},
        {"request_id", approval.request_id},
        {"role", static_cast<int>(approval.role)},
        {"signer_key_id", approval.signer_key_id},
        {"signer_service_id", approval.signer_service_id},
    };
}

internal::Json approval_set_json(const ExecutionSessionApprovalSet& approval_set) {
    internal::Json::Array approvals;
    approvals.reserve(approval_set.approvals.size());
    for (const auto& approval : approval_set.approvals)
        approvals.emplace_back(approval_json(approval));
    return internal::Json::Object{
        {"approvals", std::move(approvals)},
        {"id", approval_set.id},
        {"request_id", approval_set.request_id},
    };
}

internal::Json controller_acknowledgement_json(const ExecutionControllerAcknowledgement& acknowledgement) {
    return internal::Json::Object{
        {"accepted_command_count", std::to_string(acknowledgement.accepted_command_count)},
        {"algorithm", static_cast<int>(acknowledgement.algorithm)},
        {"authentication_tag", acknowledgement.authentication_tag},
        {"command_sequence_id", acknowledgement.command_sequence_id},
        {"controller_key_id", acknowledgement.controller_key_id},
        {"controller_service_id", acknowledgement.controller_service_id},
        {"id", acknowledgement.id},
        {"request_id", acknowledgement.request_id},
    };
}

internal::Json runtime_snapshot_json(const DeploymentRuntimeSnapshot& runtime) {
    return internal::Json::Object{
        {"authenticated_artifacts", runtime.authenticated_artifacts},
        {"command_latency_ns", std::to_string(runtime.command_latency_ns)},
        {"consecutive_missed_cycles", std::to_string(runtime.consecutive_missed_cycles)},
        {"control_period_ns", std::to_string(runtime.control_period_ns)},
        {"controller_digest", runtime.controller_digest},
        {"deployment_id", runtime.deployment_id},
        {"fail_closed_transport_active", runtime.fail_closed_transport_active},
        {"observation_age_ns", std::to_string(runtime.observation_age_ns)},
        {"platform_digest", runtime.platform_digest},
        {"robot_digest", runtime.robot_digest},
        {"runtime_digest", runtime.runtime_digest},
        {"runtime_monitor_active", runtime.runtime_monitor_active},
    };
}

internal::Json observation_json(const ExecutionRuntimeObservation& observation) {
    return internal::Json::Object{
        {"command_sequence_id", observation.command_sequence_id},
        {"id", observation.id},
        {"monitor_state", static_cast<int>(observation.monitor_state)},
        {"observation_sequence", std::to_string(observation.observation_sequence)},
        {"observed_monotonic_ns", std::to_string(observation.observed_monotonic_ns)},
        {"request_id", observation.request_id},
        {"runtime", runtime_snapshot_json(observation.runtime)},
    };
}

internal::Json monitor_acknowledgement_json(const ExecutionMonitorAcknowledgement& acknowledgement) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(acknowledgement.algorithm)},
        {"authentication_tag", acknowledgement.authentication_tag},
        {"id", acknowledgement.id},
        {"monitor_key_id", acknowledgement.monitor_key_id},
        {"monitor_service_id", acknowledgement.monitor_service_id},
        {"observation", observation_json(acknowledgement.observation)},
    };
}

internal::Json session_json(const BoundedExecutionSession& session) {
    return internal::Json::Object{
        {"approval_set", approval_set_json(session.approval_set())},
        {"command_sequence", command_sequence_json(session.command_sequence())},
        {"controller_acknowledgement", controller_acknowledgement_json(session.controller_acknowledgement())},
        {"format", "rbfsafe-bounded-execution-session"},
        {"id", session.id()},
        {"library_version", kVersion},
        {"monitor_acknowledgement", monitor_acknowledgement_json(session.monitor_acknowledgement())},
        {"request", request_json(session.request())},
        {"schema", static_cast<double>(kSchema)},
        {"start_deadline_monotonic_ns", std::to_string(session.start_deadline_monotonic_ns())},
        {"valid_from_monotonic_ns", std::to_string(session.valid_from_monotonic_ns())},
        {"valid_through_monotonic_ns", std::to_string(session.valid_through_monotonic_ns())},
    };
}

Result<void> inspect_regular_file(const std::filesystem::path& path, bool must_exist,
                                  std::uintmax_t maximum_bytes = 0) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        if (!must_exist && error == std::errc::no_such_file_or_directory) {
            return Result<void>::success();
        }
        return Result<void>::failure(StatusCode::IoError, "failed to inspect bounded execution session");
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        if (must_exist) {
            return Result<void>::failure(StatusCode::IoError, "bounded execution session does not exist");
        }
        return Result<void>::success();
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<void>::failure(StatusCode::IoError, "bounded execution session must be a regular file");
    }
    if (maximum_bytes > 0) {
        const auto bytes = std::filesystem::file_size(path, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect bounded execution session size");
        }
        if (bytes > maximum_bytes) {
            return Result<void>::failure(StatusCode::ResourceLimit,
                                         "bounded execution session exceeds byte limit");
        }
    }
    return Result<void>::success();
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing bounded execution session");
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish bounded execution session");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "bounded execution session record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "bounded execution session string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData,
                                       "bounded execution session record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData,
                                       "bounded execution session number field is invalid", std::string(key));
    }
    return value->as_number();
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData,
                                     "bounded execution session record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData,
                                     "bounded execution session Boolean field is invalid", std::string(key));
    }
    return value->as_bool();
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
            StatusCode::CorruptData, "bounded execution session decimal field is invalid", std::string(key));
    }
    return value;
}

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto value = decimal_field(object, key);
    if (!value || value.value() > static_cast<std::uint64_t>(std::min<std::size_t>(
                                      maximum, std::numeric_limits<std::size_t>::max()))) {
        return Result<std::size_t>::failure(
            StatusCode::CorruptData, "bounded execution session size field is invalid", std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto number = number_field(object, key);
    if (!number || number.value() < 0.0 || std::floor(number.value()) != number.value() ||
        number.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(
            StatusCode::CorruptData, "bounded execution session enum field is invalid", std::string(key));
    }
    return static_cast<std::size_t>(number.value());
}

Result<std::array<std::byte, kEd25519PublicKeyBytes>> decode_public_key(std::string_view text) {
    if (text.size() != kEd25519PublicKeyBytes * 2) {
        return Result<std::array<std::byte, kEd25519PublicKeyBytes>>::failure(
            StatusCode::CorruptData, "execution endpoint public key length is invalid");
    }
    auto digit = [](char value) -> int {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        return -1;
    };
    std::array<std::byte, kEd25519PublicKeyBytes> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto high = digit(text[index * 2]);
        const auto low = digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return Result<std::array<std::byte, kEd25519PublicKeyBytes>>::failure(
                StatusCode::CorruptData, "execution endpoint public key is not lowercase hex");
        }
        result[index] = static_cast<std::byte>((high << 4) | low);
    }
    return result;
}

Result<ExecutionEndpointKey> decode_endpoint(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto service = string_field(object, "service_id");
    auto role = enum_field(object, "role", static_cast<std::size_t>(ExecutionEndpointRole::RuntimeMonitor));
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto public_key_text = string_field(object, "public_key");
    if (!id || !service || !role || !algorithm || !public_key_text) {
        return Result<ExecutionEndpointKey>::failure(StatusCode::CorruptData,
                                                     "execution endpoint record is incomplete");
    }
    if (algorithm.value() != static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519)) {
        return Result<ExecutionEndpointKey>::failure(
            StatusCode::IncompatibleFormat, "execution endpoint authentication algorithm is unsupported");
    }
    auto public_key = decode_public_key(public_key_text.value());
    if (!public_key)
        return public_key.error();
    auto result = make_execution_endpoint_key(
        std::move(service).value(), static_cast<ExecutionEndpointRole>(role.value()), public_key.value());
    if (!result || result.value().id != id.value()) {
        return Result<ExecutionEndpointKey>::failure(StatusCode::CorruptData,
                                                     "execution endpoint identity is invalid");
    }
    return result;
}

Result<ExecutionSessionLimits> decode_limits(const internal::Json& object) {
    auto delay = decimal_field(object, "maximum_start_delay_ns");
    auto duration = decimal_field(object, "maximum_duration_ns");
    auto commands = size_field(object, "maximum_commands", 100'000);
    if (!delay || !duration || !commands) {
        return Result<ExecutionSessionLimits>::failure(StatusCode::CorruptData,
                                                       "execution session limits are incomplete");
    }
    ExecutionSessionLimits result;
    result.maximum_start_delay_ns = delay.value();
    result.maximum_duration_ns = duration.value();
    result.maximum_commands = commands.value();
    if (!valid_execution_session_limits(result)) {
        return Result<ExecutionSessionLimits>::failure(StatusCode::CorruptData,
                                                       "execution session limits are invalid");
    }
    return result;
}

Result<Configuration> decode_configuration(const internal::Json& json, std::size_t dimension) {
    if (!json.is_array() || json.as_array().size() != dimension) {
        return Result<Configuration>::failure(StatusCode::CorruptData,
                                              "execution command configuration dimension is invalid");
    }
    Configuration result;
    result.reserve(dimension);
    for (const auto& value : json.as_array()) {
        if (!value.is_number() || !std::isfinite(value.as_number())) {
            return Result<Configuration>::failure(StatusCode::CorruptData,
                                                  "execution command configuration is invalid");
        }
        result.push_back(value.as_number());
    }
    return result;
}

Result<ExecutionCommandSequence> decode_command_sequence(const internal::Json& object,
                                                         const BoundedExecutionSessionLoadOptions& options) {
    auto storage_schema = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto atlas_id = string_field(object, "atlas_id");
    auto robot = string_field(object, "robot_digest");
    auto scene = string_field(object, "scene_digest");
    auto certificate = string_field(object, "connectivity_certificate_id");
    auto dimension = size_field(object, "dimension", 1'000);
    const auto* commands = object.is_object() ? object.find("commands") : nullptr;
    const auto* regions = object.is_object() ? object.find("region_sequence") : nullptr;
    if (!storage_schema || !id || !atlas_id || !robot || !scene || !certificate || !dimension ||
        commands == nullptr || !commands->is_array() || regions == nullptr || !regions->is_array()) {
        return Result<ExecutionCommandSequence>::failure(StatusCode::CorruptData,
                                                         "execution command sequence is incomplete");
    }
    if (commands->as_array().size() < 2 || commands->as_array().size() > options.maximum_commands ||
        dimension.value() > options.maximum_dimension || regions->as_array().empty() ||
        regions->as_array().size() > options.maximum_region_sequence) {
        return Result<ExecutionCommandSequence>::failure(
            StatusCode::ResourceLimit, "execution command or region count exceeds configured limit");
    }
    ExecutionCommandSequence result;
    result.storage_schema = static_cast<std::uint32_t>(storage_schema.value());
    result.id = std::move(id).value();
    result.atlas_id = std::move(atlas_id).value();
    result.robot_digest = std::move(robot).value();
    result.scene_digest = std::move(scene).value();
    result.connectivity_certificate_id = std::move(certificate).value();
    result.dimension = dimension.value();
    result.commands.reserve(commands->as_array().size());
    for (const auto& command_json : commands->as_array()) {
        auto index = decimal_field(command_json, "index");
        auto offset = decimal_field(command_json, "scheduled_offset_ns");
        const auto* configuration = command_json.is_object() ? command_json.find("configuration") : nullptr;
        if (!index || !offset || configuration == nullptr) {
            return Result<ExecutionCommandSequence>::failure(StatusCode::CorruptData,
                                                             "execution command record is incomplete");
        }
        auto decoded = decode_configuration(*configuration, result.dimension);
        if (!decoded)
            return decoded.error();
        result.commands.push_back({index.value(), offset.value(), std::move(decoded).value()});
    }
    result.region_sequence.reserve(regions->as_array().size());
    for (const auto& region_json : regions->as_array()) {
        if (!region_json.is_string()) {
            return Result<ExecutionCommandSequence>::failure(StatusCode::CorruptData,
                                                             "execution region sequence entry is invalid");
        }
        std::uint64_t region = 0;
        const auto& text = region_json.as_string();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), region);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            return Result<ExecutionCommandSequence>::failure(StatusCode::CorruptData,
                                                             "execution region identifier is invalid");
        }
        result.region_sequence.push_back(region);
    }
    if (!result.valid()) {
        return Result<ExecutionCommandSequence>::failure(StatusCode::CorruptData,
                                                         "execution command-sequence identity is invalid");
    }
    return result;
}

Result<ExecutionSessionRequest> decode_request(const internal::Json& object,
                                               const ReviewedDeploymentProfile& reviewed,
                                               const ExecutionCommandSequence& sequence) {
    auto storage_schema = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto nonce = string_field(object, "session_nonce");
    auto reviewed_id = string_field(object, "reviewed_profile_id");
    auto reviewed_approvals = string_field(object, "reviewed_profile_approval_set_id");
    auto root = string_field(object, "trust_root_bundle_id");
    auto checkpoint = string_field(object, "trust_checkpoint_id");
    auto bundle = string_field(object, "trust_bundle_id");
    auto bundle_sequence = decimal_field(object, "trust_bundle_sequence");
    auto sequence_id = string_field(object, "command_sequence_id");
    auto atlas_id = string_field(object, "atlas_id");
    auto robot = string_field(object, "robot_digest");
    auto scene = string_field(object, "scene_digest");
    auto command_count = size_field(object, "command_count", 100'000);
    const auto* controller = object.is_object() ? object.find("controller") : nullptr;
    const auto* monitor = object.is_object() ? object.find("runtime_monitor") : nullptr;
    const auto* limits_json_value = object.is_object() ? object.find("limits") : nullptr;
    if (!storage_schema || !id || !nonce || !reviewed_id || !reviewed_approvals || !root || !checkpoint ||
        !bundle || !bundle_sequence || !sequence_id || !atlas_id || !robot || !scene || !command_count ||
        controller == nullptr || monitor == nullptr || limits_json_value == nullptr) {
        return Result<ExecutionSessionRequest>::failure(StatusCode::CorruptData,
                                                        "execution session request is incomplete");
    }
    if (reviewed_id.value() != reviewed.profile().id ||
        reviewed_approvals.value() != reviewed.approval_set().id) {
        return Result<ExecutionSessionRequest>::failure(
            StatusCode::IdentityMismatch, "execution session belongs to another reviewed profile",
            id.value());
    }
    auto decoded_controller = decode_endpoint(*controller);
    auto decoded_monitor = decode_endpoint(*monitor);
    auto limits = decode_limits(*limits_json_value);
    if (!decoded_controller || !decoded_monitor || !limits) {
        if (!decoded_controller)
            return decoded_controller.error();
        if (!decoded_monitor)
            return decoded_monitor.error();
        return limits.error();
    }
    ExecutionSessionRequestInput input;
    input.session_nonce = std::move(nonce).value();
    input.controller = std::move(decoded_controller).value();
    input.runtime_monitor = std::move(decoded_monitor).value();
    input.limits = limits.value();
    auto result = ExecutionSessionRequest::create(reviewed, sequence, std::move(input));
    if (!result) {
        return Result<ExecutionSessionRequest>::failure(StatusCode::CorruptData,
                                                        "execution session request cannot be reconstructed");
    }
    const auto& value = result.value();
    if (value.storage_schema != storage_schema.value() || value.id != id.value() ||
        value.reviewed_profile_id != reviewed_id.value() ||
        value.reviewed_profile_approval_set_id != reviewed_approvals.value() ||
        value.trust_root_bundle_id != root.value() || value.trust_checkpoint_id != checkpoint.value() ||
        value.trust_bundle_id != bundle.value() || value.trust_bundle_sequence != bundle_sequence.value() ||
        value.command_sequence_id != sequence_id.value() || value.atlas_id != atlas_id.value() ||
        value.robot_digest != robot.value() || value.scene_digest != scene.value() ||
        value.command_count != command_count.value()) {
        return Result<ExecutionSessionRequest>::failure(StatusCode::CorruptData,
                                                        "execution session request identity is invalid");
    }
    return result;
}

Result<ExecutionSessionApproval> decode_approval(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto request = string_field(object, "request_id");
    auto service = string_field(object, "signer_service_id");
    auto key = string_field(object, "signer_key_id");
    auto role = enum_field(object, "role", static_cast<std::size_t>(DeploymentReviewRole::Security));
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!id || !request || !service || !key || !role || !algorithm || !tag) {
        return Result<ExecutionSessionApproval>::failure(StatusCode::CorruptData,
                                                         "execution session approval is incomplete");
    }
    ExecutionSessionApproval result;
    result.id = std::move(id).value();
    result.request_id = std::move(request).value();
    result.signer_service_id = std::move(service).value();
    result.signer_key_id = std::move(key).value();
    result.role = static_cast<DeploymentReviewRole>(role.value());
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!valid_execution_session_approval(result)) {
        return Result<ExecutionSessionApproval>::failure(StatusCode::CorruptData,
                                                         "execution session approval identity is invalid");
    }
    return result;
}

Result<ExecutionSessionApprovalSet> decode_approval_set(const internal::Json& object,
                                                        const ExecutionSessionRequest& request,
                                                        const ReviewedDeploymentProfile& reviewed,
                                                        const BoundedExecutionSessionLoadOptions& options) {
    auto id = string_field(object, "id");
    auto request_id = string_field(object, "request_id");
    const auto* approvals = object.is_object() ? object.find("approvals") : nullptr;
    if (!id || !request_id || approvals == nullptr || !approvals->is_array()) {
        return Result<ExecutionSessionApprovalSet>::failure(StatusCode::CorruptData,
                                                            "execution session approval set is incomplete");
    }
    if (approvals->as_array().empty() || approvals->as_array().size() > options.maximum_approvals) {
        return Result<ExecutionSessionApprovalSet>::failure(
            StatusCode::ResourceLimit, "execution approval count exceeds configured limit");
    }
    std::vector<ExecutionSessionApproval> decoded;
    decoded.reserve(approvals->as_array().size());
    for (const auto& value : approvals->as_array()) {
        auto approval = decode_approval(value);
        if (!approval)
            return approval.error();
        decoded.push_back(std::move(approval).value());
    }
    auto result = assemble_execution_session_approvals(request, reviewed, std::move(decoded));
    if (!result || result.value().id != id.value() || result.value().request_id != request_id.value()) {
        return Result<ExecutionSessionApprovalSet>::failure(
            StatusCode::CorruptData, "execution session approval-set identity is invalid");
    }
    return result;
}

Result<ExecutionControllerAcknowledgement> decode_controller_acknowledgement(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto request = string_field(object, "request_id");
    auto sequence = string_field(object, "command_sequence_id");
    auto service = string_field(object, "controller_service_id");
    auto key = string_field(object, "controller_key_id");
    auto count = size_field(object, "accepted_command_count", 100'000);
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!id || !request || !sequence || !service || !key || !count || !algorithm || !tag) {
        return Result<ExecutionControllerAcknowledgement>::failure(
            StatusCode::CorruptData, "execution controller acknowledgement is incomplete");
    }
    ExecutionControllerAcknowledgement result;
    result.id = std::move(id).value();
    result.request_id = std::move(request).value();
    result.command_sequence_id = std::move(sequence).value();
    result.controller_service_id = std::move(service).value();
    result.controller_key_id = std::move(key).value();
    result.accepted_command_count = count.value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!valid_execution_controller_acknowledgement(result)) {
        return Result<ExecutionControllerAcknowledgement>::failure(
            StatusCode::CorruptData, "execution controller acknowledgement identity is invalid");
    }
    return result;
}

Result<DeploymentRuntimeSnapshot> decode_runtime_snapshot(const internal::Json& object) {
    auto deployment = string_field(object, "deployment_id");
    auto robot = string_field(object, "robot_digest");
    auto controller = string_field(object, "controller_digest");
    auto platform = string_field(object, "platform_digest");
    auto runtime_digest = string_field(object, "runtime_digest");
    auto age = decimal_field(object, "observation_age_ns");
    auto latency = decimal_field(object, "command_latency_ns");
    auto period = decimal_field(object, "control_period_ns");
    auto missed = decimal_field(object, "consecutive_missed_cycles");
    auto monitor = bool_field(object, "runtime_monitor_active");
    auto transport = bool_field(object, "fail_closed_transport_active");
    auto artifacts = bool_field(object, "authenticated_artifacts");
    if (!deployment || !robot || !controller || !platform || !runtime_digest || !age || !latency || !period ||
        !missed || !monitor || !transport || !artifacts ||
        missed.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<DeploymentRuntimeSnapshot>::failure(StatusCode::CorruptData,
                                                          "execution runtime snapshot is incomplete");
    }
    DeploymentRuntimeSnapshot result;
    result.deployment_id = std::move(deployment).value();
    result.robot_digest = std::move(robot).value();
    result.controller_digest = std::move(controller).value();
    result.platform_digest = std::move(platform).value();
    result.runtime_digest = std::move(runtime_digest).value();
    result.observation_age_ns = age.value();
    result.command_latency_ns = latency.value();
    result.control_period_ns = period.value();
    result.consecutive_missed_cycles = static_cast<std::uint32_t>(missed.value());
    result.runtime_monitor_active = monitor.value();
    result.fail_closed_transport_active = transport.value();
    result.authenticated_artifacts = artifacts.value();
    if (!valid_deployment_runtime_snapshot(result)) {
        return Result<DeploymentRuntimeSnapshot>::failure(StatusCode::CorruptData,
                                                          "execution runtime snapshot is invalid");
    }
    return result;
}

Result<ExecutionRuntimeObservation> decode_observation(const internal::Json& object,
                                                       const ExecutionSessionRequest& request) {
    auto id = string_field(object, "id");
    auto request_id = string_field(object, "request_id");
    auto sequence = string_field(object, "command_sequence_id");
    auto observation_sequence = decimal_field(object, "observation_sequence");
    auto observed = decimal_field(object, "observed_monotonic_ns");
    auto state = enum_field(object, "monitor_state", static_cast<std::size_t>(ExecutionMonitorState::Fault));
    const auto* runtime = object.is_object() ? object.find("runtime") : nullptr;
    if (!id || !request_id || !sequence || !observation_sequence || !observed || !state ||
        runtime == nullptr) {
        return Result<ExecutionRuntimeObservation>::failure(StatusCode::CorruptData,
                                                            "execution runtime observation is incomplete");
    }
    auto decoded_runtime = decode_runtime_snapshot(*runtime);
    if (!decoded_runtime)
        return decoded_runtime.error();
    ExecutionRuntimeObservationInput input;
    input.runtime = std::move(decoded_runtime).value();
    input.observation_sequence = observation_sequence.value();
    input.observed_monotonic_ns = observed.value();
    input.monitor_state = static_cast<ExecutionMonitorState>(state.value());
    auto result = ExecutionRuntimeObservation::create(request, std::move(input));
    if (!result || result.value().id != id.value() || result.value().request_id != request_id.value() ||
        result.value().command_sequence_id != sequence.value()) {
        return Result<ExecutionRuntimeObservation>::failure(
            StatusCode::CorruptData, "execution runtime observation identity is invalid");
    }
    return result;
}

Result<ExecutionMonitorAcknowledgement>
decode_monitor_acknowledgement(const internal::Json& object, const ExecutionSessionRequest& request) {
    auto id = string_field(object, "id");
    auto service = string_field(object, "monitor_service_id");
    auto key = string_field(object, "monitor_key_id");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    const auto* observation = object.is_object() ? object.find("observation") : nullptr;
    if (!id || !service || !key || !algorithm || !tag || observation == nullptr) {
        return Result<ExecutionMonitorAcknowledgement>::failure(
            StatusCode::CorruptData, "execution monitor acknowledgement is incomplete");
    }
    auto decoded_observation = decode_observation(*observation, request);
    if (!decoded_observation)
        return decoded_observation.error();
    ExecutionMonitorAcknowledgement result;
    result.id = std::move(id).value();
    result.observation = std::move(decoded_observation).value();
    result.monitor_service_id = std::move(service).value();
    result.monitor_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!valid_execution_monitor_acknowledgement(result)) {
        return Result<ExecutionMonitorAcknowledgement>::failure(
            StatusCode::CorruptData, "execution monitor acknowledgement identity is invalid");
    }
    return result;
}

} // namespace

Result<void> save_bounded_execution_session(const BoundedExecutionSession& session,
                                            const std::filesystem::path& path, const SaveOptions& options) {
    if (!session.valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "bounded execution session or destination is invalid");
    }
    auto inspected = inspect_regular_file(path, false);
    if (!inspected)
        return inspected;
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect bounded execution session destination");
    }
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError,
                                     "bounded execution session destination already exists");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create bounded execution session parent");
        }
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, session_json(session).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<BoundedExecutionSession>
load_bounded_execution_session(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
                               const ServiceTrustHistory& trust_history,
                               const ServiceTrustCheckpoint& trust_checkpoint,
                               const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
                               const BoundedExecutionSessionLoadOptions& options) {
    if (path.empty() || !reviewed.valid() || !trust_history.valid() || !trust_checkpoint.valid() ||
        expected_checkpoint_id.empty() || atlas.dimension() == 0 || options.maximum_commands < 2 ||
        options.maximum_dimension == 0 || options.maximum_region_sequence == 0 ||
        options.maximum_approvals == 0 || options.maximum_payload_bytes == 0) {
        return Result<BoundedExecutionSession>::failure(StatusCode::InvalidArgument,
                                                        "bounded execution session load input is invalid");
    }
    auto inspected = inspect_regular_file(path, true, options.maximum_payload_bytes);
    if (!inspected)
        return inspected.error();
    auto document = internal::read_json_file(path);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format");
    auto schema = number_field(document.value(), "schema");
    auto library_version = string_field(document.value(), "library_version");
    auto session_id = string_field(document.value(), "id");
    auto valid_from = decimal_field(document.value(), "valid_from_monotonic_ns");
    auto start_deadline = decimal_field(document.value(), "start_deadline_monotonic_ns");
    auto valid_through = decimal_field(document.value(), "valid_through_monotonic_ns");
    const auto* sequence_json_value =
        document.value().is_object() ? document.value().find("command_sequence") : nullptr;
    const auto* request_json_value =
        document.value().is_object() ? document.value().find("request") : nullptr;
    const auto* approvals_json_value =
        document.value().is_object() ? document.value().find("approval_set") : nullptr;
    const auto* controller_json_value =
        document.value().is_object() ? document.value().find("controller_acknowledgement") : nullptr;
    const auto* monitor_json_value =
        document.value().is_object() ? document.value().find("monitor_acknowledgement") : nullptr;
    if (!format || !schema || !library_version || !session_id || !valid_from || !start_deadline ||
        !valid_through || sequence_json_value == nullptr || request_json_value == nullptr ||
        approvals_json_value == nullptr || controller_json_value == nullptr ||
        monitor_json_value == nullptr) {
        return Result<BoundedExecutionSession>::failure(StatusCode::CorruptData,
                                                        "bounded execution session document is incomplete");
    }
    if (format.value() != "rbfsafe-bounded-execution-session" ||
        schema.value() != static_cast<double>(kSchema)) {
        return Result<BoundedExecutionSession>::failure(StatusCode::IncompatibleFormat,
                                                        "unsupported bounded execution session schema");
    }
    auto reverified_reviewed = ReviewedDeploymentProfile::create(
        reviewed.profile(), reviewed.approval_set(), trust_history, trust_checkpoint, expected_checkpoint_id);
    if (!reverified_reviewed)
        return reverified_reviewed.error();
    auto trust_bundle = trust_history.bundle(reverified_reviewed.value().profile().trust_bundle_id);
    if (!trust_bundle)
        return trust_bundle.error();
    auto sequence = decode_command_sequence(*sequence_json_value, options);
    if (!sequence)
        return sequence.error();
    if (sequence.value().atlas_id != atlas.version_info().id ||
        sequence.value().robot_digest != atlas.robot_digest() ||
        sequence.value().scene_digest != atlas.scene_digest()) {
        return Result<BoundedExecutionSession>::failure(StatusCode::IdentityMismatch,
                                                        "bounded execution session belongs to another Atlas",
                                                        sequence.value().id);
    }
    auto compatible = sequence.value().verify_compatible(atlas);
    if (!compatible)
        return compatible.error();
    auto request = decode_request(*request_json_value, reverified_reviewed.value(), sequence.value());
    if (!request)
        return request.error();
    auto approvals =
        decode_approval_set(*approvals_json_value, request.value(), reverified_reviewed.value(), options);
    if (!approvals)
        return approvals.error();
    auto controller = decode_controller_acknowledgement(*controller_json_value);
    if (!controller)
        return controller.error();
    auto monitor = decode_monitor_acknowledgement(*monitor_json_value, request.value());
    if (!monitor)
        return monitor.error();
    auto result = BoundedExecutionSession::create(std::move(request).value(), std::move(sequence).value(),
                                                  std::move(approvals).value(), std::move(controller).value(),
                                                  std::move(monitor).value(), reverified_reviewed.value(),
                                                  trust_bundle.value(), atlas);
    if (!result)
        return result.error();
    if (result.value().id() != session_id.value() ||
        result.value().valid_from_monotonic_ns() != valid_from.value() ||
        result.value().start_deadline_monotonic_ns() != start_deadline.value() ||
        result.value().valid_through_monotonic_ns() != valid_through.value()) {
        return Result<BoundedExecutionSession>::failure(
            StatusCode::CorruptData, "bounded execution session identity or time bounds are invalid");
    }
    return result;
}

} // namespace rbfsafe
