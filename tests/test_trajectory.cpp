#include "test_support.h"

#include <limits>
#include <vector>

int main() {
    using namespace rbfsafe;

    const auto robot = planar_robot();
    const SceneSnapshot empty_scene({}, "trajectory-empty-v1");
    auto complete = AtlasBuilder{}.build(robot, empty_scene, {{0.0, 0.0}});
    CHECK(complete);

    const TrajectoryAuditor auditor;
    auto certified = auditor.audit(complete.value().atlas,
                                   std::vector<Configuration>{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}});
    CHECK(certified);
    CHECK(certified.value().status == TrajectoryAuditStatus::Certified);
    CHECK(close(certified.value().coverage_ratio, 1.0));
    CHECK(certified.value().waypoint_count == 3);
    CHECK(certified.value().segment_count == 2);
    CHECK(certified.value().region_tests == 2);
    CHECK(certified.value().region_sequence.size() == 1);
    CHECK(certified.value().uncovered_intervals.empty());

    SerialRobotModel prismatic("trajectory-prismatic", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}},
                               {{0.0, 2.0}}, {0.05});
    SceneSnapshot split_scene({{"high-block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, "trajectory-split-v1");
    auto split = AtlasBuilder{}.build(prismatic, split_scene, {{0.25}});
    CHECK(split);
    CHECK(split.value().atlas.regions().size() == 1);

    auto partial = auditor.audit(split.value().atlas, std::vector<Configuration>{{0.25}, {1.5}});
    CHECK(partial);
    CHECK(partial.value().status == TrajectoryAuditStatus::Partial);
    CHECK(close(partial.value().coverage_ratio, 0.6));
    CHECK(partial.value().region_sequence.size() == 1);
    CHECK(partial.value().uncovered_intervals.size() == 1);
    CHECK(partial.value().uncovered_intervals[0].segment_index == 0);
    CHECK(close(partial.value().uncovered_intervals[0].start_fraction, 0.6));
    CHECK(close(partial.value().uncovered_intervals[0].end_fraction, 1.0));
    CHECK(!partial.value().uncovered_intervals[0].start_included);
    CHECK(partial.value().uncovered_intervals[0].end_included);

    auto invalid = auditor.audit(split.value().atlas, std::vector<Configuration>{{1.25}, {1.5}});
    CHECK(invalid);
    CHECK(invalid.value().status == TrajectoryAuditStatus::Invalid);
    CHECK(close(invalid.value().coverage_ratio, 0.0));
    CHECK(invalid.value().region_sequence.empty());
    CHECK(invalid.value().uncovered_intervals.size() == 1);
    CHECK(invalid.value().uncovered_intervals[0].start_included);
    CHECK(invalid.value().uncovered_intervals[0].end_included);

    auto stationary = auditor.audit(split.value().atlas, std::vector<Configuration>{{0.25}, {0.25}});
    CHECK(stationary);
    CHECK(stationary.value().status == TrajectoryAuditStatus::Certified);

    auto short_trajectory = auditor.audit(split.value().atlas, std::vector<Configuration>{{0.25}});
    CHECK(!short_trajectory);
    CHECK(short_trajectory.error().code == StatusCode::InvalidArgument);
    auto wrong_dimension =
        auditor.audit(split.value().atlas, std::vector<Configuration>{{0.25, 0.0}, {0.5, 0.0}});
    CHECK(!wrong_dimension);
    CHECK(wrong_dimension.error().code == StatusCode::DimensionMismatch);
    auto non_finite = auditor.audit(
        split.value().atlas, std::vector<Configuration>{{0.25}, {std::numeric_limits<double>::infinity()}});
    CHECK(!non_finite);
    CHECK(non_finite.error().code == StatusCode::InvalidArgument);
    auto overflowing_delta =
        auditor.audit(split.value().atlas, std::vector<Configuration>{{-std::numeric_limits<double>::max()},
                                                                      {std::numeric_limits<double>::max()}});
    CHECK(!overflowing_delta);
    CHECK(overflowing_delta.error().code == StatusCode::InvalidArgument);

    TrajectoryAuditOptions limited;
    limited.maximum_region_tests = 1;
    auto resource_limited = auditor.audit(
        complete.value().atlas, std::vector<Configuration>{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}}, limited);
    CHECK(!resource_limited);
    CHECK(resource_limited.error().code == StatusCode::ResourceLimit);
    TrajectoryAuditOptions zero_budget;
    zero_budget.maximum_region_tests = 0;
    auto invalid_budget = auditor.audit(complete.value().atlas,
                                        std::vector<Configuration>{{0.0, 0.0}, {1.0, 1.0}}, zero_budget);
    CHECK(!invalid_budget);
    CHECK(invalid_budget.error().code == StatusCode::InvalidArgument);

    TrajectoryAuditOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_result =
        auditor.audit(complete.value().atlas, std::vector<Configuration>{{0.0, 0.0}, {1.0, 1.0}}, cancelled);
    CHECK(!cancelled_result);
    CHECK(cancelled_result.error().code == StatusCode::Cancelled);
    return EXIT_SUCCESS;
}
