#include "test_support.h"

#include <algorithm>
#include <memory>
#include <string>

namespace {

class SplitValidator final : public rbfsafe::RegionValidator {
  public:
    explicit SplitValidator(bool gap) : gap_(gap) {}

    rbfsafe::Result<rbfsafe::RegionValidation> validate(const rbfsafe::SerialRobotModel& robot,
                                                        const rbfsafe::SceneSnapshot&,
                                                        const rbfsafe::CspaceAabb& domain) const override {
        const auto& axis = domain.axes().front();
        const bool certified =
            gap_ ? (axis.width() <= 0.5 && std::abs(axis.center()) >= 0.75) : axis.width() <= 1.0;
        if (!certified)
            return rbfsafe::RegionValidation{};
        auto envelope = rbfsafe::compute_ifk_aa_link_envelope(robot, domain);
        if (!envelope)
            return envelope.error();
        return rbfsafe::RegionValidation{rbfsafe::ValidationDisposition::CertifiedFree, 0.5,
                                         std::move(envelope).value()};
    }

    std::string algorithm_name() const override { return gap_ ? "test-gap" : "test-split"; }
    std::string algorithm_version() const override { return "1"; }

  private:
    bool gap_ = false;
};

rbfsafe::SerialRobotModel one_revolute_robot() {
    return rbfsafe::SerialRobotModel("safe-ik-1r", {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.0, 1.0}}, {0.02});
}

} // namespace

int main() {
    using namespace rbfsafe;
    const auto robot = one_revolute_robot();
    const SceneSnapshot scene({}, "safe-ik-empty-v1");

    AtlasBuilder connected_builder(std::make_shared<SplitValidator>(false));
    auto connected_atlas = connected_builder.build(robot, scene, {{-0.5}, {0.5}});
    CHECK(connected_atlas);
    CHECK(connected_atlas.value().atlas.regions().size() == 2);
    auto target = robot.end_effector_pose(Configuration{0.6});
    CHECK(target);

    SafeIkSolver solver;
    auto solved =
        solver.solve(robot, scene, connected_atlas.value().atlas, target.value(), Configuration{-0.5});
    CHECK(solved);
    CHECK(solved.value().status == SafeIkStatus::SafeConnected);
    CHECK(solved.value().pose_evidence == EvidenceLevel::PointChecked);
    CHECK(solved.value().region_certificate.has_value());
    CHECK(solved.value().region_certificate->level == EvidenceLevel::CertifiedRegion);
    CHECK(solved.value().connectivity_route.has_value());
    CHECK(solved.value().connectivity_route->certificate.level == EvidenceLevel::CertifiedConnectivity);
    CHECK(solved.value().position_error <= 1e-4);
    CHECK(solved.value().orientation_error <= 1e-3);
    CHECK(close(solved.value().solution[0], 0.6, 2e-4));
    CHECK(solved.value().connectivity_route->region_sequence.size() == 2);
    CHECK(solved.value().connectivity_route->waypoints.size() == 3);
    for (std::size_t segment = 0; segment < 2; ++segment) {
        const RegionId id = solved.value().connectivity_route->region_sequence[segment];
        const auto region = std::find_if(connected_atlas.value().atlas.regions().begin(),
                                         connected_atlas.value().atlas.regions().end(),
                                         [id](const SafeRegion& candidate) { return candidate.id == id; });
        CHECK(region != connected_atlas.value().atlas.regions().end());
        for (int sample = 0; sample <= 100; ++sample) {
            const double fraction = static_cast<double>(sample) / 100.0;
            Configuration point{solved.value().connectivity_route->waypoints[segment][0] +
                                fraction * (solved.value().connectivity_route->waypoints[segment + 1][0] -
                                            solved.value().connectivity_route->waypoints[segment][0])};
            CHECK(region->bounds.contains(point));
        }
    }

    auto repeated =
        solver.solve(robot, scene, connected_atlas.value().atlas, target.value(), Configuration{-0.5});
    CHECK(repeated);
    CHECK(repeated.value().solution == solved.value().solution);
    CHECK(repeated.value().connectivity_route->certificate.id ==
          solved.value().connectivity_route->certificate.id);

    AtlasBuilder gap_builder(std::make_shared<SplitValidator>(true));
    auto gap_atlas = gap_builder.build(robot, scene, {{-0.75}, {0.75}});
    CHECK(gap_atlas);
    CHECK(gap_atlas.value().atlas.regions().size() == 2);
    auto gap_target = robot.end_effector_pose(Configuration{0.8});
    CHECK(gap_target);
    auto unconnected =
        solver.solve(robot, scene, gap_atlas.value().atlas, gap_target.value(), Configuration{-0.8});
    CHECK(unconnected);
    CHECK(unconnected.value().status == SafeIkStatus::SafeUnconnected);
    CHECK(!unconnected.value().connectivity_route.has_value());
    CHECK(unconnected.value().region_certificate.has_value());

    auto uncertified_seed =
        solver.solve(robot, scene, gap_atlas.value().atlas, gap_target.value(), Configuration{0.0});
    CHECK(uncertified_seed);
    CHECK(uncertified_seed.value().status == SafeIkStatus::SeedNotCertified);

    Pose3d unreachable{{10.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    auto no_solution =
        solver.solve(robot, scene, connected_atlas.value().atlas, unreachable, Configuration{-0.5});
    CHECK(no_solution);
    CHECK(no_solution.value().status == SafeIkStatus::NoSolution);

    SafeIkOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancellation = solver.solve(robot, scene, connected_atlas.value().atlas, target.value(),
                                     Configuration{-0.5}, cancelled);
    CHECK(!cancellation);
    CHECK(cancellation.error().code == StatusCode::Cancelled);

    Pose3d invalid_target{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 2.0}};
    CHECK(!solver.solve(robot, scene, connected_atlas.value().atlas, invalid_target, Configuration{-0.5}));
    return EXIT_SUCCESS;
}
