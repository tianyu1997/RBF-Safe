#include <rbfsafe_moveit/plugin_resources.hpp>

#include <geometry_msgs/msg/pose.hpp>
#include <moveit/kinematics_base/kinematics_base.hpp>
#include <moveit/robot_model/joint_model_group.hpp>
#include <moveit/robot_model/robot_model.hpp>
#include <moveit_msgs/msg/move_it_error_codes.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/logging.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe_moveit {
namespace {

rbfsafe::Pose3d to_rbfsafe_pose(const geometry_msgs::msg::Pose& pose) {
    return rbfsafe::Pose3d{{pose.position.x, pose.position.y, pose.position.z},
                           {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
}

geometry_msgs::msg::Pose to_moveit_pose(const rbfsafe::Pose3d& pose) {
    geometry_msgs::msg::Pose result;
    result.position.x = pose.position[0];
    result.position.y = pose.position[1];
    result.position.z = pose.position[2];
    result.orientation.x = pose.orientation[0];
    result.orientation.y = pose.orientation[1];
    result.orientation.z = pose.orientation[2];
    result.orientation.w = pose.orientation[3];
    return result;
}

void set_error(moveit_msgs::msg::MoveItErrorCodes& error_code, int32_t value) { error_code.val = value; }

} // namespace

class SafeIkKinematicsPlugin final : public kinematics::KinematicsBase {
  public:
    bool initialize(const rclcpp::Node::SharedPtr& node, const moveit::core::RobotModel& robot_model,
                    const std::string& group_name, const std::string& base_frame,
                    const std::vector<std::string>& tip_frames, double search_discretization) override {
        logger_ = node->get_logger();
        if (tip_frames.size() != 1) {
            RCLCPP_ERROR(logger_, "RBF-Safe Safe IK supports exactly one tip frame");
            return false;
        }
        const std::string parameter_namespace = "rbfsafe." + group_name;
        auto loaded = load_resources(node, parameter_namespace, group_name);
        if (!loaded.resources) {
            RCLCPP_ERROR(logger_, "RBF-Safe Safe IK initialization failed: %s", loaded.error.c_str());
            return false;
        }
        if (!loaded.resources->tip_link.empty() && loaded.resources->tip_link != tip_frames.front()) {
            RCLCPP_ERROR(logger_, "Configured tip_link does not match the MoveIt kinematics tip frame");
            return false;
        }
        std::string group_error;
        if (!validate_moveit_group(robot_model, *loaded.resources, group_error)) {
            RCLCPP_ERROR(logger_, "RBF-Safe Safe IK model mapping rejected: %s", group_error.c_str());
            return false;
        }

        resources_ = std::move(loaded.resources);
        joint_names_ = resources_->joint_names;
        link_names_ = tip_frames;
        setValues("robot_description", group_name, base_frame, tip_frames, search_discretization);
        return true;
    }

    bool getPositionIK(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
                       std::vector<double>& solution, moveit_msgs::msg::MoveItErrorCodes& error_code,
                       const kinematics::KinematicsQueryOptions& options) const override {
        (void)options;
        return solve(ik_pose, ik_seed_state, 0.0, {}, solution, {}, error_code);
    }

    bool searchPositionIK(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
                          double timeout, std::vector<double>& solution,
                          moveit_msgs::msg::MoveItErrorCodes& error_code,
                          const kinematics::KinematicsQueryOptions& options) const override {
        (void)options;
        return solve(ik_pose, ik_seed_state, timeout, {}, solution, {}, error_code);
    }

    bool searchPositionIK(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
                          double timeout, const std::vector<double>& consistency_limits,
                          std::vector<double>& solution, moveit_msgs::msg::MoveItErrorCodes& error_code,
                          const kinematics::KinematicsQueryOptions& options) const override {
        (void)options;
        return solve(ik_pose, ik_seed_state, timeout, consistency_limits, solution, {}, error_code);
    }

    bool searchPositionIK(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
                          double timeout, std::vector<double>& solution,
                          const IKCallbackFn& solution_callback,
                          moveit_msgs::msg::MoveItErrorCodes& error_code,
                          const kinematics::KinematicsQueryOptions& options) const override {
        (void)options;
        return solve(ik_pose, ik_seed_state, timeout, {}, solution, solution_callback, error_code);
    }

    bool searchPositionIK(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
                          double timeout, const std::vector<double>& consistency_limits,
                          std::vector<double>& solution, const IKCallbackFn& solution_callback,
                          moveit_msgs::msg::MoveItErrorCodes& error_code,
                          const kinematics::KinematicsQueryOptions& options) const override {
        (void)options;
        return solve(ik_pose, ik_seed_state, timeout, consistency_limits, solution, solution_callback,
                     error_code);
    }

    bool getPositionFK(const std::vector<std::string>& link_names, const std::vector<double>& joint_angles,
                       std::vector<geometry_msgs::msg::Pose>& poses) const override {
        if (!resources_ || joint_angles.size() != resources_->robot.dimension())
            return false;
        if (std::any_of(link_names.begin(), link_names.end(), [this](const std::string& link) {
                return link_names_.empty() || link != link_names_.front();
            }))
            return false;
        auto pose = resources_->robot.end_effector_pose(joint_angles);
        if (!pose)
            return false;
        poses.assign(link_names.size(), to_moveit_pose(pose.value()));
        return true;
    }

    const std::vector<std::string>& getJointNames() const override { return joint_names_; }
    const std::vector<std::string>& getLinkNames() const override { return link_names_; }

  private:
    bool solve(const geometry_msgs::msg::Pose& ik_pose, const std::vector<double>& ik_seed_state,
               double timeout, const std::vector<double>& consistency_limits, std::vector<double>& solution,
               const IKCallbackFn& solution_callback, moveit_msgs::msg::MoveItErrorCodes& error_code) const {
        solution.clear();
        if (!resources_ || ik_seed_state.size() != resources_->robot.dimension() || timeout < 0.0) {
            set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::INVALID_ROBOT_STATE);
            return false;
        }
        if (!consistency_limits.empty() && consistency_limits.size() != ik_seed_state.size()) {
            set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::INVALID_ROBOT_STATE);
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        rbfsafe::SafeIkSolver solver;
        auto report = solver.solve(resources_->robot, resources_->scene, resources_->atlas,
                                   to_rbfsafe_pose(ik_pose), ik_seed_state, resources_->safe_ik_options);
        if (!report || report.value().status != rbfsafe::SafeIkStatus::SafeConnected ||
            !report.value().connectivity_route) {
            set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::NO_IK_SOLUTION);
            return false;
        }
        if (timeout > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > timeout) {
            set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::TIMED_OUT);
            return false;
        }

        solution = report.value().solution;
        for (std::size_t index = 0; index < consistency_limits.size(); ++index) {
            if (consistency_limits[index] < 0.0 ||
                std::abs(solution[index] - ik_seed_state[index]) > consistency_limits[index]) {
                solution.clear();
                set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::NO_IK_SOLUTION);
                return false;
            }
        }
        if (solution_callback) {
            moveit_msgs::msg::MoveItErrorCodes callback_error;
            set_error(callback_error, moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
            solution_callback(ik_pose, solution, callback_error);
            if (callback_error.val != moveit_msgs::msg::MoveItErrorCodes::SUCCESS) {
                solution.clear();
                error_code = callback_error;
                return false;
            }
        }
        set_error(error_code, moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
        return true;
    }

    rclcpp::Logger logger_{rclcpp::get_logger("rbfsafe_moveit.safe_ik")};
    std::shared_ptr<const PluginResources> resources_;
    std::vector<std::string> joint_names_;
    std::vector<std::string> link_names_;
};

} // namespace rbfsafe_moveit

PLUGINLIB_EXPORT_CLASS(rbfsafe_moveit::SafeIkKinematicsPlugin, kinematics::KinematicsBase)
