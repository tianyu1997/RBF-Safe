#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

rbfsafe::SerialRobotModel prismatic_robot() {
    return rbfsafe::SerialRobotModel("linear-axis", {{0.0, 0.0, 0.0, 0.0, rbfsafe::JointType::Prismatic}},
                                     {{0.0, 1.0}}, {0.05});
}

std::vector<rbfsafe::TimedConfiguration> trajectory() { return {{0, {0.0}}, {16, {1.0}}}; }

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::filesystem::path temporary_directory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("rbfsafe-occupancy-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto robot = prismatic_robot();
    ContinuousOccupancyBuildOptions build_options;
    build_options.maximum_subdivision_depth = 8;
    build_options.maximum_normalized_joint_width = 0.25;
    build_options.link_padding = 0.01;

    auto first = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a",
                                                  {-2.0, 0.0, 0.0}, trajectory(), build_options);
    auto second = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-b",
                                                   {2.0, 0.0, 0.0}, trajectory(), build_options);
    CHECK(first);
    CHECK(second);
    CHECK(first.value().valid());
    CHECK(second.value().valid());
    CHECK(first.value().slices.size() == 4);
    CHECK(first.value().slices.front().begin_tick == 0);
    CHECK(first.value().slices.front().end_tick == 4);
    CHECK(first.value().slices.back().begin_tick == 12);
    CHECK(first.value().slices.back().end_tick == 16);
    CHECK(close(first.value().slices.front().link_envelopes.front().lower[0], -2.06));
    CHECK(close(first.value().slices.front().link_envelopes.front().upper[0], -1.94));
    CHECK(first.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!first.value().authorizes_execution());
    CHECK(verify_robot_trajectory_occupancy(robot, first.value()));
    for (const auto& slice : first.value().slices) {
        for (std::size_t sample = 0; sample <= 100; ++sample) {
            const double tick =
                static_cast<double>(slice.begin_tick) +
                static_cast<double>(slice.end_tick - slice.begin_tick) * static_cast<double>(sample) / 100.0;
            const double fraction = tick / 16.0;
            auto points = robot.forward_kinematics(Configuration{fraction});
            CHECK(points);
            for (const auto& endpoint : points.value()) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const double translated = endpoint[axis] + first.value().workspace_translation[axis];
                    CHECK(translated >= slice.link_envelopes.front().lower[axis] - 1e-12);
                    CHECK(translated <= slice.link_envelopes.front().upper[axis] + 1e-12);
                }
            }
        }
    }

    auto repeated = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a",
                                                     {-2.0, 0.0, 0.0}, trajectory(), build_options);
    CHECK(repeated);
    CHECK(repeated.value().id == first.value().id);
    CHECK(repeated.value().slices.front().id == first.value().slices.front().id);
    CHECK(!analyze_continuous_fleet_occupancy(std::array{first.value()}));

    ContinuousFleetOccupancyOptions separated_options;
    separated_options.minimum_separation = 3.0;
    auto separated =
        analyze_continuous_fleet_occupancy(std::array{second.value(), first.value()}, separated_options);
    CHECK(separated);
    CHECK(separated.value().valid());
    CHECK(separated.value().status == ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes);
    CHECK(separated.value().conflicts.empty());
    CHECK(separated.value().slice_pair_evaluations == 4);
    CHECK(separated.value().link_pair_evaluations == 4);
    CHECK(separated.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!separated.value().authorizes_execution());

    auto delayed_trajectory = trajectory();
    delayed_trajectory[0].tick = 32;
    delayed_trajectory[1].tick = 48;
    auto delayed = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-c",
                                                    {2.0, 0.0, 0.0}, delayed_trajectory, build_options);
    CHECK(delayed);
    auto temporally_disjoint = analyze_continuous_fleet_occupancy(std::array{first.value(), delayed.value()});
    CHECK(temporally_disjoint);
    CHECK(temporally_disjoint.value().status ==
          ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes);
    CHECK(temporally_disjoint.value().slice_pair_evaluations == 4);
    CHECK(temporally_disjoint.value().link_pair_evaluations == 0);

    ContinuousFleetOccupancyOptions margin_options;
    margin_options.minimum_separation = 4.0;
    auto margin =
        analyze_continuous_fleet_occupancy(std::array{first.value(), second.value()}, margin_options);
    CHECK(margin);
    CHECK(margin.value().status == ContinuousFleetOccupancyStatus::PotentialConflict);
    CHECK(margin.value().conflicts.size() == 4);
    CHECK(margin.value().conflicts.front().reason ==
          ContinuousOccupancyConflictReason::SeparationMarginViolated);
    CHECK(margin.value().conflicts.front().clearance_lower_bound < 4.0);
    CHECK(continuous_occupancy_conflict_reason_name(margin.value().conflicts.front().reason) ==
          "SEPARATION_MARGIN_VIOLATED");
    CHECK(continuous_fleet_occupancy_status_name(margin.value().status) == "POTENTIAL_CONFLICT");
    auto conflict_limited_options = margin_options;
    conflict_limited_options.maximum_conflicts = 2;
    auto conflict_limited = analyze_continuous_fleet_occupancy(std::array{first.value(), second.value()},
                                                               conflict_limited_options);
    CHECK(!conflict_limited);
    CHECK(conflict_limited.error().code == StatusCode::ResourceLimit);

    auto overlapping = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-b",
                                                        {-2.0, 0.0, 0.0}, trajectory(), build_options);
    CHECK(overlapping);
    auto conflict = analyze_continuous_fleet_occupancy(std::array{first.value(), overlapping.value()});
    CHECK(conflict);
    CHECK(conflict.value().conflicts.size() == 4);
    CHECK(conflict.value().conflicts.front().reason ==
          ContinuousOccupancyConflictReason::SweptEnvelopeOverlap);

    auto wrong_timeline = second.value();
    wrong_timeline.timeline_id = "other-clock";
    CHECK(!wrong_timeline.valid());
    auto rejected_identity = analyze_continuous_fleet_occupancy(std::array{first.value(), wrong_timeline});
    CHECK(!rejected_identity);
    CHECK(rejected_identity.error().code == StatusCode::IdentityMismatch);

    auto duplicate_deployment = build_robot_trajectory_occupancy(
        robot, "cell-clock-v1", "cell-world", "arm-a", {2.0, 0.0, 0.0}, trajectory(), build_options);
    CHECK(duplicate_deployment);
    auto rejected_duplicate =
        analyze_continuous_fleet_occupancy(std::array{first.value(), duplicate_deployment.value()});
    CHECK(!rejected_duplicate);
    CHECK(rejected_duplicate.error().code == StatusCode::InvalidArgument);
    auto other_frame = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "other-world", "arm-b",
                                                        {2.0, 0.0, 0.0}, trajectory(), build_options);
    CHECK(other_frame);
    auto rejected_frame = analyze_continuous_fleet_occupancy(std::array{first.value(), other_frame.value()});
    CHECK(!rejected_frame);
    CHECK(rejected_frame.error().code == StatusCode::IdentityMismatch);

    auto tampered = first.value();
    tampered.slices.front().link_envelopes.front().upper[0] += 0.1;
    CHECK(!tampered.valid());
    auto rejected_replay = verify_robot_trajectory_occupancy(robot, tampered);
    CHECK(!rejected_replay);
    CHECK(rejected_replay.error().code == StatusCode::IdentityMismatch);

    auto wrong_robot = SerialRobotModel::create(
        "other-linear-axis", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}}, {{0.0, 1.0}}, {0.05});
    CHECK(wrong_robot);
    auto rejected_robot = verify_robot_trajectory_occupancy(wrong_robot.value(), first.value());
    CHECK(!rejected_robot);
    CHECK(rejected_robot.error().code == StatusCode::IdentityMismatch);

    auto invalid_dimension = trajectory();
    invalid_dimension[1].configuration.push_back(0.0);
    auto rejected_dimension = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a",
                                                               {}, invalid_dimension);
    CHECK(!rejected_dimension);
    CHECK(rejected_dimension.error().code == StatusCode::DimensionMismatch);
    auto invalid_ticks = trajectory();
    invalid_ticks[1].tick = 0;
    CHECK(
        !build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a", {}, invalid_ticks));
    auto outside_limits = trajectory();
    outside_limits[1].configuration = {1.1};
    CHECK(
        !build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a", {}, outside_limits));

    auto bounded_build = build_options;
    bounded_build.maximum_slices = 3;
    auto slice_limited = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a", {},
                                                          trajectory(), bounded_build);
    CHECK(!slice_limited);
    CHECK(slice_limited.error().code == StatusCode::ResourceLimit);
    auto envelope_limited_build = build_options;
    envelope_limited_build.maximum_link_envelopes = 3;
    auto envelope_limited = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a",
                                                             {}, trajectory(), envelope_limited_build);
    CHECK(!envelope_limited);
    CHECK(envelope_limited.error().code == StatusCode::ResourceLimit);
    auto waypoint_limited = build_options;
    waypoint_limited.maximum_input_waypoints = 1;
    auto rejected_waypoint_limit = build_robot_trajectory_occupancy(
        robot, "cell-clock-v1", "cell-world", "arm-a", {}, trajectory(), waypoint_limited);
    CHECK(!rejected_waypoint_limit);
    CHECK(rejected_waypoint_limit.error().code == StatusCode::InvalidArgument);
    auto no_subdivision = build_options;
    no_subdivision.maximum_subdivision_depth = 0;
    auto conservative_unsplit = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world",
                                                                 "arm-c", {}, trajectory(), no_subdivision);
    CHECK(conservative_unsplit);
    CHECK(conservative_unsplit.value().slices.size() == 1);
    auto mixed_partitions =
        analyze_continuous_fleet_occupancy(std::array{first.value(), conservative_unsplit.value()});
    CHECK(mixed_partitions);
    CHECK(mixed_partitions.value().slice_pair_evaluations == 4);

    auto cancelled_build = build_options;
    cancelled_build.cancellation = CancellationToken{};
    cancelled_build.cancellation.cancel();
    auto cancelled = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a", {},
                                                      trajectory(), cancelled_build);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);

    auto overflowing_translation = build_robot_trajectory_occupancy(
        robot, "cell-clock-v1", "cell-world", "arm-a", {std::numeric_limits<double>::infinity(), 0.0, 0.0},
        trajectory(), build_options);
    CHECK(!overflowing_translation);
    CHECK(overflowing_translation.error().code == StatusCode::InvalidArgument);
    auto finite_overflowing_translation = build_robot_trajectory_occupancy(
        robot, "cell-clock-v1", "cell-world", "arm-a", {std::numeric_limits<double>::max(), 0.0, 0.0},
        trajectory(), build_options);
    CHECK(!finite_overflowing_translation);
    CHECK(finite_overflowing_translation.error().code == StatusCode::InvalidArgument);
    auto distant_first = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-a",
                                                          {-1e307, 0.0, 0.0}, trajectory(), build_options);
    auto distant_second = build_robot_trajectory_occupancy(robot, "cell-clock-v1", "cell-world", "arm-b",
                                                           {1e307, 0.0, 0.0}, trajectory(), build_options);
    CHECK(distant_first);
    CHECK(distant_second);
    auto extreme_margin_options = separated_options;
    extreme_margin_options.minimum_separation = std::numeric_limits<double>::max();
    auto extreme_margin = analyze_continuous_fleet_occupancy(
        std::array{distant_first.value(), distant_second.value()}, extreme_margin_options);
    CHECK(extreme_margin);
    CHECK(extreme_margin.value().status == ContinuousFleetOccupancyStatus::PotentialConflict);
    CHECK(extreme_margin.value().conflicts.front().clearance_lower_bound == 0.0);

    auto bounded_analysis = separated_options;
    bounded_analysis.maximum_slice_pair_evaluations = 3;
    auto slice_pair_limited =
        analyze_continuous_fleet_occupancy(std::array{first.value(), second.value()}, bounded_analysis);
    CHECK(!slice_pair_limited);
    CHECK(slice_pair_limited.error().code == StatusCode::ResourceLimit);
    bounded_analysis = separated_options;
    bounded_analysis.maximum_link_pair_evaluations = 3;
    auto link_pair_limited =
        analyze_continuous_fleet_occupancy(std::array{first.value(), second.value()}, bounded_analysis);
    CHECK(!link_pair_limited);
    CHECK(link_pair_limited.error().code == StatusCode::ResourceLimit);
    auto cancelled_analysis = separated_options;
    cancelled_analysis.cancellation = CancellationToken{};
    cancelled_analysis.cancellation.cancel();
    auto analysis_cancelled =
        analyze_continuous_fleet_occupancy(std::array{first.value(), second.value()}, cancelled_analysis);
    CHECK(!analysis_cancelled);
    CHECK(analysis_cancelled.error().code == StatusCode::Cancelled);

    auto bundle = ContinuousFleetOccupancyBundle::create({second.value(), first.value()}, separated_options);
    auto reordered_bundle =
        ContinuousFleetOccupancyBundle::create({first.value(), second.value()}, separated_options);
    CHECK(bundle);
    CHECK(reordered_bundle);
    CHECK(bundle.value().valid());
    CHECK(bundle.value().id() == reordered_bundle.value().id());
    CHECK(bundle.value().report().id == separated.value().id);
    CHECK(bundle.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!bundle.value().authorizes_execution());

    const auto directory = temporary_directory();
    const auto bundle_path = directory / "occupancy.json";
    auto missing = ContinuousFleetOccupancyBundle::load(directory / "missing.json");
    CHECK(!missing);
    CHECK(missing.error().code == StatusCode::IoError);
    CHECK(bundle.value().save(bundle_path));
    auto loaded = ContinuousFleetOccupancyBundle::load(bundle_path);
    CHECK(loaded);
    CHECK(loaded.value().valid());
    CHECK(loaded.value().id() == bundle.value().id());
    CHECK(!bundle.value().save(bundle_path));
    CHECK(bundle.value().save(bundle_path, SaveOptions{true}));

    auto payload_limited_options = ContinuousFleetOccupancyBundleLoadOptions{};
    payload_limited_options.maximum_payload_bytes = 8;
    auto payload_limited = ContinuousFleetOccupancyBundle::load(bundle_path, payload_limited_options);
    CHECK(!payload_limited);
    CHECK(payload_limited.error().code == StatusCode::ResourceLimit);
    auto dimension_limited_options = ContinuousFleetOccupancyBundleLoadOptions{};
    dimension_limited_options.maximum_dimension = 1;
    CHECK(ContinuousFleetOccupancyBundle::load(bundle_path, dimension_limited_options));
    dimension_limited_options.maximum_dimension = 0;
    CHECK(!ContinuousFleetOccupancyBundle::load(bundle_path, dimension_limited_options));
    auto slice_load_limit = ContinuousFleetOccupancyBundleLoadOptions{};
    slice_load_limit.maximum_slices = 7;
    auto slice_load_limited = ContinuousFleetOccupancyBundle::load(bundle_path, slice_load_limit);
    CHECK(!slice_load_limited);
    CHECK(slice_load_limited.error().code == StatusCode::ResourceLimit);

    auto load_cancelled_options = ContinuousFleetOccupancyBundleLoadOptions{};
    load_cancelled_options.cancellation.cancel();
    auto load_cancelled = ContinuousFleetOccupancyBundle::load(bundle_path, load_cancelled_options);
    CHECK(!load_cancelled);
    CHECK(load_cancelled.error().code == StatusCode::Cancelled);

    const auto original = read_text(bundle_path);
    auto checksum_tamper = original;
    const auto checksum_position = checksum_tamper.find("\"checksum\": \"");
    CHECK(checksum_position != std::string::npos);
    const auto digest_position = checksum_position + 13;
    checksum_tamper[digest_position] = checksum_tamper[digest_position] == 'a' ? 'b' : 'a';
    const auto tamper_path = directory / "tamper.json";
    write_text(tamper_path, checksum_tamper);
    auto checksum_rejected = ContinuousFleetOccupancyBundle::load(tamper_path);
    CHECK(!checksum_rejected);
    CHECK(checksum_rejected.error().code == StatusCode::CorruptData);

    auto unknown_schema = original;
    const auto schema_position = unknown_schema.rfind("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, 11, "\"schema\": 2");
    const auto schema_path = directory / "schema.json";
    write_text(schema_path, unknown_schema);
    auto schema_rejected = ContinuousFleetOccupancyBundle::load(schema_path);
    CHECK(!schema_rejected);
    CHECK(schema_rejected.error().code == StatusCode::IncompatibleFormat);

    const auto truncated_path = directory / "truncated.json";
    write_text(truncated_path, original.substr(0, original.size() / 2));
    auto truncated = ContinuousFleetOccupancyBundle::load(truncated_path);
    CHECK(!truncated);

    const auto symlink_path = directory / "indirect.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(bundle_path, symlink_path, symlink_error);
    if (!symlink_error) {
        auto indirect = ContinuousFleetOccupancyBundle::load(symlink_path);
        CHECK(!indirect);
        CHECK(indirect.error().code == StatusCode::CorruptData);
    }

    const auto fixture_directory =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "continuous_fleet_occupancy_schema1";
    auto fixture = ContinuousFleetOccupancyBundle::load(fixture_directory / "occupancy.json");
    auto fixture_robot = SerialRobotModel::from_json(fixture_directory / "robot.json");
    CHECK(fixture);
    CHECK(fixture_robot);
    CHECK(fixture.value().id() == "d9a6a28c80ae86a28b996c8da954c33c725d9883a22f9f080f22d51e72be4231");
    CHECK(fixture.value().report().id == "05fc3206ce76135946763fca75e3a399449a80b44a47ae168d564a439aa280ef");
    for (const auto& occupancy : fixture.value().occupancies())
        CHECK(verify_robot_trajectory_occupancy(fixture_robot.value(), occupancy));

    std::filesystem::remove_all(directory);
    return 0;
}
