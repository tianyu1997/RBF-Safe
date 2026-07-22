#include "test_support.h"

#include <algorithm>
#include <random>

int main() {
    using namespace rbfsafe;

    const SerialRobotModel robot("correlated-planar-3r",
                                 {{0.0, 1.0, 0.0, 0.0, JointType::Revolute},
                                  {0.0, 1.0, 0.0, 0.0, JointType::Revolute},
                                  {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
                                 {{-1.0, 1.0}, {-1.0, 1.0}, {-1.0, 1.0}}, {0.05, 0.05, 0.05});
    auto correlated = CspaceZonotope::create({0.0, 0.0, 0.0}, 1, {0.1, -0.1, 0.0});
    CHECK(correlated);
    CHECK(correlated.value().valid());
    CHECK(correlated.value().dimension() == 3);
    CHECK(correlated.value().generator_count() == 1);
    CHECK(correlated.value().enclosing_aabb().dimension() == 3);
    CHECK(correlated.value().enclosing_aabb().contains(Configuration{0.1, -0.1, 0.0}, 1e-15));
    CHECK(correlated.value().contains(Configuration{0.05, -0.05, 0.0}).value());
    CHECK(!correlated.value().contains(Configuration{0.05, 0.05, 0.0}).value());

    auto envelope = compute_ifk_zonotope_link_envelope(robot, correlated.value());
    CHECK(envelope);
    CHECK(envelope.value().links.size() == robot.link_count());
    auto independent = compute_ifk_aa_link_envelope(robot, correlated.value().enclosing_aabb());
    CHECK(independent);
    bool strictly_tighter = false;
    for (std::size_t link = 0; link < robot.link_count(); ++link) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double correlated_width =
                envelope.value().links[link].upper[axis] - envelope.value().links[link].lower[axis];
            const double independent_width =
                independent.value().links[link].upper[axis] - independent.value().links[link].lower[axis];
            if (correlated_width + 1e-12 < independent_width)
                strictly_tighter = true;
        }
    }
    CHECK(strictly_tighter);

    std::mt19937_64 engine(77);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    for (int sample = 0; sample < 10'000; ++sample) {
        const double variable = 0.1 * unit(engine);
        const Configuration configuration{variable, -variable, 0.0};
        CHECK(correlated.value().contains(configuration).value());
        const auto points = robot.forward_kinematics(configuration);
        CHECK(points);
        for (std::size_t link = 0; link < robot.link_count(); ++link) {
            for (std::size_t endpoint = link; endpoint <= link + 1; ++endpoint) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    CHECK(points.value()[endpoint][axis] >= envelope.value().links[link].lower[axis] - 1e-12);
                    CHECK(points.value()[endpoint][axis] <= envelope.value().links[link].upper[axis] + 1e-12);
                }
            }
        }
    }

    auto taylor = CspaceTaylorRegion::create({0.0, 0.0, 0.0}, 1, {0.4, -0.4, 0.0}, {0.05, 0.05, 0.0});
    CHECK(taylor);
    CHECK(taylor.value().valid());
    CHECK(taylor.value().contains(Configuration{0.4, -0.4, 0.0}).value());
    CHECK(taylor.value().contains(Configuration{0.45, -0.35, 0.0}).value());
    auto taylor_envelope = compute_ifk_taylor_link_envelope(robot, taylor.value());
    CHECK(taylor_envelope);
    const SceneSnapshot scene({}, "higher-order-empty-v1");
    HigherOrderRegionValidator validator;
    auto validation = validator.validate(robot, scene, taylor.value());
    CHECK(validation);
    CHECK(validation.value().disposition == ValidationDisposition::CertifiedFree);
    CHECK(validation.value().envelope.links.size() == robot.link_count());

    CHECK(!CspaceZonotope::create({}, 0, {}));
    CHECK(!CspaceZonotope::create({0.0, 0.0}, 1, {1.0}));
    CHECK(!CspaceTaylorRegion::create({0.0}, 0, {}, {-1.0}));
    CHECK(!correlated.value().contains(Configuration{0.0}, 1e-10));
    return EXIT_SUCCESS;
}
