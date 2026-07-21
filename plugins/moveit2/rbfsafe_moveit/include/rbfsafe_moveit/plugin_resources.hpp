#pragma once

#include <rbfsafe/rbfsafe.h>

#include <moveit/robot_model/robot_model.hpp>
#include <rclcpp/node.hpp>

#include <memory>
#include <string>
#include <vector>

namespace rbfsafe_moveit {

struct PluginResources {
    rbfsafe::SerialRobotModel robot;
    rbfsafe::SceneSnapshot scene;
    rbfsafe::SafeAtlas atlas;
    std::string group_name;
    std::vector<std::string> joint_names;
    std::string tip_link;
    rbfsafe::SafeIkOptions safe_ik_options;
    rbfsafe::TrajectoryAuditOptions audit_options;
};

struct ResourceLoadResult {
    std::shared_ptr<const PluginResources> resources;
    std::string error;
};

ResourceLoadResult load_resources(const rclcpp::Node::SharedPtr& node, const std::string& parameter_namespace,
                                  const std::string& expected_group = {});

bool validate_moveit_group(const moveit::core::RobotModel& robot_model, const PluginResources& resources,
                           std::string& error);

rbfsafe::Configuration extract_group_positions(const moveit::core::RobotState& state,
                                               const PluginResources& resources);

} // namespace rbfsafe_moveit
