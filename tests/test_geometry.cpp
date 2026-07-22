#include "test_support.h"

#include <array>
#include <filesystem>
#include <random>

int main() {
    using namespace rbfsafe;

    Interval interval{-2.0, 3.0};
    CHECK(interval.valid());
    CHECK(close(interval.width(), 5.0));
    CHECK(interval.contains(-2.0));
    CHECK(interval.overlaps({3.0, 4.0}));

    auto robot = planar_robot();
    CHECK(robot.validate());
    CHECK(robot.digest().size() == 64);
    CHECK(robot.digest() == robot.digest());
    auto loaded_robot =
        SerialRobotModel::from_json(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "planar_2r.json");
    CHECK(loaded_robot);
    CHECK(loaded_robot.value().dimension() == 2);
    auto loaded_scene =
        SceneSnapshot::from_json(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "empty_scene.json");
    CHECK(loaded_scene);
    CHECK(loaded_scene.value().obstacles().empty());
    auto point_fk = robot.forward_kinematics(Configuration{0.2, -0.3});
    CHECK(point_fk);
    CHECK(point_fk.value().size() == 3);
    auto end_pose = robot.end_effector_pose(Configuration{0.2, -0.3});
    CHECK(end_pose);
    CHECK(end_pose.value().valid());
    CHECK(close(end_pose.value().position[0], point_fk.value().back()[0]));
    CHECK(close(end_pose.value().position[1], point_fk.value().back()[1]));
    CHECK(close(end_pose.value().position[2], point_fk.value().back()[2]));
    CHECK(close(end_pose.value().orientation[2], std::sin(-0.05)));
    CHECK(close(end_pose.value().orientation[3], std::cos(-0.05)));

    CspaceAabb domain({{-0.7, 0.9}, {-0.5, 0.8}});
    auto envelope = compute_ifk_aa_link_envelope(robot, domain);
    CHECK(envelope);
    CHECK(envelope.value().links.size() == robot.dimension());

    // Property regression: every sampled endpoint must lie inside its
    // conservative AA endpoint-pair link box.
    std::mt19937_64 random(42);
    std::uniform_real_distribution<double> first(domain.axes()[0].lower, domain.axes()[0].upper);
    std::uniform_real_distribution<double> second(domain.axes()[1].lower, domain.axes()[1].upper);
    for (int sample = 0; sample < 10'000; ++sample) {
        Configuration q{first(random), second(random)};
        auto fk = robot.forward_kinematics(q);
        CHECK(fk);
        for (std::size_t link = 0; link < robot.dimension(); ++link) {
            const auto& bounds = envelope.value().links[link];
            for (std::size_t endpoint = link; endpoint <= link + 1; ++endpoint) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    CHECK(fk.value()[endpoint][axis] >= bounds.lower[axis] - 1e-12);
                    CHECK(fk.value()[endpoint][axis] <= bounds.upper[axis] + 1e-12);
                }
            }
        }
    }

    SceneSnapshot empty;
    IfkAaLinkAabbValidator validator;
    auto certified = validator.validate(robot, empty, domain);
    CHECK(certified);
    CHECK(certified.value().disposition == ValidationDisposition::CertifiedFree);
    auto certificate = make_region_certificate(robot, empty, validator, certified.value(), 0.0);
    CHECK(certificate);
    CHECK(certificate.value().level == EvidenceLevel::CertifiedRegion);
    CHECK(certificate.value().id.size() == 64);
    auto bound_certificate = make_region_certificate(robot, empty, domain, validator, certified.value(), 0.0);
    CHECK(bound_certificate);
    CHECK(bound_certificate.value().subject_digest.size() == 64);
    auto incomplete = certified.value();
    incomplete.envelope.links.clear();
    auto rejected_incomplete = make_region_certificate(robot, empty, domain, validator, incomplete, 0.0);
    CHECK(!rejected_incomplete);
    CHECK(rejected_incomplete.error().code == StatusCode::InternalError);

    SceneSnapshot blocked({{"block", {{0.4, -0.2, -0.2}, {1.2, 0.2, 0.2}}}}, "blocked-v1");
    CHECK(blocked.validate());
    auto undetermined = validator.validate(robot, blocked, domain);
    CHECK(undetermined);
    CHECK(undetermined.value().disposition == ValidationDisposition::Undetermined);
    CHECK(!make_region_certificate(robot, blocked, validator, undetermined.value(), 0.0));

    return EXIT_SUCCESS;
}
