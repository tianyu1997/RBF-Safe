#include <rbfsafe/model.h>

#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace rbfsafe {
namespace {

using Matrix4 = std::array<double, 16>;

Matrix4 identity_matrix() {
    Matrix4 matrix{};
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;
    return matrix;
}

Matrix4 multiply(const Matrix4& left, const Matrix4& right) {
    Matrix4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result[row * 4 + column] += left[row * 4 + inner] * right[inner * 4 + column];
            }
        }
    }
    return result;
}

Matrix4 joint_matrix(const DhJoint& joint, double value) {
    const double theta = joint.theta + (joint.type == JointType::Revolute ? value : 0.0);
    const double d = joint.d + (joint.type == JointType::Prismatic ? value : 0.0);
    const double cosine_theta = std::cos(theta);
    const double sine_theta = std::sin(theta);
    const double cosine_alpha = std::cos(joint.alpha);
    const double sine_alpha = std::sin(joint.alpha);
    return Matrix4{cosine_theta,
                   -sine_theta,
                   0.0,
                   joint.a,
                   sine_theta * cosine_alpha,
                   cosine_theta * cosine_alpha,
                   -sine_alpha,
                   -d * sine_alpha,
                   sine_theta * sine_alpha,
                   cosine_theta * sine_alpha,
                   cosine_alpha,
                   d * cosine_alpha,
                   0.0,
                   0.0,
                   0.0,
                   1.0};
}

Pose3d pose_from_matrix(const Matrix4& matrix) {
    Pose3d pose;
    pose.position = {matrix[3], matrix[7], matrix[11]};
    const double trace = matrix[0] + matrix[5] + matrix[10];
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
    if (trace > 0.0) {
        const double scale = 2.0 * std::sqrt(std::max(0.0, trace + 1.0));
        w = 0.25 * scale;
        x = (matrix[9] - matrix[6]) / scale;
        y = (matrix[2] - matrix[8]) / scale;
        z = (matrix[4] - matrix[1]) / scale;
    } else if (matrix[0] > matrix[5] && matrix[0] > matrix[10]) {
        const double scale = 2.0 * std::sqrt(std::max(0.0, 1.0 + matrix[0] - matrix[5] - matrix[10]));
        w = (matrix[9] - matrix[6]) / scale;
        x = 0.25 * scale;
        y = (matrix[1] + matrix[4]) / scale;
        z = (matrix[2] + matrix[8]) / scale;
    } else if (matrix[5] > matrix[10]) {
        const double scale = 2.0 * std::sqrt(std::max(0.0, 1.0 + matrix[5] - matrix[0] - matrix[10]));
        w = (matrix[2] - matrix[8]) / scale;
        x = (matrix[1] + matrix[4]) / scale;
        y = 0.25 * scale;
        z = (matrix[6] + matrix[9]) / scale;
    } else {
        const double scale = 2.0 * std::sqrt(std::max(0.0, 1.0 + matrix[10] - matrix[0] - matrix[5]));
        w = (matrix[4] - matrix[1]) / scale;
        x = (matrix[2] + matrix[8]) / scale;
        y = (matrix[6] + matrix[9]) / scale;
        z = 0.25 * scale;
    }
    const double norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (norm > 0.0) {
        x /= norm;
        y /= norm;
        z /= norm;
        w /= norm;
    }
    if (w < 0.0 || (w == 0.0 && (x < 0.0 || (x == 0.0 && (y < 0.0 || (y == 0.0 && z < 0.0)))))) {
        x = -x;
        y = -y;
        z = -z;
        w = -w;
    }
    pose.orientation = {x, y, z, w};
    return pose;
}

bool finite_joint(const DhJoint& joint) {
    return std::isfinite(joint.alpha) && std::isfinite(joint.a) && std::isfinite(joint.d) &&
           std::isfinite(joint.theta) &&
           (joint.type == JointType::Revolute || joint.type == JointType::Prismatic);
}

internal::Json joint_json(const DhJoint& joint) {
    return internal::Json::Object{
        {"a", joint.a},
        {"alpha", joint.alpha},
        {"d", joint.d},
        {"theta", joint.theta},
        {"type", joint.type == JointType::Revolute ? "revolute" : "prismatic"},
    };
}

Result<double> required_number(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "missing or invalid numeric field",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::string> required_string(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

Result<DhJoint> parse_joint(const internal::Json& json) {
    if (!json.is_object())
        return Result<DhJoint>::failure(StatusCode::CorruptData, "joint must be an object");
    auto alpha = required_number(json, "alpha");
    if (!alpha)
        return alpha.error();
    auto a = required_number(json, "a");
    if (!a)
        return a.error();
    auto d = required_number(json, "d");
    if (!d)
        return d.error();
    auto theta = required_number(json, "theta");
    if (!theta)
        return theta.error();
    auto type = required_string(json, "type");
    if (!type)
        return type.error();
    JointType joint_type;
    if (type.value() == "revolute")
        joint_type = JointType::Revolute;
    else if (type.value() == "prismatic")
        joint_type = JointType::Prismatic;
    else
        return Result<DhJoint>::failure(StatusCode::CorruptData, "unknown joint type", type.value());
    return DhJoint{alpha.value(), a.value(), d.value(), theta.value(), joint_type};
}

} // namespace

bool Pose3d::valid(double tolerance) const noexcept {
    if (!std::isfinite(tolerance) || tolerance < 0.0)
        return false;
    if (!std::all_of(position.begin(), position.end(), [](double value) { return std::isfinite(value); }) ||
        !std::all_of(orientation.begin(), orientation.end(),
                     [](double value) { return std::isfinite(value); }))
        return false;
    double squared_norm = 0.0;
    for (const double value : orientation)
        squared_norm += value * value;
    return std::abs(std::sqrt(squared_norm) - 1.0) <= tolerance;
}

SerialRobotModel::SerialRobotModel(std::string name, std::vector<DhJoint> joints,
                                   std::vector<Interval> joint_limits, std::vector<double> link_radii,
                                   std::optional<DhJoint> tool_frame)
    : name_(std::move(name)), joints_(std::move(joints)), joint_limits_(std::move(joint_limits)),
      link_radii_(std::move(link_radii)), tool_frame_(std::move(tool_frame)) {}

Result<SerialRobotModel> SerialRobotModel::create(std::string name, std::vector<DhJoint> joints,
                                                  std::vector<Interval> joint_limits,
                                                  std::vector<double> link_radii,
                                                  std::optional<DhJoint> tool_frame) {
    SerialRobotModel model(std::move(name), std::move(joints), std::move(joint_limits), std::move(link_radii),
                           std::move(tool_frame));
    auto status = model.validate();
    if (!status)
        return status.error();
    return model;
}

Result<void> SerialRobotModel::validate() const {
    if (name_.empty())
        return Result<void>::failure(StatusCode::InvalidArgument, "robot name must not be empty");
    if (joints_.empty() || joints_.size() > 64) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "robot must contain between 1 and 64 joints");
    }
    const std::size_t expected_links = joints_.size() + (tool_frame_ ? 1u : 0u);
    if (joint_limits_.size() != joints_.size() || link_radii_.size() != expected_links) {
        return Result<void>::failure(StatusCode::DimensionMismatch,
                                     "link radii must describe every joint link and the optional tool link");
    }
    for (std::size_t index = 0; index < joints_.size(); ++index) {
        if (!finite_joint(joints_[index])) {
            return Result<void>::failure(StatusCode::InvalidArgument, "joint contains invalid values",
                                         std::to_string(index));
        }
        if (!joint_limits_[index].valid()) {
            return Result<void>::failure(StatusCode::InvalidArgument, "joint limit is invalid",
                                         std::to_string(index));
        }
    }
    for (std::size_t index = 0; index < link_radii_.size(); ++index) {
        if (!std::isfinite(link_radii_[index]) || link_radii_[index] < 0.0) {
            return Result<void>::failure(StatusCode::InvalidArgument,
                                         "link radius must be finite and non-negative",
                                         std::to_string(index));
        }
    }
    if (tool_frame_ && !finite_joint(*tool_frame_)) {
        return Result<void>::failure(StatusCode::InvalidArgument, "tool frame contains invalid values");
    }
    return Result<void>::success();
}

Result<std::vector<std::array<double, 3>>>
SerialRobotModel::forward_kinematics(std::span<const double> configuration) const {
    auto model_status = validate();
    if (!model_status)
        return model_status.error();
    auto configuration_status = validate_configuration(configuration, dimension());
    if (!configuration_status)
        return configuration_status.error();
    for (std::size_t index = 0; index < dimension(); ++index) {
        if (!joint_limits_[index].contains(configuration[index], 1e-12)) {
            return Result<std::vector<std::array<double, 3>>>::failure(
                StatusCode::InvalidArgument, "configuration lies outside joint limits",
                std::to_string(index));
        }
    }

    std::vector<std::array<double, 3>> origins;
    origins.reserve(joints_.size() + 1 + (tool_frame_ ? 1 : 0));
    Matrix4 transform = identity_matrix();
    origins.push_back({0.0, 0.0, 0.0});
    for (std::size_t index = 0; index < joints_.size(); ++index) {
        transform = multiply(transform, joint_matrix(joints_[index], configuration[index]));
        origins.push_back({transform[3], transform[7], transform[11]});
    }
    if (tool_frame_) {
        transform = multiply(transform, joint_matrix(*tool_frame_, 0.0));
        origins.push_back({transform[3], transform[7], transform[11]});
    }
    return origins;
}

Result<Pose3d> SerialRobotModel::end_effector_pose(std::span<const double> configuration) const {
    auto model_status = validate();
    if (!model_status)
        return model_status.error();
    auto configuration_status = validate_configuration(configuration, dimension());
    if (!configuration_status)
        return configuration_status.error();
    for (std::size_t index = 0; index < dimension(); ++index) {
        if (!joint_limits_[index].contains(configuration[index], 1e-12)) {
            return Result<Pose3d>::failure(StatusCode::InvalidArgument,
                                           "configuration lies outside joint limits", std::to_string(index));
        }
    }

    Matrix4 transform = identity_matrix();
    for (std::size_t index = 0; index < joints_.size(); ++index)
        transform = multiply(transform, joint_matrix(joints_[index], configuration[index]));
    if (tool_frame_)
        transform = multiply(transform, joint_matrix(*tool_frame_, 0.0));
    return pose_from_matrix(transform);
}

std::string SerialRobotModel::canonical_json() const {
    internal::Json::Array joints;
    internal::Json::Array limits;
    internal::Json::Array radii;
    for (const auto& joint : joints_)
        joints.emplace_back(joint_json(joint));
    for (const auto& limit : joint_limits_)
        limits.emplace_back(internal::Json::Array{limit.lower, limit.upper});
    for (const auto radius : link_radii_)
        radii.emplace_back(radius);
    internal::Json::Object root{
        {"joint_limits", std::move(limits)},
        {"joints", std::move(joints)},
        {"link_radii", std::move(radii)},
        {"name", name_},
        {"schema", 1},
    };
    root.emplace("tool_frame",
                 tool_frame_ ? internal::Json(joint_json(*tool_frame_)) : internal::Json(nullptr));
    return internal::Json(std::move(root)).dump(false);
}

std::string SerialRobotModel::digest() const { return internal::sha256(canonical_json()); }

Result<SerialRobotModel> SerialRobotModel::from_json(const std::filesystem::path& path) {
    auto root = internal::read_json_file(path);
    if (!root)
        return root.error();
    if (!root.value().is_object()) {
        return Result<SerialRobotModel>::failure(StatusCode::CorruptData, "robot JSON root must be an object",
                                                 path.string());
    }
    const auto* schema = root.value().find("schema");
    if (schema == nullptr || !schema->is_number() || schema->as_number() != 1.0) {
        return Result<SerialRobotModel>::failure(StatusCode::IncompatibleFormat,
                                                 "unsupported robot JSON schema", path.string());
    }
    auto name = required_string(root.value(), "name");
    if (!name)
        return name.error();
    const auto* joints_json = root.value().find("joints");
    const auto* limits_json = root.value().find("joint_limits");
    const auto* radii_json = root.value().find("link_radii");
    if (joints_json == nullptr || !joints_json->is_array() || limits_json == nullptr ||
        !limits_json->is_array() || radii_json == nullptr || !radii_json->is_array()) {
        return Result<SerialRobotModel>::failure(StatusCode::CorruptData,
                                                 "robot arrays are missing or invalid", path.string());
    }
    std::vector<DhJoint> joints;
    for (const auto& json : joints_json->as_array()) {
        auto joint = parse_joint(json);
        if (!joint)
            return joint.error();
        joints.push_back(joint.value());
    }
    std::vector<Interval> limits;
    for (const auto& json : limits_json->as_array()) {
        if (!json.is_array() || json.as_array().size() != 2 || !json.as_array()[0].is_number() ||
            !json.as_array()[1].is_number()) {
            return Result<SerialRobotModel>::failure(StatusCode::CorruptData,
                                                     "joint limit must contain two numbers");
        }
        limits.emplace_back(json.as_array()[0].as_number(), json.as_array()[1].as_number());
    }
    std::vector<double> radii;
    for (const auto& json : radii_json->as_array()) {
        if (!json.is_number())
            return Result<SerialRobotModel>::failure(StatusCode::CorruptData, "link radius must be numeric");
        radii.push_back(json.as_number());
    }
    std::optional<DhJoint> tool;
    if (const auto* tool_json = root.value().find("tool_frame");
        tool_json != nullptr && !tool_json->is_null()) {
        auto parsed = parse_joint(*tool_json);
        if (!parsed)
            return parsed.error();
        tool = parsed.value();
    }
    return create(std::move(name).value(), std::move(joints), std::move(limits), std::move(radii), tool);
}

SceneSnapshot::SceneSnapshot(std::vector<SceneObstacle> obstacles, std::string version)
    : obstacles_(std::move(obstacles)), version_(std::move(version)) {
    std::sort(obstacles_.begin(), obstacles_.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
}

Result<SceneSnapshot> SceneSnapshot::create(std::vector<SceneObstacle> obstacles, std::string version) {
    SceneSnapshot scene(std::move(obstacles), std::move(version));
    auto status = scene.validate();
    if (!status)
        return status.error();
    return scene;
}

Result<void> SceneSnapshot::validate() const {
    if (version_.empty())
        return Result<void>::failure(StatusCode::InvalidArgument, "scene version must not be empty");
    std::set<std::string> ids;
    for (std::size_t index = 0; index < obstacles_.size(); ++index) {
        const auto& obstacle = obstacles_[index];
        if (obstacle.id.empty())
            return Result<void>::failure(StatusCode::InvalidArgument, "obstacle ID must not be empty");
        if (!ids.insert(obstacle.id).second) {
            return Result<void>::failure(StatusCode::InvalidArgument, "obstacle IDs must be unique",
                                         obstacle.id);
        }
        if (!obstacle.bounds.valid()) {
            return Result<void>::failure(StatusCode::InvalidArgument, "obstacle bounds are invalid",
                                         obstacle.id);
        }
    }
    return Result<void>::success();
}

std::string SceneSnapshot::canonical_json() const {
    internal::Json::Array obstacles;
    for (const auto& obstacle : obstacles_) {
        internal::Json::Array lower;
        internal::Json::Array upper;
        for (const auto value : obstacle.bounds.lower)
            lower.emplace_back(value);
        for (const auto value : obstacle.bounds.upper)
            upper.emplace_back(value);
        obstacles.emplace_back(internal::Json::Object{
            {"id", obstacle.id}, {"lower", std::move(lower)}, {"upper", std::move(upper)}});
    }
    return internal::Json(internal::Json::Object{
                              {"obstacles", std::move(obstacles)}, {"schema", 1}, {"version", version_}})
        .dump(false);
}

std::string SceneSnapshot::digest() const { return internal::sha256(canonical_json()); }

Result<SceneSnapshot> SceneSnapshot::from_json(const std::filesystem::path& path) {
    auto root = internal::read_json_file(path);
    if (!root)
        return root.error();
    if (!root.value().is_object())
        return Result<SceneSnapshot>::failure(StatusCode::CorruptData, "scene JSON root must be object");
    const auto* schema = root.value().find("schema");
    if (schema == nullptr || !schema->is_number() || schema->as_number() != 1.0) {
        return Result<SceneSnapshot>::failure(StatusCode::IncompatibleFormat, "unsupported scene JSON schema",
                                              path.string());
    }
    auto version = required_string(root.value(), "version");
    if (!version)
        return version.error();
    const auto* obstacles_json = root.value().find("obstacles");
    if (obstacles_json == nullptr || !obstacles_json->is_array()) {
        return Result<SceneSnapshot>::failure(StatusCode::CorruptData, "scene obstacles must be an array");
    }
    std::vector<SceneObstacle> obstacles;
    for (const auto& json : obstacles_json->as_array()) {
        if (!json.is_object())
            return Result<SceneSnapshot>::failure(StatusCode::CorruptData, "obstacle must be object");
        auto id = required_string(json, "id");
        if (!id)
            return id.error();
        const auto* lower = json.find("lower");
        const auto* upper = json.find("upper");
        if (lower == nullptr || upper == nullptr || !lower->is_array() || !upper->is_array() ||
            lower->as_array().size() != 3 || upper->as_array().size() != 3) {
            return Result<SceneSnapshot>::failure(StatusCode::CorruptData,
                                                  "obstacle bounds must be length-three arrays", id.value());
        }
        WorkspaceAabb bounds;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!lower->as_array()[axis].is_number() || !upper->as_array()[axis].is_number()) {
                return Result<SceneSnapshot>::failure(StatusCode::CorruptData,
                                                      "obstacle bound must be numeric", id.value());
            }
            bounds.lower[axis] = lower->as_array()[axis].as_number();
            bounds.upper[axis] = upper->as_array()[axis].as_number();
        }
        obstacles.push_back({std::move(id).value(), bounds});
    }
    return create(std::move(obstacles), std::move(version).value());
}

} // namespace rbfsafe
