#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    using namespace rbfsafe;
    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot) {
        std::cerr << robot.error().describe() << '\n';
        return 1;
    }
    const SceneSnapshot scene({}, "quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}, {1.0, -1.0}});
    if (!built) {
        std::cerr << built.error().describe() << '\n';
        return 1;
    }
    std::cout << "regions=" << built.value().atlas.regions().size()
              << " safe_origin=" << built.value().atlas.contains(Configuration{0.0, 0.0}) << '\n';
    const std::vector<Configuration> trajectory{{-1.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}};
    auto audit = TrajectoryAuditor{}.audit(built.value().atlas, trajectory);
    if (!audit) {
        std::cerr << audit.error().describe() << '\n';
        return 1;
    }
    std::cout << "trajectory_coverage=" << audit.value().coverage_ratio << '\n';
    if (argc == 2) {
        auto saved = built.value().atlas.save(std::filesystem::path(argv[1]));
        if (!saved) {
            std::cerr << saved.error().describe() << '\n';
            return 1;
        }
    }
    return 0;
}
