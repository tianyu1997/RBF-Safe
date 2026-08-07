#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

template <typename T> T require(rbfsafe::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
    return std::move(result).value();
}

void require(rbfsafe::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
}

rbfsafe::WorkspaceAabb box(double lower_x, double upper_x) {
    return {{lower_x, -0.25, -0.25}, {upper_x, 0.25, 0.25}};
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;

    try {
        const std::filesystem::path output =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("continuous-robot-scene-occupancy.json");
        if (std::filesystem::exists(output)) {
            std::cerr << "output already exists: " << output << '\n';
            return 2;
        }
        const auto robot = require(SerialRobotModel::create(
            "planar-2r",
            {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
            {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05}));
        const std::vector<TimedConfiguration> robot_trajectory{
            {0, {-0.2, 0.1}},
            {32, {0.2, -0.1}},
        };
        ContinuousOccupancyBuildOptions robot_options;
        robot_options.maximum_normalized_joint_width = 0.05;
        const auto robot_occupancy = require(
            build_robot_trajectory_occupancy(robot, "fixture-scene-clock-v1", "fixture-scene-world", "arm-a",
                                             {-4.0, 0.0, 0.0}, robot_trajectory, robot_options));
        require(verify_robot_trajectory_occupancy(robot, robot_occupancy));

        const std::vector<TimedWorkspaceAabb> obstacle_trajectory{
            {0, box(5.0, 5.5)},
            {16, box(6.0, 6.5)},
            {32, box(5.0, 5.5)},
        };
        MovingObstacleOccupancyBuildOptions obstacle_options;
        obstacle_options.obstacle_padding = 0.02;
        const auto obstacle =
            require(build_moving_obstacle_occupancy("fixture-scene-clock-v1", "fixture-scene-world", "cart-a",
                                                    obstacle_trajectory, obstacle_options));
        require(verify_moving_obstacle_occupancy(obstacle));

        ContinuousRobotSceneOccupancyOptions analysis_options;
        analysis_options.minimum_separation = 1.0;
        const auto bundle = require(
            ContinuousRobotSceneOccupancyBundle::create({robot_occupancy}, {obstacle}, analysis_options));
        require(bundle.save(output));
        std::cout << "bundle=" << bundle.id() << '\n'
                  << "report=" << bundle.report().id << '\n'
                  << "status=" << continuous_robot_scene_occupancy_status_name(bundle.report().status) << '\n'
                  << "robots=" << bundle.robot_occupancies().size() << '\n'
                  << "obstacles=" << bundle.obstacle_occupancies().size() << '\n'
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
