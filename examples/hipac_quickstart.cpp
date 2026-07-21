#include <rbfsafe/corridor.h>

#include <iostream>
#include <vector>

int main() {
    using namespace rbfsafe;

    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    auto scene = SceneSnapshot::create({}, "empty-v1");
    if (!robot || !scene) {
        std::cerr << "failed to create inputs\n";
        return 1;
    }

    const std::vector<Configuration> path{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
    auto report = HipacCorridorBuilder{}.build(robot.value(), scene.value(), path);
    if (!report || report.value().status != HipacBuildStatus::Certified) {
        std::cerr << "failed to certify the path corridor\n";
        return 1;
    }
    auto route = report.value().corridor.route(Configuration{-0.5, -0.5}, Configuration{0.5, 0.5});
    if (!route || !route.value()) {
        std::cerr << "failed to recover a certified route\n";
        return 1;
    }

    std::cout << "cells=" << report.value().corridor.regions().size()
              << " portals=" << report.value().corridor.portals().size()
              << " route_waypoints=" << route.value()->waypoints.size() << '\n';
    return 0;
}
