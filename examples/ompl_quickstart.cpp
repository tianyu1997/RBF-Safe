#include <rbfsafe/ompl.h>

#include <iostream>
#include <memory>

int main() {
    using namespace rbfsafe;

    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot)
        return 1;
    SceneSnapshot scene({}, "ompl-quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built)
        return 1;

    auto atlas = std::make_shared<SafeAtlas>(std::move(built.value().atlas));
    OmplPlannerOptions options;
    options.planner = OmplPlannerKind::BitStar;
    auto planned = OmplPlanner{}.solve(atlas, Configuration{-1.0, -1.0}, Configuration{1.0, 1.0}, options);
    if (!planned || planned.value().status != OmplPlanStatus::CertifiedExactSolution)
        return 1;

    std::cout << "certified BIT* path with " << planned.value().path.size()
              << " states; certified motions=" << planned.value().stats.adapter.certified_motions << '\n';
    return 0;
}
