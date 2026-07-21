#include "test_support.h"

#include <algorithm>

int main() {
    using namespace rbfsafe;
    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "empty-v1");
    const std::vector<Configuration> samples{{0.0, 0.0}, {1.0, -1.0}, {0.0, 0.0}};

    AtlasBuilder builder;
    BuildOptions serial;
    serial.threads = 1;
    auto first = builder.build(robot, scene, samples, serial);
    CHECK(first);
    CHECK(first.value().stats.input_samples == 3);
    CHECK(first.value().stats.unique_samples == 2);
    CHECK(first.value().atlas.regions().size() == 1);
    CHECK(first.value().atlas.certificates().size() == 1);
    CHECK(first.value().atlas.certificates()[0].policy.algorithm == "ifk-aa-link-iaabb");
    CHECK(first.value().atlas.contains(Configuration{0.0, 0.0}));
    CHECK(first.value().atlas.contains(Configuration{1.4, -1.4}));
    CHECK(first.value().atlas.contains(Configuration{-1.5, 1.5}));
    CHECK(first.value().atlas.connected(Configuration{0.0, 0.0}, Configuration{1.0, -1.0}).value());
    CHECK(first.value().atlas.verify_compatible(robot, scene));

    BuildOptions parallel;
    parallel.threads = 4;
    auto second = builder.build(robot, scene, samples, parallel);
    CHECK(second);
    CHECK(second.value().atlas.regions().size() == first.value().atlas.regions().size());
    CHECK(second.value().atlas.regions()[0].id == first.value().atlas.regions()[0].id);
    CHECK(second.value().atlas.regions()[0].component == first.value().atlas.regions()[0].component);

    auto nearest = first.value().atlas.nearest_region(Configuration{2.0, 2.0});
    CHECK(nearest);
    CHECK(nearest.value().has_value());

    CHECK(!builder.build(robot, scene, {}));
    auto wrong_dimension = builder.build(robot, scene, {{0.0, 0.0, 0.0}});
    CHECK(!wrong_dimension);
    CHECK(wrong_dimension.error().code == StatusCode::DimensionMismatch);
    BuildOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_result = builder.build(robot, scene, {{0.0, 0.0}}, cancelled);
    CHECK(!cancelled_result);
    CHECK(cancelled_result.error().code == StatusCode::Cancelled);

    SceneSnapshot changed_scene({}, "empty-v2");
    auto mismatch = first.value().atlas.verify_compatible(robot, changed_scene);
    CHECK(!mismatch);
    CHECK(mismatch.error().code == StatusCode::IdentityMismatch);

    // A non-trivial one-dimensional build: the low prismatic cell can be
    // certified while the sibling that reaches the obstacle remains unknown.
    SerialRobotModel prismatic("prismatic-1d", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}}, {{0.0, 2.0}},
                               {0.05});
    SceneSnapshot split_scene({{"high-block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, "split-v1");
    auto split = builder.build(prismatic, split_scene, {{0.25}});
    CHECK(split);
    CHECK(split.value().stats.nodes_visited == 3);
    CHECK(split.value().atlas.contains(Configuration{0.25}));
    CHECK(!split.value().atlas.contains(Configuration{1.5}));
    BuildOptions split_parallel;
    split_parallel.threads = 4;
    auto split_again = builder.build(prismatic, split_scene, {{0.25}}, split_parallel);
    CHECK(split_again);
    CHECK(split_again.value().atlas.regions().size() == split.value().atlas.regions().size());
    CHECK(split_again.value().atlas.regions()[0].id == split.value().atlas.regions()[0].id);
    BuildOptions constrained;
    constrained.maximum_nodes = 1;
    auto limited = builder.build(prismatic, split_scene, {{0.25}}, constrained);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);

    // Seeds on opposite sides of an uncertified angular band produce two
    // isolated certified components without implying a path between them.
    SerialRobotModel swing(
        "swing-2r", {{0.0, 0.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-0.01, 0.01}}, {0.0, 0.02});
    SceneSnapshot angular_block({{"center-block", {{0.75, -0.1, -0.1}, {0.95, 0.1, 0.1}}}},
                                "angular-block-v1");
    auto isolated = builder.build(swing, angular_block, {{-1.0, 0.0}, {1.0, 0.0}});
    CHECK(isolated);
    CHECK(isolated.value().atlas.contains(Configuration{-1.0, 0.0}));
    CHECK(isolated.value().atlas.contains(Configuration{1.0, 0.0}));
    auto disconnected = isolated.value().atlas.connected(Configuration{-1.0, 0.0}, Configuration{1.0, 0.0});
    CHECK(disconnected);
    CHECK(!disconnected.value());
    return EXIT_SUCCESS;
}
