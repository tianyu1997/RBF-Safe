#include <rbfsafe_moveit/plugin_resources.hpp>

#include <moveit/robot_model/joint_model_group.hpp>
#include <moveit/robot_state/robot_state.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe_moveit {
namespace {

std::string parameter_name(const std::string& parameter_namespace, const std::string& leaf) {
    if (parameter_namespace.empty())
        return leaf;
    if (parameter_namespace.back() == '.')
        return parameter_namespace + leaf;
    return parameter_namespace + "." + leaf;
}

template <typename T>
T parameter(const rclcpp::Node::SharedPtr& node, const std::string& name, T default_value) {
    if (!node->has_parameter(name))
        return node->declare_parameter<T>(name, std::move(default_value));
    T value{};
    if (!node->get_parameter(name, value))
        return default_value;
    return value;
}

ResourceLoadResult failure(std::string message) { return ResourceLoadResult{nullptr, std::move(message)}; }

std::mutex registry_mutex;
std::map<std::string, std::vector<std::shared_ptr<const PluginResources>>> registry;

bool same_resource_identity(const PluginResources& left, const PluginResources& right) {
    if (left.robot.digest() != right.robot.digest() || left.scene.digest() != right.scene.digest()) {
        return false;
    }
    const auto& left_version = left.atlas.version_info().id;
    const auto& right_version = right.atlas.version_info().id;
    if (!left_version.empty() || !right_version.empty())
        return left_version == right_version;
    if (left.atlas.dimension() != right.atlas.dimension() ||
        left.atlas.regions().size() != right.atlas.regions().size() ||
        left.atlas.certificates().size() != right.atlas.certificates().size() ||
        left.atlas.adjacency() != right.atlas.adjacency()) {
        return false;
    }
    for (std::size_t index = 0; index < left.atlas.certificates().size(); ++index) {
        if (left.atlas.certificates()[index].id != right.atlas.certificates()[index].id)
            return false;
    }
    for (std::size_t index = 0; index < left.atlas.regions().size(); ++index) {
        const auto& left_region = left.atlas.regions()[index];
        const auto& right_region = right.atlas.regions()[index];
        if (left_region.id != right_region.id ||
            left_region.certificate_index != right_region.certificate_index ||
            left_region.component != right_region.component) {
            return false;
        }
    }
    return true;
}

bool same_registration(const PluginResources& left, const PluginResources& right) {
    return same_resource_identity(left, right) && left.joint_names == right.joint_names &&
           left.sampler_options.policy == right.sampler_options.policy &&
           left.sampler_options.seed == right.sampler_options.seed &&
           left.sampler_options.maximum_attempts == right.sampler_options.maximum_attempts &&
           left.roadmap_bias == right.roadmap_bias && left.roadmap_jitter == right.roadmap_jitter;
}

} // namespace

ResourceLoadResult load_resources(const rclcpp::Node::SharedPtr& node, const std::string& parameter_namespace,
                                  const std::string& expected_group) {
    if (!node)
        return failure("ROS node is null");

    const auto robot_path =
        parameter<std::string>(node, parameter_name(parameter_namespace, "robot_model_path"), "");
    const auto scene_path =
        parameter<std::string>(node, parameter_name(parameter_namespace, "scene_path"), "");
    const auto atlas_path =
        parameter<std::string>(node, parameter_name(parameter_namespace, "atlas_path"), "");
    const auto configured_group =
        parameter<std::string>(node, parameter_name(parameter_namespace, "group_name"), expected_group);
    const auto joint_names =
        parameter<std::vector<std::string>>(node, parameter_name(parameter_namespace, "joint_names"), {});
    const auto tip_link = parameter<std::string>(node, parameter_name(parameter_namespace, "tip_link"), "");

    if (robot_path.empty() || scene_path.empty() || atlas_path.empty())
        return failure("robot_model_path, scene_path, and atlas_path are required");
    if (configured_group.empty())
        return failure("group_name is required");
    if (!expected_group.empty() && configured_group != expected_group)
        return failure("configured group_name does not match the MoveIt planning group");
    if (joint_names.empty())
        return failure("joint_names must explicitly map every DH joint to the MoveIt group");
    if (tip_link.empty())
        return failure("tip_link is required");
    auto sorted_joint_names = joint_names;
    std::sort(sorted_joint_names.begin(), sorted_joint_names.end());
    if (std::adjacent_find(sorted_joint_names.begin(), sorted_joint_names.end()) != sorted_joint_names.end())
        return failure("joint_names must be unique");

    auto robot = rbfsafe::SerialRobotModel::from_json(std::filesystem::path(robot_path));
    if (!robot)
        return failure("cannot load RBF-Safe robot model: " + robot.error().describe());
    auto scene = rbfsafe::SceneSnapshot::from_json(std::filesystem::path(scene_path));
    if (!scene)
        return failure("cannot load RBF-Safe scene: " + scene.error().describe());
    auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(atlas_path));
    if (!atlas)
        return failure("cannot load SafeAtlas: " + atlas.error().describe());
    auto compatible = atlas.value().verify_compatible(robot.value(), scene.value());
    if (!compatible)
        return failure("SafeAtlas identity check failed: " + compatible.error().describe());
    if (joint_names.size() != robot.value().dimension())
        return failure("joint_names count does not match the RBF-Safe robot dimension");

    auto resources = std::make_shared<PluginResources>();
    resources->robot = std::move(robot).value();
    resources->scene = std::move(scene).value();
    resources->atlas = std::move(atlas).value();
    resources->group_name = configured_group;
    resources->joint_names = joint_names;
    resources->tip_link = tip_link;

    const double position_tolerance =
        parameter<double>(node, parameter_name(parameter_namespace, "position_tolerance"), 1e-4);
    const double orientation_tolerance =
        parameter<double>(node, parameter_name(parameter_namespace, "orientation_tolerance"), 1e-3);
    const int64_t maximum_iterations =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "maximum_ik_iterations"), 128);
    const int64_t maximum_region_attempts =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "maximum_region_attempts"), 256);
    const int64_t maximum_audit_region_tests = parameter<int64_t>(
        node, parameter_name(parameter_namespace, "maximum_audit_region_tests"), 10'000'000);
    const int64_t sampler_seed =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "sampler_seed"), 42);
    const int64_t maximum_sampling_attempts =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "maximum_sampling_attempts"), 64);
    const std::string sampling_policy = parameter<std::string>(
        node, parameter_name(parameter_namespace, "sampling_policy"), "volume_weighted");
    const double roadmap_bias =
        parameter<double>(node, parameter_name(parameter_namespace, "roadmap_bias"), 0.25);
    const double roadmap_jitter =
        parameter<double>(node, parameter_name(parameter_namespace, "roadmap_jitter"), 0.05);
    const int64_t maximum_roadmap_nodes =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "maximum_roadmap_nodes"), 1'000'000);
    const int64_t maximum_roadmap_edges =
        parameter<int64_t>(node, parameter_name(parameter_namespace, "maximum_roadmap_edges"), 2'000'000);
    if (!std::isfinite(position_tolerance) || position_tolerance < 0.0 ||
        !std::isfinite(orientation_tolerance) || orientation_tolerance < 0.0 || maximum_iterations < 0 ||
        maximum_iterations > 10'000 || maximum_region_attempts <= 0 || maximum_region_attempts > 1'000'000 ||
        maximum_audit_region_tests <= 0 || sampler_seed < 0 || maximum_sampling_attempts <= 0 ||
        maximum_sampling_attempts > 1'000'000 || !std::isfinite(roadmap_bias) || roadmap_bias < 0.0 ||
        roadmap_bias > 1.0 || !std::isfinite(roadmap_jitter) || roadmap_jitter < 0.0 ||
        maximum_roadmap_nodes <= 0 || maximum_roadmap_edges <= 0)
        return failure("Safe IK, audit, or certified-sampling options are invalid");
    if (sampling_policy == "uniform_regions") {
        resources->sampler_options.policy = rbfsafe::CertifiedSamplingPolicy::UniformRegions;
    } else if (sampling_policy == "volume_weighted") {
        resources->sampler_options.policy = rbfsafe::CertifiedSamplingPolicy::VolumeWeighted;
    } else {
        return failure("sampling_policy must be 'uniform_regions' or 'volume_weighted'");
    }

    resources->safe_ik_options.position_tolerance = position_tolerance;
    resources->safe_ik_options.orientation_tolerance = orientation_tolerance;
    resources->safe_ik_options.maximum_iterations = static_cast<std::size_t>(maximum_iterations);
    resources->safe_ik_options.maximum_region_attempts = static_cast<std::size_t>(maximum_region_attempts);
    resources->safe_ik_options.require_connectivity = true;
    resources->audit_options.maximum_region_tests = static_cast<std::size_t>(maximum_audit_region_tests);
    resources->sampler_options.seed = static_cast<std::uint64_t>(sampler_seed);
    resources->sampler_options.maximum_attempts = static_cast<std::size_t>(maximum_sampling_attempts);
    resources->roadmap_bias = roadmap_bias;
    resources->roadmap_jitter = roadmap_jitter;
    rbfsafe::CertifiedRoadmapOptions roadmap_options;
    roadmap_options.maximum_nodes = static_cast<std::size_t>(maximum_roadmap_nodes);
    roadmap_options.maximum_edges = static_cast<std::size_t>(maximum_roadmap_edges);
    auto roadmap = rbfsafe::CertifiedRoadmapBuilder{}.build(resources->atlas, roadmap_options);
    if (!roadmap)
        return failure("cannot build certified roadmap: " + roadmap.error().describe());
    resources->roadmap = std::move(roadmap).value().roadmap;
    return ResourceLoadResult{std::move(resources), {}};
}

bool validate_moveit_group(const moveit::core::RobotModel& robot_model, const PluginResources& resources,
                           std::string& error) {
    const auto* group = robot_model.getJointModelGroup(resources.group_name);
    if (group == nullptr) {
        error = "MoveIt robot model has no joint group named " + resources.group_name;
        return false;
    }
    if (group->getVariableNames() != resources.joint_names) {
        error = "MoveIt group variable order does not exactly match configured joint_names";
        return false;
    }

    constexpr double limit_tolerance = 1e-9;
    for (std::size_t index = 0; index < resources.joint_names.size(); ++index) {
        const auto& moveit_bounds = robot_model.getVariableBounds(resources.joint_names[index]);
        const auto& rbfsafe_bounds = resources.robot.joint_limits()[index];
        if (!moveit_bounds.position_bounded_ ||
            std::abs(moveit_bounds.min_position_ - rbfsafe_bounds.lower) > limit_tolerance ||
            std::abs(moveit_bounds.max_position_ - rbfsafe_bounds.upper) > limit_tolerance) {
            error = "MoveIt and RBF-Safe joint limits differ for " + resources.joint_names[index];
            return false;
        }
    }
    return true;
}

rbfsafe::Configuration extract_group_positions(const moveit::core::RobotState& state,
                                               const PluginResources& resources) {
    rbfsafe::Configuration result;
    const auto* group = state.getRobotModel()->getJointModelGroup(resources.group_name);
    if (group != nullptr)
        state.copyJointGroupPositions(group, result);
    return result;
}

void register_resources(std::shared_ptr<const PluginResources> resources) {
    if (!resources)
        return;
    std::lock_guard lock(registry_mutex);
    auto& candidates = registry[resources->group_name];
    const auto duplicate = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return same_registration(*candidate, *resources);
    });
    if (duplicate == candidates.end())
        candidates.push_back(std::move(resources));
}

std::shared_ptr<const PluginResources> registered_resources(const std::string& group_name,
                                                            const moveit::core::RobotModel& robot_model) {
    std::lock_guard lock(registry_mutex);
    const auto found = registry.find(group_name);
    if (found == registry.end())
        return {};
    std::shared_ptr<const PluginResources> selected;
    for (const auto& candidate : found->second) {
        std::string error;
        if (!validate_moveit_group(robot_model, *candidate, error))
            continue;
        if (selected && !same_resource_identity(*selected, *candidate)) {
            return {};
        }
        selected = candidate;
    }
    return selected;
}

} // namespace rbfsafe_moveit
