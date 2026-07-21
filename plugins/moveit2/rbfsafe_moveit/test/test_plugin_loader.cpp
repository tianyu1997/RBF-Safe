#include <rbfsafe/rbfsafe.h>

#include <geometry_msgs/msg/pose.hpp>
#include <moveit/kinematics_base/kinematics_base.hpp>
#include <moveit/planning_interface/planning_request_adapter.hpp>
#include <moveit/planning_interface/planning_response.hpp>
#include <moveit/planning_interface/planning_response_adapter.hpp>
#include <moveit/planning_scene/planning_scene.hpp>
#include <moveit/robot_model/robot_model.hpp>
#include <moveit/robot_state/robot_state.hpp>
#include <moveit/robot_trajectory/robot_trajectory.hpp>
#include <moveit_msgs/msg/move_it_error_codes.hpp>
#include <pluginlib/class_loader.hpp>
#include <rclcpp/node.hpp>
#include <srdfdom/model.h>
#include <urdf_parser/urdf_parser.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

template <typename Base> void expect_loadable(const std::string& base_name, const std::string& plugin_name) {
    pluginlib::ClassLoader<Base> loader("moveit_core", base_name);
    const std::vector<std::string> classes = loader.getDeclaredClasses();
    EXPECT_NE(std::find(classes.begin(), classes.end(), plugin_name), classes.end());
    EXPECT_NO_THROW({
        auto plugin = loader.createSharedInstance(plugin_name);
        EXPECT_NE(plugin, nullptr);
    });
}

TEST(MoveItPlugins, exported_classes_are_discoverable_and_constructible) {
    expect_loadable<planning_interface::PlanningRequestAdapter>("planning_interface::PlanningRequestAdapter",
                                                                "rbfsafe_moveit/CertifiedStartStateAdapter");
    expect_loadable<planning_interface::PlanningResponseAdapter>(
        "planning_interface::PlanningResponseAdapter", "rbfsafe_moveit/CertifiedTrajectoryAdapter");
    expect_loadable<kinematics::KinematicsBase>("kinematics::KinematicsBase",
                                                "rbfsafe_moveit/SafeIkKinematicsPlugin");
}

class TemporaryDirectory {
  public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("rbfsafe-moveit-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }

    std::filesystem::path path;
};

TEST(MoveItPlugins, safe_ik_plugin_solves_an_atlas_connected_pose) {
    TemporaryDirectory temporary;
    const auto robot_path = temporary.path / "robot.json";
    const auto scene_path = temporary.path / "scene.json";
    const auto atlas_path = temporary.path / "atlas";
    {
        std::ofstream robot_file(robot_path);
        robot_file << R"({
          "schema": 1,
          "name": "moveit-safe-ik-1r",
          "joints": [
            {"alpha": 0.0, "a": 1.0, "d": 0.0, "theta": 0.0, "type": "revolute"}
          ],
          "joint_limits": [[-1.0, 1.0]],
          "link_radii": [0.02],
          "tool_frame": null
        })";
        std::ofstream scene_file(scene_path);
        scene_file << R"({"schema": 1, "version": "moveit-test-v1", "obstacles": []})";
    }
    auto robot = rbfsafe::SerialRobotModel::from_json(robot_path);
    auto scene = rbfsafe::SceneSnapshot::from_json(scene_path);
    ASSERT_TRUE(robot) << robot.error().describe();
    ASSERT_TRUE(scene) << scene.error().describe();
    auto built = rbfsafe::AtlasBuilder{}.build(robot.value(), scene.value(), {{0.0}});
    ASSERT_TRUE(built) << built.error().describe();
    auto saved = built.value().atlas.save(atlas_path);
    ASSERT_TRUE(saved) << saved.error().describe();

    const std::string urdf_xml = R"(
      <robot name="moveit-safe-ik-1r">
        <link name="base"/>
        <link name="tool"/>
        <joint name="joint_1" type="revolute">
          <parent link="base"/>
          <child link="tool"/>
          <axis xyz="0 0 1"/>
          <limit lower="-1.0" upper="1.0" effort="1.0" velocity="1.0"/>
        </joint>
      </robot>)";
    const auto urdf_model = urdf::parseURDF(urdf_xml);
    ASSERT_NE(urdf_model, nullptr);
    auto srdf_model = std::make_shared<srdf::Model>();
    ASSERT_TRUE(srdf_model->initString(
        *urdf_model,
        R"(<robot name="moveit-safe-ik-1r"><group name="arm"><chain base_link="base" tip_link="tool"/></group></robot>)"));
    if (!rclcpp::ok()) {
        int argc = 0;
        char** argv = nullptr;
        rclcpp::init(argc, argv);
    }
    const auto moveit_model = std::make_shared<moveit::core::RobotModel>(urdf_model, srdf_model);
    rclcpp::NodeOptions node_options;
    node_options.parameter_overrides({
        rclcpp::Parameter("rbfsafe.arm.robot_model_path", robot_path.string()),
        rclcpp::Parameter("rbfsafe.arm.scene_path", scene_path.string()),
        rclcpp::Parameter("rbfsafe.arm.atlas_path", atlas_path.string()),
        rclcpp::Parameter("rbfsafe.arm.group_name", "arm"),
        rclcpp::Parameter("rbfsafe.arm.joint_names", std::vector<std::string>{"joint_1"}),
        rclcpp::Parameter("rbfsafe.arm.tip_link", "tool"),
        rclcpp::Parameter("test_pipeline.robot_model_path", robot_path.string()),
        rclcpp::Parameter("test_pipeline.scene_path", scene_path.string()),
        rclcpp::Parameter("test_pipeline.atlas_path", atlas_path.string()),
        rclcpp::Parameter("test_pipeline.group_name", "arm"),
        rclcpp::Parameter("test_pipeline.joint_names", std::vector<std::string>{"joint_1"}),
        rclcpp::Parameter("test_pipeline.tip_link", "tool"),
    });
    auto node = std::make_shared<rclcpp::Node>("rbfsafe_moveit_test", node_options);

    pluginlib::ClassLoader<kinematics::KinematicsBase> loader("moveit_core", "kinematics::KinematicsBase");
    auto plugin = loader.createSharedInstance("rbfsafe_moveit/SafeIkKinematicsPlugin");
    ASSERT_TRUE(plugin->initialize(node, *moveit_model, "arm", "base", {"tool"}, 0.1));

    constexpr double desired_joint = 0.5;
    auto desired_pose = robot.value().end_effector_pose(rbfsafe::Configuration{desired_joint});
    ASSERT_TRUE(desired_pose) << desired_pose.error().describe();
    geometry_msgs::msg::Pose target;
    target.position.x = desired_pose.value().position[0];
    target.position.y = desired_pose.value().position[1];
    target.position.z = desired_pose.value().position[2];
    target.orientation.x = desired_pose.value().orientation[0];
    target.orientation.y = desired_pose.value().orientation[1];
    target.orientation.z = desired_pose.value().orientation[2];
    target.orientation.w = desired_pose.value().orientation[3];
    std::vector<double> solution;
    moveit_msgs::msg::MoveItErrorCodes error_code;
    EXPECT_TRUE(plugin->getPositionIK(target, {0.0}, solution, error_code));
    EXPECT_EQ(error_code.val, moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    ASSERT_EQ(solution.size(), 1U);
    EXPECT_NEAR(solution.front(), desired_joint, 2e-4);

    std::vector<geometry_msgs::msg::Pose> poses;
    EXPECT_TRUE(plugin->getPositionFK({"tool"}, solution, poses));
    ASSERT_EQ(poses.size(), 1U);
    EXPECT_NEAR(poses.front().position.x, target.position.x, 1e-4);
    EXPECT_NEAR(poses.front().position.y, target.position.y, 1e-4);

    auto planning_scene = std::make_shared<planning_scene::PlanningScene>(moveit_model);
    planning_interface::MotionPlanRequest request;
    request.group_name = "arm";
    request.start_state.joint_state.name = {"joint_1"};
    request.start_state.joint_state.position = {0.0};
    pluginlib::ClassLoader<planning_interface::PlanningRequestAdapter> request_loader(
        "moveit_core", "planning_interface::PlanningRequestAdapter");
    auto request_adapter = request_loader.createSharedInstance("rbfsafe_moveit/CertifiedStartStateAdapter");
    request_adapter->initialize(node, "test_pipeline");
    EXPECT_EQ(request_adapter->adapt(planning_scene, request).val,
              moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    request.start_state.joint_state.position = {1.5};
    EXPECT_NE(request_adapter->adapt(planning_scene, request).val,
              moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    request.start_state.joint_state.position = {0.0};

    pluginlib::ClassLoader<planning_interface::PlanningResponseAdapter> response_loader(
        "moveit_core", "planning_interface::PlanningResponseAdapter");
    auto response_adapter = response_loader.createSharedInstance("rbfsafe_moveit/CertifiedTrajectoryAdapter");
    response_adapter->initialize(node, "test_pipeline");
    auto make_response = [&](double goal) {
        planning_interface::MotionPlanResponse response;
        response.error_code = moveit::core::MoveItErrorCode(moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
        response.trajectory = std::make_shared<robot_trajectory::RobotTrajectory>(moveit_model, "arm");
        moveit::core::RobotState start(moveit_model);
        start.setToDefaultValues();
        start.setJointGroupPositions("arm", std::vector<double>{0.0});
        start.update();
        moveit::core::RobotState finish(start);
        finish.setJointGroupPositions("arm", std::vector<double>{goal});
        finish.update();
        response.trajectory->addSuffixWayPoint(start, 0.0);
        response.trajectory->addSuffixWayPoint(finish, 1.0);
        return response;
    };
    auto certified_response = make_response(0.5);
    response_adapter->adapt(planning_scene, request, certified_response);
    EXPECT_TRUE(certified_response);
    EXPECT_NE(certified_response.trajectory, nullptr);
    auto rejected_response = make_response(1.5);
    response_adapter->adapt(planning_scene, request, rejected_response);
    EXPECT_FALSE(rejected_response);
    EXPECT_EQ(rejected_response.trajectory, nullptr);
    EXPECT_EQ(rejected_response.error_code.val, moveit_msgs::msg::MoveItErrorCodes::INVALID_MOTION_PLAN);
}

} // namespace
