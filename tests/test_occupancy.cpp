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

rbfsafe::WorkspaceAabb workspace_box(double lower_x, double upper_x) {
    return {{lower_x, -0.1, -0.1}, {upper_x, 0.1, 0.1}};
}

std::vector<rbfsafe::TimedWorkspaceAabb> moving_obstacle_trajectory(double first_x, double middle_x,
                                                                    double final_x) {
    return {{0, workspace_box(first_x, first_x + 0.2)},
            {8, workspace_box(middle_x, middle_x + 0.2)},
            {16, workspace_box(final_x, final_x + 0.2)}};
}

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
    CHECK(first.value().storage_schema == 1);
    CHECK(first.value().algorithm_version == "1");
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

    DeploymentFrameBounds rotated_frame;
    rotated_frame.rotation = {0.0, 0.0, 1.0, 0.0, 1.0, 0.0, -1.0, 0.0, 0.0};
    rotated_frame.translation = {1.0, 2.0, 3.0};
    CHECK(rotated_frame.valid());
    CHECK(rotated_frame.exact());
    auto rotated = build_robot_trajectory_occupancy_in_frame(
        robot, "cell-clock-v1", "cell-world", "arm-rotated", rotated_frame, trajectory(), build_options);
    CHECK(rotated);
    CHECK(rotated.value().storage_schema == 2);
    CHECK(rotated.value().algorithm_version == "2");
    CHECK(rotated.value().workspace_rotation == rotated_frame.rotation);
    CHECK(verify_robot_trajectory_occupancy(robot, rotated.value()));
    for (const auto& slice : rotated.value().slices) {
        for (std::size_t sample = 0; sample <= 100; ++sample) {
            const double tick =
                static_cast<double>(slice.begin_tick) +
                static_cast<double>(slice.end_tick - slice.begin_tick) * static_cast<double>(sample) / 100.0;
            const double fraction = tick / 16.0;
            auto points = robot.forward_kinematics(Configuration{fraction});
            CHECK(points);
            for (const auto& endpoint : points.value()) {
                for (std::size_t output_axis = 0; output_axis < 3; ++output_axis) {
                    double transformed = rotated_frame.translation[output_axis];
                    for (std::size_t input_axis = 0; input_axis < 3; ++input_axis) {
                        transformed +=
                            rotated_frame.rotation[output_axis * 3 + input_axis] * endpoint[input_axis];
                    }
                    CHECK(transformed >= slice.link_envelopes.front().lower[output_axis] - 1e-12);
                    CHECK(transformed <= slice.link_envelopes.front().upper[output_axis] + 1e-12);
                }
            }
        }
    }

    auto uncertain_frame = rotated_frame;
    uncertain_frame.translation_uncertainty = {0.1, 0.2, 0.3};
    uncertain_frame.angular_uncertainty_radians = 0.05;
    CHECK(uncertain_frame.valid());
    CHECK(!uncertain_frame.exact());
    auto uncertain = build_robot_trajectory_occupancy_in_frame(
        robot, "cell-clock-v1", "cell-world", "arm-uncertain", uncertain_frame, trajectory(), build_options);
    CHECK(uncertain);
    CHECK(verify_robot_trajectory_occupancy(robot, uncertain.value()));
    CHECK(uncertain.value().id != rotated.value().id);
    for (std::size_t slice = 0; slice < rotated.value().slices.size(); ++slice) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(uncertain.value().slices[slice].link_envelopes.front().lower[axis] <
                  rotated.value().slices[slice].link_envelopes.front().lower[axis]);
            CHECK(uncertain.value().slices[slice].link_envelopes.front().upper[axis] >
                  rotated.value().slices[slice].link_envelopes.front().upper[axis]);
        }
    }
    auto invalid_frame = rotated_frame;
    invalid_frame.rotation[0] = -1.0;
    CHECK(!invalid_frame.valid());
    CHECK(!build_robot_trajectory_occupancy_in_frame(robot, "cell-clock-v1", "cell-world", "arm-invalid",
                                                     invalid_frame, trajectory(), build_options));
    invalid_frame = rotated_frame;
    invalid_frame.translation_uncertainty[0] = -0.1;
    CHECK(!invalid_frame.valid());
    invalid_frame = rotated_frame;
    invalid_frame.angular_uncertainty_radians = 4.0;
    CHECK(!invalid_frame.valid());

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
    auto far_frame = uncertain_frame;
    far_frame.translation = {20.0, 2.0, 3.0};
    auto far = build_robot_trajectory_occupancy_in_frame(robot, "cell-clock-v1", "cell-world", "arm-far",
                                                         far_frame, trajectory(), build_options);
    CHECK(far);
    auto frame_bundle = ContinuousFleetOccupancyBundle::create({uncertain.value(), far.value()});
    CHECK(frame_bundle);
    CHECK(frame_bundle.value().storage_schema() == 2);
    const auto frame_bundle_path = directory / "occupancy-schema2.json";
    CHECK(frame_bundle.value().save(frame_bundle_path));
    auto loaded_frame_bundle = ContinuousFleetOccupancyBundle::load(frame_bundle_path);
    CHECK(loaded_frame_bundle);
    CHECK(loaded_frame_bundle.value().id() == frame_bundle.value().id());
    CHECK(loaded_frame_bundle.value().storage_schema() == 2);
    for (const auto& occupancy : loaded_frame_bundle.value().occupancies())
        CHECK(verify_robot_trajectory_occupancy(robot, occupancy));
    auto mixed_bundle = ContinuousFleetOccupancyBundle::create({first.value(), far.value()});
    CHECK(mixed_bundle);
    CHECK(mixed_bundle.value().storage_schema() == 2);
    const auto mixed_bundle_path = directory / "occupancy-mixed-schema.json";
    CHECK(mixed_bundle.value().save(mixed_bundle_path));
    auto loaded_mixed_bundle = ContinuousFleetOccupancyBundle::load(mixed_bundle_path);
    CHECK(loaded_mixed_bundle);
    CHECK(loaded_mixed_bundle.value().id() == mixed_bundle.value().id());
    CHECK(loaded_mixed_bundle.value().occupancies().front().storage_schema == 1);
    CHECK(loaded_mixed_bundle.value().occupancies().back().storage_schema == 2);

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

    const auto fixture2_directory =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "continuous_fleet_occupancy_schema2";
    auto fixture2 = ContinuousFleetOccupancyBundle::load(fixture2_directory / "occupancy.json");
    auto fixture2_robot = SerialRobotModel::from_json(fixture2_directory / "robot.json");
    CHECK(fixture2);
    CHECK(fixture2_robot);
    CHECK(fixture2.value().id() == "6030e3574db5634f60b6cf04ffc325077f944ef23d256ef7cc937fe857dce8d0");
    CHECK(fixture2.value().report().id == "9a8b12df9ba0ca88142a9f44ef722a411cb33930a8986e4dd7b657d8d333053e");
    CHECK(fixture2.value().storage_schema() == 2);
    constexpr std::array<double, 9> identity_rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    constexpr std::array<double, 3> fixture_uncertainty{0.01, 0.01, 0.02};
    for (const auto& occupancy : fixture2.value().occupancies()) {
        CHECK(occupancy.workspace_rotation != identity_rotation);
        CHECK(occupancy.workspace_translation_uncertainty == fixture_uncertainty);
        CHECK(verify_robot_trajectory_occupancy(fixture2_robot.value(), occupancy));
    }

    MovingObstacleOccupancyBuildOptions obstacle_build_options;
    obstacle_build_options.obstacle_padding = 0.02;
    auto far_obstacle =
        build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cart-far",
                                        moving_obstacle_trajectory(4.0, 5.0, 4.0), obstacle_build_options);
    auto repeated_obstacle =
        build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cart-far",
                                        moving_obstacle_trajectory(4.0, 5.0, 4.0), obstacle_build_options);
    CHECK(far_obstacle);
    CHECK(repeated_obstacle);
    CHECK(far_obstacle.value().valid());
    CHECK(far_obstacle.value().id == repeated_obstacle.value().id);
    CHECK(far_obstacle.value().slices.size() == 2);
    CHECK(far_obstacle.value().algorithm_version == "1");
    CHECK(far_obstacle.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!far_obstacle.value().authorizes_execution());
    CHECK(verify_moving_obstacle_occupancy(far_obstacle.value()));
    for (std::size_t segment = 0; segment < far_obstacle.value().slices.size(); ++segment) {
        const auto& slice = far_obstacle.value().slices[segment];
        CHECK(slice.begin_tick == far_obstacle.value().trajectory[segment].tick);
        CHECK(slice.end_tick == far_obstacle.value().trajectory[segment + 1].tick);
        for (std::size_t sample = 0; sample <= 100; ++sample) {
            const double fraction = static_cast<double>(sample) / 100.0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double lower =
                    far_obstacle.value().trajectory[segment].bounds.lower[axis] +
                    fraction * (far_obstacle.value().trajectory[segment + 1].bounds.lower[axis] -
                                far_obstacle.value().trajectory[segment].bounds.lower[axis]);
                const double upper =
                    far_obstacle.value().trajectory[segment].bounds.upper[axis] +
                    fraction * (far_obstacle.value().trajectory[segment + 1].bounds.upper[axis] -
                                far_obstacle.value().trajectory[segment].bounds.upper[axis]);
                CHECK(lower >= slice.swept_bounds.lower[axis]);
                CHECK(upper <= slice.swept_bounds.upper[axis]);
            }
        }
    }

    auto separated_scene =
        analyze_continuous_robot_scene_occupancy(std::array{first.value()}, std::array{far_obstacle.value()});
    CHECK(separated_scene);
    CHECK(separated_scene.value().valid());
    CHECK(separated_scene.value().status ==
          ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes);
    CHECK(separated_scene.value().conflicts.empty());
    CHECK(separated_scene.value().begin_tick == 0);
    CHECK(separated_scene.value().end_tick == 16);
    CHECK(separated_scene.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!separated_scene.value().authorizes_execution());
    CHECK(continuous_robot_scene_occupancy_status_name(separated_scene.value().status) ==
          "CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES");

    auto near_obstacle = build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cart-near",
                                                         moving_obstacle_trajectory(-2.0, -1.5, -1.0));
    CHECK(near_obstacle);
    auto conflict_scene = analyze_continuous_robot_scene_occupancy(std::array{first.value()},
                                                                   std::array{near_obstacle.value()});
    CHECK(conflict_scene);
    CHECK(conflict_scene.value().status == ContinuousRobotSceneOccupancyStatus::PotentialConflict);
    CHECK(!conflict_scene.value().conflicts.empty());
    CHECK(conflict_scene.value().conflicts.front().reason ==
          ContinuousOccupancyConflictReason::SweptEnvelopeOverlap);
    CHECK(continuous_robot_scene_occupancy_status_name(conflict_scene.value().status) ==
          "POTENTIAL_CONFLICT");

    auto close_obstacle = build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cart-close",
                                                          moving_obstacle_trajectory(-1.7, -1.7, -1.7));
    CHECK(close_obstacle);
    ContinuousRobotSceneOccupancyOptions scene_margin_options;
    scene_margin_options.minimum_separation = 0.5;
    auto margin_scene = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{close_obstacle.value()}, scene_margin_options);
    CHECK(margin_scene);
    CHECK(margin_scene.value().status == ContinuousRobotSceneOccupancyStatus::PotentialConflict);
    CHECK(margin_scene.value().conflicts.front().reason ==
          ContinuousOccupancyConflictReason::SeparationMarginViolated);
    CHECK(margin_scene.value().conflicts.front().clearance_lower_bound < 0.5);

    auto second_far_obstacle =
        build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cart-z",
                                        moving_obstacle_trajectory(8.0, 9.0, 8.0), obstacle_build_options);
    CHECK(second_far_obstacle);
    auto ordered_scene = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{second_far_obstacle.value(), far_obstacle.value()});
    auto reordered_scene = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{far_obstacle.value(), second_far_obstacle.value()});
    CHECK(ordered_scene);
    CHECK(reordered_scene);
    CHECK(ordered_scene.value().id == reordered_scene.value().id);
    CHECK(ordered_scene.value().obstacle_occupancy_ids == reordered_scene.value().obstacle_occupancy_ids);

    auto invalid_obstacle_ticks = moving_obstacle_trajectory(4.0, 5.0, 4.0);
    invalid_obstacle_ticks[1].tick = 0;
    CHECK(
        !build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "bad-ticks", invalid_obstacle_ticks));
    auto invalid_obstacle_bounds = moving_obstacle_trajectory(4.0, 5.0, 4.0);
    invalid_obstacle_bounds[1].bounds.lower[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(!build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "bad-bounds",
                                           invalid_obstacle_bounds));
    auto invalid_obstacle_options = obstacle_build_options;
    invalid_obstacle_options.obstacle_padding = -0.1;
    CHECK(!build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "bad-options",
                                           moving_obstacle_trajectory(4.0, 5.0, 4.0),
                                           invalid_obstacle_options));
    auto obstacle_slice_limit = obstacle_build_options;
    obstacle_slice_limit.maximum_slices = 1;
    auto obstacle_limited =
        build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "limited",
                                        moving_obstacle_trajectory(4.0, 5.0, 4.0), obstacle_slice_limit);
    CHECK(!obstacle_limited);
    CHECK(obstacle_limited.error().code == StatusCode::ResourceLimit);
    auto cancelled_obstacle_options = obstacle_build_options;
    cancelled_obstacle_options.cancellation.cancel();
    auto cancelled_obstacle = build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "cancelled",
                                                              moving_obstacle_trajectory(4.0, 5.0, 4.0),
                                                              cancelled_obstacle_options);
    CHECK(!cancelled_obstacle);
    CHECK(cancelled_obstacle.error().code == StatusCode::Cancelled);
    auto overflow_obstacle =
        moving_obstacle_trajectory(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max());
    CHECK(!build_moving_obstacle_occupancy("cell-clock-v1", "cell-world", "overflow", overflow_obstacle,
                                           obstacle_build_options));

    auto tampered_obstacle = far_obstacle.value();
    tampered_obstacle.slices.front().swept_bounds.upper[0] += 0.1;
    CHECK(!tampered_obstacle.valid());
    CHECK(!verify_moving_obstacle_occupancy(tampered_obstacle));
    auto limited_obstacle_replay = MovingObstacleOccupancyReplayOptions{};
    limited_obstacle_replay.maximum_slices = 1;
    auto obstacle_replay_limited =
        verify_moving_obstacle_occupancy(far_obstacle.value(), limited_obstacle_replay);
    CHECK(!obstacle_replay_limited);
    CHECK(obstacle_replay_limited.error().code == StatusCode::ResourceLimit);

    auto wrong_obstacle_timeline = build_moving_obstacle_occupancy(
        "other-clock", "cell-world", "wrong-timeline", moving_obstacle_trajectory(4.0, 5.0, 4.0));
    auto wrong_obstacle_frame = build_moving_obstacle_occupancy("cell-clock-v1", "other-world", "wrong-frame",
                                                                moving_obstacle_trajectory(4.0, 5.0, 4.0));
    auto short_obstacle_trajectory = moving_obstacle_trajectory(4.0, 5.0, 4.0);
    short_obstacle_trajectory.front().tick = 1;
    auto wrong_obstacle_window = build_moving_obstacle_occupancy("cell-clock-v1", "cell-world",
                                                                 "wrong-window", short_obstacle_trajectory);
    CHECK(wrong_obstacle_timeline);
    CHECK(wrong_obstacle_frame);
    CHECK(wrong_obstacle_window);
    CHECK(!analyze_continuous_robot_scene_occupancy(std::array{first.value()},
                                                    std::array{wrong_obstacle_timeline.value()}));
    CHECK(!analyze_continuous_robot_scene_occupancy(std::array{first.value()},
                                                    std::array{wrong_obstacle_frame.value()}));
    CHECK(!analyze_continuous_robot_scene_occupancy(std::array{first.value()},
                                                    std::array{wrong_obstacle_window.value()}));
    auto duplicate_obstacle = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{far_obstacle.value(), repeated_obstacle.value()});
    CHECK(!duplicate_obstacle);
    CHECK(duplicate_obstacle.error().code == StatusCode::InvalidArgument);

    auto bounded_scene_analysis = ContinuousRobotSceneOccupancyOptions{};
    bounded_scene_analysis.maximum_slice_pair_evaluations = 1;
    auto scene_pair_limited = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{far_obstacle.value()}, bounded_scene_analysis);
    CHECK(!scene_pair_limited);
    CHECK(scene_pair_limited.error().code == StatusCode::ResourceLimit);
    bounded_scene_analysis = {};
    bounded_scene_analysis.maximum_link_evaluations = 1;
    auto scene_link_limited = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{far_obstacle.value()}, bounded_scene_analysis);
    CHECK(!scene_link_limited);
    CHECK(scene_link_limited.error().code == StatusCode::ResourceLimit);
    bounded_scene_analysis = {};
    bounded_scene_analysis.maximum_conflicts = 1;
    auto scene_conflict_limited = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{near_obstacle.value()}, bounded_scene_analysis);
    CHECK(!scene_conflict_limited);
    CHECK(scene_conflict_limited.error().code == StatusCode::ResourceLimit);
    bounded_scene_analysis = {};
    bounded_scene_analysis.cancellation.cancel();
    auto scene_cancelled = analyze_continuous_robot_scene_occupancy(
        std::array{first.value()}, std::array{far_obstacle.value()}, bounded_scene_analysis);
    CHECK(!scene_cancelled);
    CHECK(scene_cancelled.error().code == StatusCode::Cancelled);

    auto scene_bundle = ContinuousRobotSceneOccupancyBundle::create(
        {first.value()}, {second_far_obstacle.value(), far_obstacle.value()});
    auto reordered_scene_bundle = ContinuousRobotSceneOccupancyBundle::create(
        {first.value()}, {far_obstacle.value(), second_far_obstacle.value()});
    CHECK(scene_bundle);
    CHECK(reordered_scene_bundle);
    CHECK(scene_bundle.value().valid());
    CHECK(scene_bundle.value().id() == reordered_scene_bundle.value().id());
    CHECK(scene_bundle.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!scene_bundle.value().authorizes_execution());
    const auto scene_bundle_path = directory / "robot-scene-occupancy.json";
    CHECK(scene_bundle.value().save(scene_bundle_path));
    auto loaded_scene_bundle = ContinuousRobotSceneOccupancyBundle::load(scene_bundle_path);
    CHECK(loaded_scene_bundle);
    CHECK(loaded_scene_bundle.value().id() == scene_bundle.value().id());
    CHECK(verify_robot_trajectory_occupancy(robot, loaded_scene_bundle.value().robot_occupancies().front()));
    for (const auto& obstacle : loaded_scene_bundle.value().obstacle_occupancies())
        CHECK(verify_moving_obstacle_occupancy(obstacle));
    CHECK(!scene_bundle.value().save(scene_bundle_path));
    CHECK(scene_bundle.value().save(scene_bundle_path, SaveOptions{true}));

    const auto scene_bundle_text = read_text(scene_bundle_path);
    const auto scene_bundle_bytes =
        std::as_bytes(std::span<const char>(scene_bundle_text.data(), scene_bundle_text.size()));
    auto loaded_scene_bytes = load_continuous_robot_scene_occupancy_bundle(scene_bundle_bytes);
    CHECK(loaded_scene_bytes);
    CHECK(loaded_scene_bytes.value().id() == scene_bundle.value().id());
    auto scene_load_limit = ContinuousRobotSceneOccupancyBundleLoadOptions{};
    scene_load_limit.maximum_obstacle_occupancies = 1;
    auto scene_obstacle_limited =
        ContinuousRobotSceneOccupancyBundle::load(scene_bundle_path, scene_load_limit);
    CHECK(!scene_obstacle_limited);
    CHECK(scene_obstacle_limited.error().code == StatusCode::ResourceLimit);
    scene_load_limit = {};
    scene_load_limit.maximum_payload_bytes = 8;
    auto scene_payload_limited =
        ContinuousRobotSceneOccupancyBundle::load(scene_bundle_path, scene_load_limit);
    CHECK(!scene_payload_limited);
    CHECK(scene_payload_limited.error().code == StatusCode::ResourceLimit);
    scene_load_limit = {};
    scene_load_limit.cancellation.cancel();
    auto scene_load_cancelled =
        ContinuousRobotSceneOccupancyBundle::load(scene_bundle_path, scene_load_limit);
    CHECK(!scene_load_cancelled);
    CHECK(scene_load_cancelled.error().code == StatusCode::Cancelled);

    auto scene_checksum_tamper = scene_bundle_text;
    const auto scene_checksum_position = scene_checksum_tamper.find("\"checksum\": \"");
    CHECK(scene_checksum_position != std::string::npos);
    const auto scene_digest_position = scene_checksum_position + 13;
    scene_checksum_tamper[scene_digest_position] =
        scene_checksum_tamper[scene_digest_position] == 'a' ? 'b' : 'a';
    const auto scene_tamper_path = directory / "robot-scene-tamper.json";
    write_text(scene_tamper_path, scene_checksum_tamper);
    auto scene_checksum_rejected = ContinuousRobotSceneOccupancyBundle::load(scene_tamper_path);
    CHECK(!scene_checksum_rejected);
    CHECK(scene_checksum_rejected.error().code == StatusCode::CorruptData);
    const auto scene_truncated_path = directory / "robot-scene-truncated.json";
    write_text(scene_truncated_path, scene_bundle_text.substr(0, scene_bundle_text.size() / 2));
    CHECK(!ContinuousRobotSceneOccupancyBundle::load(scene_truncated_path));
    auto unknown_scene_schema = scene_bundle_text;
    const auto scene_schema_position = unknown_scene_schema.rfind("\"schema\": 1");
    CHECK(scene_schema_position != std::string::npos);
    unknown_scene_schema.replace(scene_schema_position, 11, "\"schema\": 2");
    const auto scene_schema_path = directory / "robot-scene-schema.json";
    write_text(scene_schema_path, unknown_scene_schema);
    auto scene_schema_rejected = ContinuousRobotSceneOccupancyBundle::load(scene_schema_path);
    CHECK(!scene_schema_rejected);
    CHECK(scene_schema_rejected.error().code == StatusCode::IncompatibleFormat);
    const auto scene_symlink_path = directory / "robot-scene-indirect.json";
    symlink_error.clear();
    std::filesystem::create_symlink(scene_bundle_path, scene_symlink_path, symlink_error);
    if (!symlink_error) {
        auto scene_indirect = ContinuousRobotSceneOccupancyBundle::load(scene_symlink_path);
        CHECK(!scene_indirect);
        CHECK(scene_indirect.error().code == StatusCode::CorruptData);
    }

    const auto scene_fixture_directory =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "continuous_robot_scene_occupancy_schema1";
    auto scene_fixture =
        ContinuousRobotSceneOccupancyBundle::load(scene_fixture_directory / "occupancy.json");
    auto scene_fixture_robot = SerialRobotModel::from_json(scene_fixture_directory / "robot.json");
    CHECK(scene_fixture);
    CHECK(scene_fixture_robot);
    CHECK(scene_fixture.value().id() == "653772769983773f589ae739e4d633ca1224e68b0273dbd3847e3308876e4b3f");
    CHECK(scene_fixture.value().report().id ==
          "8264e583a0edc29442489f16b2f2217e75a83363c851642641f9ab78aa1d22ce");
    for (const auto& occupancy : scene_fixture.value().robot_occupancies())
        CHECK(verify_robot_trajectory_occupancy(scene_fixture_robot.value(), occupancy));
    for (const auto& occupancy : scene_fixture.value().obstacle_occupancies())
        CHECK(verify_moving_obstacle_occupancy(occupancy));

    std::filesystem::remove_all(directory);
    return 0;
}
