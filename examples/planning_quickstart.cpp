#include <rbfsafe/planning.h>

#include <iostream>
#include <memory>

int main() {
    using namespace rbfsafe;
    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot)
        return 1;
    const SceneSnapshot scene({}, "planning-quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built)
        return 1;
    auto atlas = std::make_shared<const SafeAtlas>(std::move(built).value().atlas);
    auto sampler = CertifiedRegionSampler::create(atlas, {.seed = 7});
    auto roadmap = CertifiedRoadmapBuilder{}.build(*atlas);
    if (!sampler || !roadmap)
        return 1;
    auto sample = sampler.value().sample();
    if (!sample)
        return 1;
    std::cout << "sampled certified q with " << sample.value().size()
              << " joints; roadmap nodes=" << roadmap.value().roadmap.nodes().size() << '\n';
    return 0;
}
