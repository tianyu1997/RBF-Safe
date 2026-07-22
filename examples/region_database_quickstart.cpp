#include <rbfsafe/region_database.h>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace rbfsafe;

    const SerialRobotModel robot(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    const SceneSnapshot scene({}, "region-database-empty-v1");
    auto result = ObbAtlasBuilder{}.build(robot, scene, {{-0.5, -0.2}, {0.0, 0.0}, {0.5, 0.2}});
    if (!result) {
        std::cerr << result.error().describe() << '\n';
        return 1;
    }
    auto& database = result.value().database;
    std::cout << "records=" << database.records().size()
              << " portals=" << result.value().stats.portal.portals_created
              << " origin_safe=" << database.contains(Configuration{0.0, 0.0}) << '\n';
    if (argc == 2) {
        auto saved = database.save(std::filesystem::path(argv[1]));
        if (!saved) {
            std::cerr << saved.error().describe() << '\n';
            return 1;
        }
    }
    return 0;
}
