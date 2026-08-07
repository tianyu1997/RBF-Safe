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

internal::Json runtime_constraints_json(const DeploymentRuntimeConstraints& constraints) {
    return internal::Json::Object{
        {"maximum_command_latency_ns", std::to_string(constraints.maximum_command_latency_ns)},
        {"maximum_consecutive_missed_cycles",
         static_cast<double>(constraints.maximum_consecutive_missed_cycles)},
        {"maximum_control_period_ns", std::to_string(constraints.maximum_control_period_ns)},
        {"maximum_observation_age_ns", std::to_string(constraints.maximum_observation_age_ns)},
        {"require_authenticated_artifacts", constraints.require_authenticated_artifacts},
        {"require_fail_closed_transport", constraints.require_fail_closed_transport},
        {"require_runtime_monitor", constraints.require_runtime_monitor},
    };
}

internal::Json review_policy_json(const DeploymentReviewPolicy& policy) {
    internal::Json::Array roles;
    roles.reserve(policy.required_roles.size());
    for (const auto role : policy.required_roles)
        roles.emplace_back(static_cast<int>(role));
    return internal::Json::Object{
        {"minimum_approvals", static_cast<double>(policy.minimum_approvals)},
        {"require_distinct_services", policy.require_distinct_services},
        {"required_roles", std::move(roles)},
    };
}

internal::Json profile_json(const DeploymentProfile& profile) {
    return internal::Json::Object{
        {"controller_digest", profile.controller_digest},
        {"deployment_id", profile.deployment_id},
        {"id", profile.id},
        {"platform_digest", profile.platform_digest},
        {"review_policy", review_policy_json(profile.review_policy)},
        {"robot_digest", profile.robot_digest},
        {"runtime_constraints", runtime_constraints_json(profile.runtime_constraints)},
        {"runtime_digest", profile.runtime_digest},
        {"storage_schema", static_cast<double>(profile.storage_schema)},
        {"trust_bundle_id", profile.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(profile.trust_bundle_sequence)},
        {"trust_checkpoint_id", profile.trust_checkpoint_id},
        {"trust_root_bundle_id", profile.trust_root_bundle_id},
    };
}

internal::Json approval_json(const DeploymentProfileApproval& approval) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(approval.algorithm)},
        {"authentication_tag", approval.authentication_tag},
        {"id", approval.id},
        {"profile_id", approval.profile_id},
        {"role", static_cast<int>(approval.role)},
        {"signer_key_id", approval.signer_key_id},
        {"signer_service_id", approval.signer_service_id},
    };
}

internal::Json approval_set_json(const DeploymentProfileApprovalSet& approval_set) {
    internal::Json::Array approvals;
    approvals.reserve(approval_set.approvals.size());
    for (const auto& approval : approval_set.approvals)
        approvals.emplace_back(approval_json(approval));
    return internal::Json::Object{
        {"approvals", std::move(approvals)},
        {"id", approval_set.id},
        {"profile_id", approval_set.profile_id},
    };
}

internal::Json reviewed_json(const ReviewedDeploymentProfile& reviewed) {
    return internal::Json::Object{
        {"approval_set", approval_set_json(reviewed.approval_set())},
        {"format", "rbfsafe-reviewed-deployment-profile"},
        {"library_version", kVersion},
        {"profile", profile_json(reviewed.profile())},
        {"schema", static_cast<double>(kSchema)},
    };
}

Result<void> inspect_regular_file(const std::filesystem::path& path, bool must_exist,
                                  std::uintmax_t maximum_bytes = 0) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        if (!must_exist && error == std::errc::no_such_file_or_directory)
            return Result<void>::success();
        return Result<void>::failure(StatusCode::IoError, "failed to inspect reviewed deployment profile");
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        if (must_exist) {
            return Result<void>::failure(StatusCode::IoError, "reviewed deployment profile does not exist");
        }
        return Result<void>::success();
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<void>::failure(StatusCode::IoError,
                                     "reviewed deployment profile must be a regular file");
    }
    if (maximum_bytes > 0) {
        const auto bytes = std::filesystem::file_size(path, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect reviewed deployment profile size");
        }
        if (bytes > maximum_bytes) {
            return Result<void>::failure(StatusCode::ResourceLimit,
                                         "reviewed deployment profile exceeds byte limit");
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
                                         "failed to stage existing reviewed deployment profile");
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish reviewed deployment profile");
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
                                            "reviewed deployment profile record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "reviewed deployment profile string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData,
                                       "reviewed deployment profile record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(
            StatusCode::CorruptData, "reviewed deployment profile number field is invalid", std::string(key));
    }
    return value->as_number();
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
                                              "reviewed deployment profile decimal field is invalid",
                                              std::string(key));
    }
    return result;
}

Result<std::size_t> integer_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto number = number_field(object, key);
    if (!number || number.value() < 0.0 || std::floor(number.value()) != number.value() ||
        number.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData,
                                            "reviewed deployment profile integer field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(number.value());
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData,
                                     "reviewed deployment profile record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData,
                                     "reviewed deployment profile boolean field is invalid",
                                     std::string(key));
    }
    return value->as_bool();
}

Result<DeploymentRuntimeConstraints> decode_runtime_constraints(const internal::Json& object) {
    auto observation = decimal_field(object, "maximum_observation_age_ns");
    auto latency = decimal_field(object, "maximum_command_latency_ns");
    auto period = decimal_field(object, "maximum_control_period_ns");
    auto missed =
        integer_field(object, "maximum_consecutive_missed_cycles", std::numeric_limits<std::uint32_t>::max());
    auto monitor = bool_field(object, "require_runtime_monitor");
    auto transport = bool_field(object, "require_fail_closed_transport");
    auto artifacts = bool_field(object, "require_authenticated_artifacts");
    if (!observation || !latency || !period || !missed || !monitor || !transport || !artifacts) {
        return Result<DeploymentRuntimeConstraints>::failure(StatusCode::CorruptData,
                                                             "deployment runtime constraints are incomplete");
    }
    DeploymentRuntimeConstraints result;
    result.maximum_observation_age_ns = observation.value();
    result.maximum_command_latency_ns = latency.value();
    result.maximum_control_period_ns = period.value();
    result.maximum_consecutive_missed_cycles = static_cast<std::uint32_t>(missed.value());
    result.require_runtime_monitor = monitor.value();
    result.require_fail_closed_transport = transport.value();
    result.require_authenticated_artifacts = artifacts.value();
    if (!valid_deployment_runtime_constraints(result)) {
        return Result<DeploymentRuntimeConstraints>::failure(StatusCode::CorruptData,
                                                             "deployment runtime constraints are invalid");
    }
    return result;
}

Result<DeploymentReviewPolicy> decode_review_policy(const internal::Json& object,
                                                    const ReviewedDeploymentProfileLoadOptions& options) {
    auto minimum = integer_field(object, "minimum_approvals", 100'000);
    auto distinct = bool_field(object, "require_distinct_services");
    const auto* roles = object.is_object() ? object.find("required_roles") : nullptr;
    if (!minimum || !distinct || roles == nullptr || !roles->is_array()) {
        return Result<DeploymentReviewPolicy>::failure(StatusCode::CorruptData,
                                                       "deployment review policy is incomplete");
    }
    if (roles->as_array().size() > options.maximum_required_roles) {
        return Result<DeploymentReviewPolicy>::failure(
            StatusCode::ResourceLimit, "deployment review role count exceeds configured limit");
    }
    DeploymentReviewPolicy result;
    result.minimum_approvals = static_cast<std::uint32_t>(minimum.value());
    result.require_distinct_services = distinct.value();
    for (const auto& role_json : roles->as_array()) {
        if (!role_json.is_number() || !std::isfinite(role_json.as_number()) || role_json.as_number() < 0.0 ||
            std::floor(role_json.as_number()) != role_json.as_number() ||
            role_json.as_number() > static_cast<double>(DeploymentReviewRole::Security)) {
            return Result<DeploymentReviewPolicy>::failure(StatusCode::CorruptData,
                                                           "deployment review role is invalid");
        }
        result.required_roles.push_back(
            static_cast<DeploymentReviewRole>(static_cast<std::size_t>(role_json.as_number())));
    }
    if (!valid_deployment_review_policy(result)) {
        return Result<DeploymentReviewPolicy>::failure(StatusCode::CorruptData,
                                                       "deployment review policy is invalid");
    }
    return result;
}

Result<DeploymentProfile> decode_profile(const internal::Json& object,
                                         const ReviewedDeploymentProfileLoadOptions& options) {
    auto id = string_field(object, "id");
    auto storage_schema = integer_field(object, "storage_schema", 1);
    auto deployment_id = string_field(object, "deployment_id");
    auto robot_digest = string_field(object, "robot_digest");
    auto controller_digest = string_field(object, "controller_digest");
    auto platform_digest = string_field(object, "platform_digest");
    auto runtime_digest = string_field(object, "runtime_digest");
    auto root = string_field(object, "trust_root_bundle_id");
    auto checkpoint = string_field(object, "trust_checkpoint_id");
    auto bundle = string_field(object, "trust_bundle_id");
    auto sequence = decimal_field(object, "trust_bundle_sequence");
    const auto* constraints_json = object.is_object() ? object.find("runtime_constraints") : nullptr;
    const auto* policy_json = object.is_object() ? object.find("review_policy") : nullptr;
    if (!id || !storage_schema || !deployment_id || !robot_digest || !controller_digest || !platform_digest ||
        !runtime_digest || !root || !checkpoint || !bundle || !sequence || constraints_json == nullptr ||
        policy_json == nullptr) {
        return Result<DeploymentProfile>::failure(StatusCode::CorruptData,
                                                  "deployment profile is incomplete");
    }
    auto constraints = decode_runtime_constraints(*constraints_json);
    auto policy = decode_review_policy(*policy_json, options);
    if (!constraints || !policy)
        return !constraints ? constraints.error() : policy.error();
    DeploymentProfileInput input;
    input.deployment_id = std::move(deployment_id).value();
    input.robot_digest = std::move(robot_digest).value();
    input.controller_digest = std::move(controller_digest).value();
    input.platform_digest = std::move(platform_digest).value();
    input.runtime_digest = std::move(runtime_digest).value();
    input.trust_root_bundle_id = std::move(root).value();
    input.trust_checkpoint_id = std::move(checkpoint).value();
    input.trust_bundle_id = std::move(bundle).value();
    input.trust_bundle_sequence = sequence.value();
    input.runtime_constraints = constraints.value();
    input.review_policy = policy.value();
    auto result = DeploymentProfile::create(std::move(input));
    if (!result || result.value().storage_schema != storage_schema.value() ||
        result.value().id != id.value()) {
        return Result<DeploymentProfile>::failure(StatusCode::CorruptData,
                                                  "deployment profile identity is invalid");
    }
    return result;
}

Result<DeploymentProfileApproval> decode_approval(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto profile_id = string_field(object, "profile_id");
    auto service_id = string_field(object, "signer_service_id");
    auto key_id = string_field(object, "signer_key_id");
    auto role = integer_field(object, "role", static_cast<std::size_t>(DeploymentReviewRole::Security));
    auto algorithm = integer_field(object, "algorithm",
                                   static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!id || !profile_id || !service_id || !key_id || !role || !algorithm || !tag) {
        return Result<DeploymentProfileApproval>::failure(StatusCode::CorruptData,
                                                          "deployment profile approval is incomplete");
    }
    DeploymentProfileApproval result;
    result.id = std::move(id).value();
    result.profile_id = std::move(profile_id).value();
    result.signer_service_id = std::move(service_id).value();
    result.signer_key_id = std::move(key_id).value();
    result.role = static_cast<DeploymentReviewRole>(role.value());
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!valid_deployment_profile_approval(result)) {
        return Result<DeploymentProfileApproval>::failure(StatusCode::CorruptData,
                                                          "deployment profile approval identity is invalid");
    }
    return result;
}

Result<DeploymentProfileApprovalSet>
decode_approval_set(const internal::Json& object, const DeploymentProfile& profile,
                    const ReviewedDeploymentProfileLoadOptions& options) {
    auto id = string_field(object, "id");
    auto profile_id = string_field(object, "profile_id");
    const auto* approvals = object.is_object() ? object.find("approvals") : nullptr;
    if (!id || !profile_id || approvals == nullptr || !approvals->is_array()) {
        return Result<DeploymentProfileApprovalSet>::failure(StatusCode::CorruptData,
                                                             "deployment profile approval set is incomplete");
    }
    if (approvals->as_array().empty() || approvals->as_array().size() > options.maximum_approvals) {
        return Result<DeploymentProfileApprovalSet>::failure(
            StatusCode::ResourceLimit, "deployment approval count exceeds configured limit");
    }
    std::vector<DeploymentProfileApproval> decoded;
    decoded.reserve(approvals->as_array().size());
    for (const auto& approval_json_value : approvals->as_array()) {
        auto approval = decode_approval(approval_json_value);
        if (!approval)
            return approval.error();
        decoded.push_back(std::move(approval).value());
    }
    auto result = assemble_deployment_profile_approvals(profile, std::move(decoded));
    if (!result || result.value().id != id.value() || result.value().profile_id != profile_id.value()) {
        return Result<DeploymentProfileApprovalSet>::failure(
            StatusCode::CorruptData, "deployment profile approval-set identity is invalid");
    }
    return result;
}

} // namespace

Result<void> save_reviewed_deployment_profile(const ReviewedDeploymentProfile& reviewed,
                                              const std::filesystem::path& path, const SaveOptions& options) {
    if (!reviewed.valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "reviewed deployment profile or destination is invalid");
    }
    auto inspected = inspect_regular_file(path, false);
    if (!inspected)
        return inspected;
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect reviewed deployment profile destination");
    }
    if (destination_exists) {
        if (!options.overwrite) {
            return Result<void>::failure(StatusCode::IoError,
                                         "reviewed deployment profile destination already exists");
        }
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create reviewed deployment profile parent");
        }
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, reviewed_json(reviewed).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<ReviewedDeploymentProfile>
load_reviewed_deployment_profile(const std::filesystem::path& path, const ServiceTrustHistory& trust_history,
                                 const ServiceTrustCheckpoint& trust_checkpoint,
                                 const std::string& expected_checkpoint_id,
                                 const ReviewedDeploymentProfileLoadOptions& options) {
    if (path.empty() || !trust_history.valid() || !trust_checkpoint.valid() ||
        expected_checkpoint_id.empty() || options.maximum_approvals == 0 ||
        options.maximum_required_roles == 0 || options.maximum_payload_bytes == 0) {
        return Result<ReviewedDeploymentProfile>::failure(
            StatusCode::InvalidArgument, "reviewed deployment profile load input is invalid");
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
    const auto* profile_json_value =
        document.value().is_object() ? document.value().find("profile") : nullptr;
    const auto* approval_set_json_value =
        document.value().is_object() ? document.value().find("approval_set") : nullptr;
    if (!format || !schema || !library_version || profile_json_value == nullptr ||
        approval_set_json_value == nullptr) {
        return Result<ReviewedDeploymentProfile>::failure(
            StatusCode::CorruptData, "reviewed deployment profile document is incomplete");
    }
    if (format.value() != "rbfsafe-reviewed-deployment-profile" ||
        schema.value() != static_cast<double>(kSchema)) {
        return Result<ReviewedDeploymentProfile>::failure(StatusCode::IncompatibleFormat,
                                                          "unsupported reviewed deployment profile schema");
    }
    auto profile = decode_profile(*profile_json_value, options);
    if (!profile)
        return profile.error();
    auto approval_set = decode_approval_set(*approval_set_json_value, profile.value(), options);
    if (!approval_set)
        return approval_set.error();
    auto result =
        ReviewedDeploymentProfile::create(std::move(profile).value(), std::move(approval_set).value(),
                                          trust_history, trust_checkpoint, expected_checkpoint_id);
    if (!result) {
        if (result.error().code == StatusCode::InvalidArgument) {
            return Result<ReviewedDeploymentProfile>::failure(
                StatusCode::CorruptData, "reviewed deployment profile structure is invalid");
        }
        return result.error();
    }
    return result;
}

} // namespace rbfsafe
