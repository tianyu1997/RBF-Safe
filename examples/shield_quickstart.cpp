#include <rbfsafe/shield.h>

#include <cstdlib>
#include <iostream>

int main() {
    using namespace rbfsafe;
    SerialRobotModel robot("shield-demo", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}}, {{-1.0, 1.0}}, {0.02});
    SceneSnapshot scene({}, "empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{-0.5}, {0.5}});
    if (!built) {
        std::cerr << built.error().describe() << '\n';
        return EXIT_FAILURE;
    }

    RuntimeShield shield;
    auto decision = shield.check_action(robot, scene, built.value().atlas, Configuration{-0.5},
                                        ShieldAction{JointDeltaAction{{0.25}}});
    if (!decision) {
        std::cerr << decision.error().describe() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << shield_outcome_name(decision.value().outcome) << " decision=" << decision.value().id
              << " evidence=" << evidence_level_name(decision.value().evidence) << '\n';
    return decision.value().outcome == ShieldOutcome::Reject ? EXIT_FAILURE : EXIT_SUCCESS;
}
