#include <rbfsafe/dynamic.h>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace rbfsafe;
    const SerialRobotModel robot("dynamic-prismatic-1d", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}},
                                 {{0.0, 2.0}}, {0.05});
    const SceneSnapshot empty({}, "empty-v1");
    const SceneSnapshot blocked({{"block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, "blocked-v1");
    auto initial = AtlasBuilder{}.build(robot, empty, {{0.25}});
    if (!initial) {
        std::cerr << initial.error().describe() << '\n';
        return 1;
    }
    auto restricted = AtlasUpdater{}.update(robot, empty, blocked, initial.value().atlas);
    if (!restricted) {
        std::cerr << restricted.error().describe() << '\n';
        return 1;
    }
    std::cout << "invalidated=" << restricted.value().stats.regions_invalidated
              << " repaired=" << restricted.value().stats.repaired_regions
              << " low_safe=" << restricted.value().atlas.contains(Configuration{0.25})
              << " high_safe=" << restricted.value().atlas.contains(Configuration{1.5}) << '\n';

    const SceneSnapshot reopened({}, "empty-v2");
    auto recovered = AtlasUpdater{}.update(robot, blocked, reopened, restricted.value().atlas);
    if (!recovered) {
        std::cerr << recovered.error().describe() << '\n';
        return 1;
    }
    std::cout << "recovered_high=" << recovered.value().atlas.contains(Configuration{1.5})
              << " version=" << recovered.value().atlas.version_info().sequence << '\n';

    if (argc == 2) {
        auto store = AtlasVersionStore::create(std::filesystem::path(argv[1]), initial.value().atlas);
        if (!store) {
            std::cerr << store.error().describe() << '\n';
            return 1;
        }
        auto published = store.value().publish(restricted.value().atlas);
        if (published)
            published = store.value().publish(recovered.value().atlas);
        if (!published) {
            std::cerr << published.error().describe() << '\n';
            return 1;
        }
        std::cout << "stored_versions=" << store.value().versions().size() << '\n';
    }
    return 0;
}
