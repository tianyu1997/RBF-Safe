#include <rbfsafe/rbfsafe.h>

#include <iostream>

int main() {
    using namespace rbfsafe;
    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot) {
        std::cerr << robot.error().describe() << '\n';
        return 1;
    }
    const SceneSnapshot scene({}, "safe-ik-quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built) {
        std::cerr << built.error().describe() << '\n';
        return 1;
    }
    auto target = robot.value().end_effector_pose(Configuration{0.4, -0.2});
    if (!target) {
        std::cerr << target.error().describe() << '\n';
        return 1;
    }
    auto report = SafeIkSolver{}.solve(robot.value(), scene, built.value().atlas, target.value(),
                                       Configuration{0.0, 0.0});
    if (!report) {
        std::cerr << report.error().describe() << '\n';
        return 1;
    }
    if (report.value().status != SafeIkStatus::SafeConnected || !report.value().connectivity_route) {
        std::cerr << "no Atlas-connected Safe IK solution\n";
        return 2;
    }
    std::cout << "solution=" << report.value().solution[0] << ',' << report.value().solution[1]
              << " region=" << report.value().region_id
              << " route_certificate=" << report.value().connectivity_route->certificate.id << '\n';
    return 0;
}
