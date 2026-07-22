#include <rbfsafe_moveit/plugin_resources.hpp>

#include <moveit/planning_interface/planning_request_adapter.hpp>
#include <moveit/planning_interface/planning_response_adapter.hpp>
#include <moveit/planning_scene/planning_scene.hpp>
#include <moveit/robot_trajectory/robot_trajectory.hpp>
#include <moveit_msgs/msg/move_it_error_codes.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/logging.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe_moveit {
namespace {

moveit::core::MoveItErrorCode error_code(int32_t value) { return moveit::core::MoveItErrorCode(value); }

} // namespace

class CertifiedStartStateAdapter final : public planning_interface::PlanningRequestAdapter {
  public:
    void initialize(const rclcpp::Node::SharedPtr& node, const std::string& parameter_namespace) override {
        logger_ = node->get_logger();
        auto loaded = load_resources(node, parameter_namespace);
        resources_ = std::move(loaded.resources);
        initialization_error_ = std::move(loaded.error);
        register_resources(resources_);
        if (!resources_)
            RCLCPP_ERROR(logger_, "RBF-Safe request adapter is unavailable: %s",
                         initialization_error_.c_str());
    }

    [[nodiscard]] std::string getDescription() const override {
        return "RBF-Safe certified start-state gate";
    }

    [[nodiscard]] moveit::core::MoveItErrorCode
    adapt(const planning_scene::PlanningSceneConstPtr& planning_scene,
          planning_interface::MotionPlanRequest& request) const override {
        if (!resources_ || !planning_scene) {
            RCLCPP_ERROR(logger_, "RBF-Safe request adapter cannot validate this request: %s",
                         initialization_error_.c_str());
            return error_code(moveit_msgs::msg::MoveItErrorCodes::FAILURE);
        }
        if (request.group_name != resources_->group_name) {
            RCLCPP_ERROR(logger_, "RBF-Safe adapter expected group '%s', received '%s'",
                         resources_->group_name.c_str(), request.group_name.c_str());
            return error_code(moveit_msgs::msg::MoveItErrorCodes::INVALID_GROUP_NAME);
        }
        std::string group_error;
        if (!validate_moveit_group(*planning_scene->getRobotModel(), *resources_, group_error)) {
            RCLCPP_ERROR(logger_, "RBF-Safe model mapping rejected: %s", group_error.c_str());
            return error_code(moveit_msgs::msg::MoveItErrorCodes::INVALID_ROBOT_STATE);
        }
        const auto start_state = planning_scene->getCurrentStateUpdated(request.start_state);
        if (!start_state) {
            RCLCPP_ERROR(logger_, "MoveIt did not provide a usable start state");
            return error_code(moveit_msgs::msg::MoveItErrorCodes::INVALID_ROBOT_STATE);
        }
        const auto start = extract_group_positions(*start_state, *resources_);
        if (start.size() != resources_->robot.dimension() || !resources_->atlas.contains(start)) {
            RCLCPP_ERROR(logger_, "Planning start state is not covered by the configured SafeAtlas");
            return error_code(moveit_msgs::msg::MoveItErrorCodes::START_STATE_INVALID);
        }
        return error_code(moveit_msgs::msg::MoveItErrorCodes::SUCCESS);
    }

  private:
    rclcpp::Logger logger_{rclcpp::get_logger("rbfsafe_moveit.request_adapter")};
    std::shared_ptr<const PluginResources> resources_;
    std::string initialization_error_ = "adapter has not been initialized";
};

class CertifiedTrajectoryAdapter final : public planning_interface::PlanningResponseAdapter {
  public:
    void initialize(const rclcpp::Node::SharedPtr& node, const std::string& parameter_namespace) override {
        logger_ = node->get_logger();
        auto loaded = load_resources(node, parameter_namespace);
        resources_ = std::move(loaded.resources);
        initialization_error_ = std::move(loaded.error);
        register_resources(resources_);
        if (!resources_)
            RCLCPP_ERROR(logger_, "RBF-Safe response adapter is unavailable: %s",
                         initialization_error_.c_str());
    }

    [[nodiscard]] std::string getDescription() const override { return "RBF-Safe certified trajectory gate"; }

    void adapt(const planning_scene::PlanningSceneConstPtr& planning_scene,
               const planning_interface::MotionPlanRequest& request,
               planning_interface::MotionPlanResponse& response) const override {
        if (!response)
            return;
        if (!resources_ || !planning_scene || !response.trajectory ||
            request.group_name != resources_->group_name) {
            reject(response,
                   initialization_error_.empty() ? "invalid MoveIt response context" : initialization_error_);
            return;
        }
        std::string group_error;
        if (!validate_moveit_group(*planning_scene->getRobotModel(), *resources_, group_error)) {
            reject(response, group_error);
            return;
        }

        std::vector<rbfsafe::Configuration> trajectory;
        trajectory.reserve(response.trajectory->getWayPointCount());
        for (std::size_t index = 0; index < response.trajectory->getWayPointCount(); ++index) {
            auto configuration =
                extract_group_positions(response.trajectory->getWayPoint(index), *resources_);
            if (configuration.size() != resources_->robot.dimension()) {
                reject(response, "trajectory waypoint dimension does not match the SafeAtlas");
                return;
            }
            trajectory.push_back(std::move(configuration));
        }

        rbfsafe::TrajectoryAuditor auditor;
        auto report = auditor.audit(resources_->atlas, trajectory, resources_->audit_options);
        if (!report) {
            reject(response, "trajectory audit failed: " + report.error().describe());
            return;
        }
        if (report.value().status != rbfsafe::TrajectoryAuditStatus::Certified) {
            reject(response, "trajectory is not completely covered by certified SafeAtlas regions");
            return;
        }
    }

  private:
    void reject(planning_interface::MotionPlanResponse& response, const std::string& reason) const {
        RCLCPP_ERROR(logger_, "RBF-Safe rejected a MoveIt trajectory: %s", reason.c_str());
        response.trajectory.reset();
        response.error_code = error_code(moveit_msgs::msg::MoveItErrorCodes::INVALID_MOTION_PLAN);
    }

    rclcpp::Logger logger_{rclcpp::get_logger("rbfsafe_moveit.response_adapter")};
    std::shared_ptr<const PluginResources> resources_;
    std::string initialization_error_ = "adapter has not been initialized";
};

} // namespace rbfsafe_moveit

PLUGINLIB_EXPORT_CLASS(rbfsafe_moveit::CertifiedStartStateAdapter, planning_interface::PlanningRequestAdapter)
PLUGINLIB_EXPORT_CLASS(rbfsafe_moveit::CertifiedTrajectoryAdapter,
                       planning_interface::PlanningResponseAdapter)
