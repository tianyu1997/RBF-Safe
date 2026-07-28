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

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;

    try {
        const std::filesystem::path output = argc > 1
                                                 ? std::filesystem::path(argv[1])
                                                 : std::filesystem::path("continuous-fleet-occupancy.json");
        if (std::filesystem::exists(output)) {
            std::cerr << "output already exists: " << output << '\n';
            return 2;
        }
        const auto robot = require(SerialRobotModel::create(
            "planar-2r",
            {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
            {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05}));
        const std::vector<TimedConfiguration> trajectory{
            {0, {-0.2, 0.1}},
            {32, {0.2, -0.1}},
        };
        ContinuousOccupancyBuildOptions build_options;
        build_options.maximum_normalized_joint_width = 0.05;
        const auto first =
            require(build_robot_trajectory_occupancy(robot, "fixture-cell-clock-v1", "fixture-cell-world",
                                                     "arm-a", {-4.0, 0.0, 0.0}, trajectory, build_options));
        const auto second =
            require(build_robot_trajectory_occupancy(robot, "fixture-cell-clock-v1", "fixture-cell-world",
                                                     "arm-b", {4.0, 0.0, 0.0}, trajectory, build_options));
        require(verify_robot_trajectory_occupancy(robot, first));
        require(verify_robot_trajectory_occupancy(robot, second));
        ContinuousFleetOccupancyOptions analysis_options;
        analysis_options.minimum_separation = 1.0;
        const auto bundle =
            require(ContinuousFleetOccupancyBundle::create({second, first}, analysis_options));
        require(bundle.save(output));
        std::cout << "bundle=" << bundle.id() << '\n'
                  << "report=" << bundle.report().id << '\n'
                  << "status=" << continuous_fleet_occupancy_status_name(bundle.report().status) << '\n'
                  << "occupancies=" << bundle.occupancies().size() << '\n'
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
