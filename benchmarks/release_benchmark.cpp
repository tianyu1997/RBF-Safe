#include <rbfsafe/rbfsafe.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path fixtures;
    std::size_t iterations = 1'000;
    bool json = false;
    bool help = false;
};

struct FixtureCase {
    std::string name;
    std::filesystem::path robot;
    std::filesystem::path scene;
    rbfsafe::Configuration start;
    rbfsafe::Configuration goal;
};

struct CaseMetrics {
    std::string name;
    std::size_t dimension = 0;
    std::size_t regions = 0;
    std::size_t certificates = 0;
    std::size_t queries = 0;
    std::size_t false_safe = 0;
    std::size_t estimated_memory_bytes = 0;
    std::size_t inherited_certificates = 0;
    std::size_t policy_feedback_records = 0;
    std::size_t policy_calibration_profiles = 0;
    std::size_t policy_calibration_lifecycles = 0;
    std::size_t memory_artifacts = 0;
    std::size_t memory_reuses = 0;
    std::size_t fleet_schedule_checks = 0;
    std::size_t fleet_schedule_versions = 0;
    std::size_t artifact_attestations = 0;
    std::size_t artifact_transfers = 0;
    std::size_t artifact_transfer_records = 0;
    std::size_t public_key_transfers = 0;
    std::size_t trusted_service_keys = 0;
    std::size_t trust_bundle_authorizations = 0;
    std::size_t trust_history_records = 0;
    std::size_t trust_checkpoint_signatures = 0;
    std::size_t trust_checkpoints = 0;
    std::size_t deployment_profiles = 0;
    std::size_t deployment_profile_approvals = 0;
    std::size_t deployment_profile_assessments = 0;
    std::size_t conformant_deployment_profiles = 0;
    std::size_t execution_sessions = 0;
    std::size_t execution_session_approvals = 0;
    std::size_t execution_endpoint_acknowledgements = 0;
    std::size_t execution_ledgers = 0;
    std::size_t execution_ledger_records = 0;
    std::size_t execution_controller_completions = 0;
    std::size_t execution_checkpoint_revalidations = 0;
    std::size_t runtime_executable_commands = 0;
    std::size_t transparency_logs = 0;
    std::size_t transparency_records = 0;
    std::size_t deployment_transparency_anchors = 0;
    std::size_t runtime_observation_attestations = 0;
    std::size_t transparency_inclusion_proofs = 0;
    std::size_t transparency_consistency_witnesses = 0;
    std::size_t transparency_compact_consistency_proofs = 0;
    std::size_t transparency_checkpoint_cosignatures = 0;
    std::size_t transparency_gossip_archives = 0;
    std::size_t transparency_gossip_records = 0;
    std::size_t transparency_gossip_consistent_audits = 0;
    std::size_t transparency_split_view_conflicts = 0;
    double build_ms = 0.0;
    double query_ms = 0.0;
    double update_ms = 0.0;
    double certified_path_ratio = 0.0;
};

class ScopedDirectory {
  public:
    explicit ScopedDirectory(std::filesystem::path path) : path_(std::move(path)) {}
    ScopedDirectory(const ScopedDirectory&) = delete;
    ScopedDirectory& operator=(const ScopedDirectory&) = delete;
    ~ScopedDirectory() {
        if (path_.empty())
            return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

template <typename Function> double elapsed_ms(Function&& function) {
    const auto start = Clock::now();
    function();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<std::string> split(std::string_view input, char delimiter) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= input.size()) {
        const std::size_t end = input.find(delimiter, start);
        fields.emplace_back(
            input.substr(start, end == std::string_view::npos ? input.size() - start : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return fields;
}

bool parse_configuration(std::string_view text, rbfsafe::Configuration& output) {
    output.clear();
    for (const auto& field : split(text, ',')) {
        try {
            std::size_t consumed = 0;
            const double value = std::stod(field, &consumed);
            if (consumed != field.size() || !std::isfinite(value))
                return false;
            output.push_back(value);
        } catch (const std::exception&) {
            return false;
        }
    }
    return !output.empty();
}

bool parse_size(std::string_view text, std::size_t& output) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(std::string(text), &consumed);
        if (consumed != text.size() || value == 0 || value > 10'000'000)
            return false;
        output = static_cast<std::size_t>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--fixtures" && index + 1 < argc) {
            options.fixtures = argv[++index];
        } else if (argument == "--iterations" && index + 1 < argc) {
            if (!parse_size(argv[++index], options.iterations))
                return false;
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--help") {
            options.help = true;
        } else {
            return false;
        }
    }
    return options.help || !options.fixtures.empty();
}

bool load_cases(const std::filesystem::path& fixture_root, std::vector<FixtureCase>& cases,
                std::string& error) {
    std::ifstream input(fixture_root / "cases.tsv");
    if (!input) {
        error = "cannot open fixture cases.tsv";
        return false;
    }
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        const auto fields = split(line, '\t');
        if (fields.size() != 5) {
            error = "invalid fixture field count at line " + std::to_string(line_number);
            return false;
        }
        FixtureCase fixture;
        fixture.name = fields[0];
        fixture.robot = fixture_root / fields[1];
        fixture.scene = fixture_root / fields[2];
        if (fixture.name.empty() || !parse_configuration(fields[3], fixture.start) ||
            !parse_configuration(fields[4], fixture.goal)) {
            error = "invalid fixture values at line " + std::to_string(line_number);
            return false;
        }
        cases.push_back(std::move(fixture));
    }
    if (cases.empty()) {
        error = "fixture cases.tsv contains no cases";
        return false;
    }
    return true;
}

std::size_t estimated_memory(const rbfsafe::SafeAtlas& atlas) {
    std::size_t total = sizeof(atlas);
    for (const auto& region : atlas.regions()) {
        total += sizeof(region) + region.bounds.axes().capacity() * sizeof(rbfsafe::Interval) +
                 region.source_node.path().capacity();
    }
    for (const auto& certificate : atlas.certificates()) {
        total += sizeof(certificate) + certificate.id.capacity() + certificate.robot_digest.capacity() +
                 certificate.scene_digest.capacity() + certificate.policy.algorithm.capacity() +
                 certificate.policy.algorithm_version.capacity() + certificate.subject_digest.capacity() +
                 certificate.parent_certificate_id.capacity() + certificate.transition_digest.capacity();
    }
    for (const auto& dependency : atlas.dependencies())
        total += sizeof(dependency) + dependency.envelope.links.capacity() * sizeof(rbfsafe::WorkspaceAabb);
    for (const auto& neighbors : atlas.adjacency())
        total += sizeof(neighbors) + neighbors.capacity() * sizeof(std::size_t);
    for (const auto& node : atlas.lect().all_nodes()) {
        total += sizeof(node) + node.key.path().capacity() + node.left.path().capacity() +
                 node.right.path().capacity() + node.box.axes().capacity() * sizeof(rbfsafe::Interval);
    }
    return total;
}

void hash_field(std::uint64_t& hash, std::string_view value) {
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    hash ^= 0xffU;
    hash *= 1099511628211ULL;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

bool load_expected_digest(const std::filesystem::path& fixture_root, std::string& digest,
                          std::string& error) {
    std::ifstream input(fixture_root / "logical_digest.txt");
    if (!input || !std::getline(input, digest)) {
        error = "cannot read fixture logical_digest.txt";
        return false;
    }
    if (!digest.empty() && digest.back() == '\r')
        digest.pop_back();
    if (digest.size() != 16 || digest.find_first_not_of("0123456789abcdef") != std::string::npos) {
        error = "fixture logical_digest.txt must contain 16 lowercase hexadecimal digits";
        return false;
    }
    return true;
}

rbfsafe::Result<void> replay_provenance_fixture(const std::filesystem::path& fixture_root,
                                                std::uint64_t& logical_hash) {
    constexpr std::string_view expected_root =
        "af93de1f517b91d348732b72bd08becb9411ee08c1151f5f66da0291740a2865";
    constexpr std::string_view expected_checkpoint =
        "6aa7aa5c91644205fa67e0caf6858a7f11ba0bb03aec2c548ba941624984137b";
    const auto root = fixture_root.parent_path() / "provenance_bundle_schema1";
    auto provenance = rbfsafe::VerifiableProvenanceBundle::load(root / "provenance.json");
    if (!provenance)
        return provenance.error();
    auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(root / "checkpoint.json");
    if (!checkpoint)
        return checkpoint.error();
    auto history = rbfsafe::ServiceTrustHistory::open(root / "trust-history", std::string(expected_root),
                                                      checkpoint.value(), std::string(expected_checkpoint));
    if (!history)
        return history.error();
    auto trust_bundle = history.value().bundle(provenance.value().trust_bundle_id());
    if (!trust_bundle)
        return trust_bundle.error();
    auto audit = rbfsafe::replay_verifiable_provenance(provenance.value(), trust_bundle.value(), 1'000'000);
    if (!audit)
        return audit.error();
    if (!audit.value().ready() ||
        audit.value().hardware.status != rbfsafe::HardwareProvenanceStatus::Satisfied ||
        audit.value().freshness.status != rbfsafe::ExternalTimeFreshnessStatus::Fresh ||
        audit.value().evidence() != rbfsafe::EvidenceLevel::Unknown || audit.value().authorizes_execution()) {
        return rbfsafe::Result<void>::failure(rbfsafe::StatusCode::InternalError,
                                              "release provenance fixture replay was inconsistent");
    }
    hash_field(logical_hash, "hardware-provenance-and-external-time-fresh-but-non-authorizing");
    hash_field(logical_hash, provenance.value().id());
    hash_field(logical_hash, audit.value().hardware.id);
    hash_field(logical_hash, audit.value().freshness.id);
    hash_field(logical_hash, std::to_string(audit.value().hardware.authenticated_statement_count));
    hash_field(logical_hash, std::to_string(audit.value().freshness.authenticated_source_count));
    return rbfsafe::Result<void>::success();
}

rbfsafe::Result<void> replay_continuous_occupancy_fixture(const std::filesystem::path& fixture_root,
                                                          std::uint64_t& logical_hash) {
    constexpr std::array<std::string_view, 2> fixture_names{
        "continuous_fleet_occupancy_schema1",
        "continuous_fleet_occupancy_schema2",
    };
    constexpr std::array<double, 9> identity_rotation{
        1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
    };
    for (const auto fixture_name : fixture_names) {
        const auto root = fixture_root.parent_path() / fixture_name;
        auto robot = rbfsafe::SerialRobotModel::from_json(root / "robot.json");
        if (!robot)
            return robot.error();
        auto bundle = rbfsafe::ContinuousFleetOccupancyBundle::load(root / "occupancy.json");
        if (!bundle)
            return bundle.error();
        std::size_t slices = 0;
        std::size_t rotated_frames = 0;
        std::size_t uncertain_frames = 0;
        for (const auto& occupancy : bundle.value().occupancies()) {
            auto verified = rbfsafe::verify_robot_trajectory_occupancy(robot.value(), occupancy);
            if (!verified)
                return verified.error();
            slices += occupancy.slices.size();
            if (occupancy.workspace_rotation != identity_rotation)
                ++rotated_frames;
            if (occupancy.workspace_angular_uncertainty_radians > 0.0 ||
                std::any_of(occupancy.workspace_translation_uncertainty.begin(),
                            occupancy.workspace_translation_uncertainty.end(),
                            [](double value) { return value > 0.0; })) {
                ++uncertain_frames;
            }
        }
        if (bundle.value().report().status !=
                rbfsafe::ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes ||
            !bundle.value().report().conflicts.empty() ||
            bundle.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
            bundle.value().authorizes_execution()) {
            return rbfsafe::Result<void>::failure(
                rbfsafe::StatusCode::InternalError,
                "release continuous occupancy fixture replay was inconsistent");
        }
        hash_field(logical_hash, "continuous-fleet-swept-link-aabb-separated-but-non-authorizing");
        hash_field(logical_hash, fixture_name);
        hash_field(logical_hash, std::to_string(bundle.value().storage_schema()));
        hash_field(logical_hash, bundle.value().id());
        hash_field(logical_hash, bundle.value().report().id);
        hash_field(logical_hash, std::to_string(bundle.value().occupancies().size()));
        hash_field(logical_hash, std::to_string(slices));
        hash_field(logical_hash, std::to_string(rotated_frames));
        hash_field(logical_hash, std::to_string(uncertain_frames));
        hash_field(logical_hash,
                   rbfsafe::continuous_fleet_occupancy_status_name(bundle.value().report().status));
    }
    return rbfsafe::Result<void>::success();
}

rbfsafe::Result<void>
replay_continuous_robot_scene_occupancy_fixture(const std::filesystem::path& fixture_root,
                                                std::uint64_t& logical_hash) {
    const auto root = fixture_root.parent_path() / "continuous_robot_scene_occupancy_schema1";
    auto robot = rbfsafe::SerialRobotModel::from_json(root / "robot.json");
    if (!robot)
        return robot.error();
    auto bundle = rbfsafe::ContinuousRobotSceneOccupancyBundle::load(root / "occupancy.json");
    if (!bundle)
        return bundle.error();
    std::size_t robot_slices = 0;
    for (const auto& occupancy : bundle.value().robot_occupancies()) {
        auto verified = rbfsafe::verify_robot_trajectory_occupancy(robot.value(), occupancy);
        if (!verified)
            return verified.error();
        robot_slices += occupancy.slices.size();
    }
    std::size_t obstacle_slices = 0;
    for (const auto& occupancy : bundle.value().obstacle_occupancies()) {
        auto verified = rbfsafe::verify_moving_obstacle_occupancy(occupancy);
        if (!verified)
            return verified.error();
        obstacle_slices += occupancy.slices.size();
    }
    if (bundle.value().report().status !=
            rbfsafe::ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes ||
        !bundle.value().report().conflicts.empty() ||
        bundle.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        bundle.value().authorizes_execution()) {
        return rbfsafe::Result<void>::failure(
            rbfsafe::StatusCode::InternalError,
            "release continuous robot-scene occupancy fixture replay was inconsistent");
    }
    hash_field(logical_hash, "continuous-moving-obstacle-swept-aabb-separated-but-non-authorizing");
    hash_field(logical_hash, bundle.value().id());
    hash_field(logical_hash, bundle.value().report().id);
    hash_field(logical_hash, std::to_string(bundle.value().robot_occupancies().size()));
    hash_field(logical_hash, std::to_string(bundle.value().obstacle_occupancies().size()));
    hash_field(logical_hash, std::to_string(robot_slices));
    hash_field(logical_hash, std::to_string(obstacle_slices));
    hash_field(logical_hash,
               rbfsafe::continuous_robot_scene_occupancy_status_name(bundle.value().report().status));
    return rbfsafe::Result<void>::success();
}

rbfsafe::Result<void> replay_occupancy_publication_fixture(const std::filesystem::path& fixture_root,
                                                           std::uint64_t& logical_hash) {
    const auto publication_root = fixture_root.parent_path() / "occupancy_publication_schema1";
    const auto occupancy_path =
        fixture_root.parent_path() / "continuous_fleet_occupancy_schema2" / "occupancy.json";
    auto publication = rbfsafe::OccupancyPublication::load(publication_root / "publication.json");
    if (!publication)
        return publication.error();
    auto trust_bundle = rbfsafe::ServiceTrustBundle::load(publication_root / "trust-bundle.json");
    if (!trust_bundle)
        return trust_bundle.error();
    auto verified = rbfsafe::verify_continuous_fleet_occupancy_publication(
        occupancy_path, publication.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), "", 16);
    if (!verified)
        return verified.error();
    if (publication.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        publication.value().authorizes_execution() ||
        verified.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        verified.value().authorizes_execution()) {
        return rbfsafe::Result<void>::failure(
            rbfsafe::StatusCode::InternalError,
            "release authenticated occupancy publication replay was inconsistent");
    }
    hash_field(logical_hash, "ed25519-authenticated-occupancy-publication-but-non-authorizing");
    hash_field(logical_hash, publication.value().id);
    hash_field(logical_hash, trust_bundle.value().id());
    hash_field(logical_hash, publication.value().occupancy_bundle_id);
    hash_field(logical_hash, publication.value().stream_id);
    hash_field(logical_hash, publication.value().publisher_service_id);
    hash_field(logical_hash, std::to_string(publication.value().publisher_sequence));
    hash_field(logical_hash, verified.value().id);
    hash_field(logical_hash, std::to_string(verified.value().evaluation_tick));
    return rbfsafe::Result<void>::success();
}

rbfsafe::Result<void> replay_occupancy_publication_history_fixture(const std::filesystem::path& fixture_root,
                                                                   std::uint64_t& logical_hash) {
    const auto history_root = fixture_root.parent_path() / "occupancy_publication_history_schema1";
    constexpr std::string_view trust_bundle_id =
        "89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d";
    constexpr std::string_view root_publication_id =
        "90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251";
    constexpr std::string_view head_publication_id =
        "83a6952083ac661aacff43168473c1938e29adfe738275d2230458dd6074dfb9";
    auto history = rbfsafe::OccupancyPublicationHistory::open(
        history_root, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher", trust_bundle_id,
        root_publication_id, head_publication_id);
    if (!history)
        return history.error();
    auto verified = history.value().verify(head_publication_id, 31);
    if (!verified)
        return verified.error();
    auto audit = rbfsafe::audit_occupancy_publication_histories(history.value(), history.value());
    if (!audit)
        return audit.error();
    if (history.value().records().size() != 2 ||
        history.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        history.value().authorizes_execution() ||
        audit.value().relation != rbfsafe::OccupancyPublicationHistoryRelation::Identical ||
        audit.value().fork_detected() || audit.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        audit.value().authorizes_execution()) {
        return rbfsafe::Result<void>::failure(
            rbfsafe::StatusCode::InternalError,
            "release occupancy publication-history replay was inconsistent");
    }
    hash_field(logical_hash, "pinned-replayable-occupancy-publication-history-but-non-authorizing");
    hash_field(logical_hash, history.value().root_publication_id());
    hash_field(logical_hash, history.value().current_publication_id());
    hash_field(logical_hash, history.value().trust_bundle_id());
    hash_field(logical_hash, std::to_string(history.value().records().size()));
    for (const auto& record : history.value().records())
        hash_field(logical_hash, record.id);
    hash_field(logical_hash, verified.value().id);
    hash_field(logical_hash, rbfsafe::occupancy_publication_history_relation_name(audit.value().relation));
    hash_field(logical_hash, audit.value().id);
    return rbfsafe::Result<void>::success();
}

rbfsafe::Result<CaseMetrics> run_case(const FixtureCase& fixture, std::size_t iterations,
                                      std::uint64_t& logical_hash) {
    auto robot = rbfsafe::SerialRobotModel::from_json(fixture.robot);
    if (!robot)
        return robot.error();
    auto scene = rbfsafe::SceneSnapshot::from_json(fixture.scene);
    if (!scene)
        return scene.error();
    auto start_status =
        rbfsafe::validate_configuration(fixture.start, robot.value().dimension(), fixture.name);
    if (!start_status)
        return start_status.error();
    auto goal_status = rbfsafe::validate_configuration(fixture.goal, robot.value().dimension(), fixture.name);
    if (!goal_status)
        return goal_status.error();
    if (!robot.value().configuration_domain().contains(fixture.start) ||
        !robot.value().configuration_domain().contains(fixture.goal)) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InvalidArgument,
                                                     "release fixture exceeds joint limits", fixture.name);
    }

    CaseMetrics metrics;
    metrics.name = fixture.name;
    metrics.dimension = robot.value().dimension();
    rbfsafe::Result<rbfsafe::AtlasBuildResult> built = rbfsafe::Result<rbfsafe::AtlasBuildResult>::failure(
        rbfsafe::StatusCode::InternalError, "benchmark build did not run");
    metrics.build_ms = elapsed_ms([&] {
        rbfsafe::BuildOptions build_options;
        build_options.maximum_depth = 16;
        build_options.maximum_nodes = 100'000;
        built = rbfsafe::AtlasBuilder{}.build(robot.value(), scene.value(), {fixture.start, fixture.goal},
                                              build_options);
    });
    if (!built)
        return built.error();
    auto& atlas = built.value().atlas;
    auto compatibility = atlas.verify_compatible(robot.value(), scene.value());
    if (!compatibility)
        return compatibility.error();
    if (!atlas.contains(fixture.start) || !atlas.contains(fixture.goal)) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture endpoints are not certified", fixture.name);
    }
    metrics.regions = atlas.regions().size();
    metrics.certificates = atlas.certificates().size();
    metrics.estimated_memory_bytes = estimated_memory(atlas);

    const std::vector<rbfsafe::Configuration> trajectory{fixture.start, fixture.goal};
    auto audit = rbfsafe::TrajectoryAuditor{}.audit(atlas, trajectory);
    if (!audit)
        return audit.error();
    metrics.certified_path_ratio = audit.value().coverage_ratio;
    if (audit.value().status != rbfsafe::TrajectoryAuditStatus::Certified) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture path is not certified", fixture.name);
    }

    metrics.queries = iterations;
    rbfsafe::Result<void> query_status = rbfsafe::Result<void>::success();
    metrics.query_ms = elapsed_ms([&] {
        rbfsafe::Configuration query(robot.value().dimension());
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            const double fraction = static_cast<double>(iteration % 1001) / 1000.0;
            for (std::size_t axis = 0; axis < query.size(); ++axis) {
                query[axis] = fixture.start[axis] + fraction * (fixture.goal[axis] - fixture.start[axis]);
            }
            if (!atlas.contains(query)) {
                query_status =
                    rbfsafe::Result<void>::failure(rbfsafe::StatusCode::InternalError,
                                                   "certified fixture query left the Atlas", fixture.name);
                return;
            }
            auto collision_free =
                rbfsafe::configuration_is_collision_free(robot.value(), scene.value(), query);
            if (!collision_free) {
                query_status = collision_free.error();
                return;
            }
            if (!collision_free.value())
                ++metrics.false_safe;
        }
    });
    if (!query_status)
        return query_status.error();
    if (metrics.false_safe != 0) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "certified fixture produced a false-safe sample",
                                                     fixture.name);
    }

    rbfsafe::Configuration delta(fixture.start.size());
    for (std::size_t axis = 0; axis < delta.size(); ++axis)
        delta[axis] = fixture.goal[axis] - fixture.start[axis];
    rbfsafe::RuntimeShield shield;
    auto decision = shield.check_action(robot.value(), scene.value(), atlas, fixture.start,
                                        rbfsafe::ShieldAction{rbfsafe::JointDeltaAction{delta}});
    if (!decision)
        return decision.error();
    if (decision.value().outcome != rbfsafe::ShieldOutcome::Accept) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture action was not accepted", fixture.name);
    }
    rbfsafe::PolicyProposalMetadata policy_metadata;
    policy_metadata.policy_id = "release-policy";
    policy_metadata.task_id = fixture.name;
    policy_metadata.episode_id = "release-fixture";
    policy_metadata.sequence = 1;
    policy_metadata.confidence = 0.9;
    policy_metadata.state_uncertainty = 0.05;
    policy_metadata.action_uncertainty = 0.05;
    policy_metadata.observation_age_seconds = 0.01;
    policy_metadata.inference_latency_seconds = 0.02;
    auto rejected_metadata = policy_metadata;
    rejected_metadata.sequence = 2;
    rejected_metadata.confidence = 0.1;
    const std::vector<rbfsafe::PolicyProposal> proposals{
        {rbfsafe::JointDeltaAction{delta}, policy_metadata},
        {rbfsafe::JointDeltaAction{delta}, rejected_metadata},
    };
    rbfsafe::PolicyGateOptions policy_options;
    policy_options.minimum_confidence = 0.5;
    policy_options.maximum_state_uncertainty = 0.2;
    policy_options.maximum_action_uncertainty = 0.2;
    policy_options.maximum_observation_age_seconds = 0.1;
    policy_options.maximum_inference_latency_seconds = 0.1;
    policy_options.selection_mode = rbfsafe::PolicySelectionMode::HighestConfidence;
    rbfsafe::LearningPolicySafetyGate policy_gate;
    auto policy_report = policy_gate.check_proposals(robot.value(), scene.value(), atlas, fixture.start,
                                                     proposals, policy_options);
    if (!policy_report)
        return policy_report.error();
    if (policy_report.value().selected_index != 0 ||
        policy_report.value().feedback[0].label != rbfsafe::PolicyFeedbackLabel::SelectedAccepted ||
        policy_report.value().feedback[1].label != rbfsafe::PolicyFeedbackLabel::PolicyRejected) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture policy gate was inconsistent", fixture.name);
    }
    auto feedback_database = rbfsafe::PolicyFeedbackDatabase::create(policy_report.value().feedback);
    if (!feedback_database)
        return feedback_database.error();
    metrics.policy_feedback_records = feedback_database.value().records().size();
    rbfsafe::PolicyCalibrationProfileInput calibration_input;
    calibration_input.policy_id = "release-policy";
    calibration_input.policy_model_digest = robot.value().digest();
    calibration_input.scope_id = fixture.name;
    calibration_input.task_id = fixture.name;
    calibration_input.dataset_digest = scene.value().digest();
    calibration_input.method = "release-reliability-bins";
    calibration_input.method_version = "1";
    calibration_input.outcome_definition = "shield accepted or repaired proposal";
    calibration_input.state_uncertainty_unit = "normalized-joint-range-rms";
    calibration_input.action_uncertainty_unit = "normalized-joint-range-rms";
    calibration_input.bins = {{0.0, 0.5, 0.25, 500, 100}, {0.5, 1.0, 0.85, 500, 400}};
    auto calibration = rbfsafe::PolicyCalibrationProfile::create(std::move(calibration_input));
    if (!calibration)
        return calibration.error();
    rbfsafe::CalibratedPolicyGateOptions calibrated_options;
    calibrated_options.maximum_expected_calibration_error = 0.06;
    calibrated_options.maximum_bin_calibration_error = 0.06;
    calibrated_options.policy = policy_options;
    auto calibration_lifecycle = rbfsafe::PolicyCalibrationLifecycle::create(calibration.value());
    if (!calibration_lifecycle)
        return calibration_lifecycle.error();
    rbfsafe::PolicyCalibrationWindowInput calibration_window;
    calibration_window.window_id = fixture.name + "-release-window";
    calibration_window.source_digest = scene.value().digest();
    calibration_window.bins = {{500, 100}, {500, 400}};
    auto drift_report = calibration_lifecycle.value().assess(
        calibration.value(), std::move(calibration_window), calibration_lifecycle.value().current_event_id());
    if (!drift_report)
        return drift_report.error();
    auto calibration_activated = calibration_lifecycle.value().transition(
        calibration.value(), calibration_lifecycle.value().current_event_id(),
        rbfsafe::PolicyCalibrationLifecycleState::Active, "release benchmark review approved");
    if (!calibration_activated)
        return calibration_activated.error();
    rbfsafe::CalibratedPolicySafetyGate calibrated_gate;
    auto calibrated_report = calibrated_gate.check_proposals_guarded(
        calibration.value(), calibration_lifecycle.value(), calibration_lifecycle.value().current_event_id(),
        fixture.name, robot.value().digest(), robot.value(), scene.value(), atlas, fixture.start, proposals,
        calibrated_options);
    if (!calibrated_report || calibrated_report.value().policy_report.selected_index != 0 ||
        calibrated_report.value().applications[0].conservative_confidence >=
            calibrated_report.value().applications[0].raw_metadata.confidence ||
        calibrated_report.value().lifecycle_event_id != calibration_lifecycle.value().current_event_id()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture calibrated policy gate was inconsistent",
            fixture.name);
    }
    metrics.policy_calibration_profiles = 1;
    metrics.policy_calibration_lifecycles = 1;
    rbfsafe::SafetyMemory memory;
    rbfsafe::MemoryArtifactInput memory_input;
    memory_input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    memory_input.deployment_id = fixture.name + "-robot";
    memory_input.robot_digest = robot.value().digest();
    memory_input.scene_digest = scene.value().digest();
    memory_input.task_id = fixture.name;
    memory_input.content_digest = atlas.version_info().id;
    memory_input.locator = "fixtures/" + fixture.name + "/atlas";
    memory_input.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    memory_input.tags = {"release"};
    auto memory_artifact = memory.register_artifact(std::move(memory_input));
    if (!memory_artifact)
        return memory_artifact.error();
    rbfsafe::MemoryReuseQuery reuse_query;
    reuse_query.deployment_id = fixture.name + "-robot";
    reuse_query.robot_digest = robot.value().digest();
    reuse_query.scene_digest = scene.value().digest();
    reuse_query.target_task_id = fixture.name + "-reuse";
    reuse_query.minimum_evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    reuse_query.required_tags = {"release"};
    auto reuse = memory.query_reuse(reuse_query);
    if (!reuse || reuse.value().size() != 1 ||
        reuse.value().front().disposition != rbfsafe::ReuseDisposition::Direct ||
        !reuse.value().front().cross_task) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture memory reuse was inconsistent",
                                                     fixture.name);
    }
    auto reuse_recorded =
        memory.record_reuse(memory_artifact.value().id, reuse_query, "release benchmark reuse");
    if (!reuse_recorded)
        return reuse_recorded.error();
    metrics.memory_reuses = memory.summary().recorded_reuses;

    const std::string authenticated_payload = "release fixture authenticated artifact\n";
    const auto authenticated_bytes =
        std::as_bytes(std::span(authenticated_payload.data(), authenticated_payload.size()));
    std::array<std::byte, 32> authentication_key{};
    for (std::size_t index = 0; index < authentication_key.size(); ++index)
        authentication_key[index] = static_cast<std::byte>(index + 1);
    auto attestation =
        rbfsafe::attest_artifact(memory_artifact.value(), authenticated_bytes, "release-service",
                                 "release-test-key", authentication_key, 1, "application/octet-stream");
    if (!attestation ||
        !rbfsafe::verify_artifact(memory_artifact.value(), authenticated_bytes, attestation.value(),
                                  "release-service", "release-test-key", authentication_key)) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture artifact attestation was inconsistent",
                                                     fixture.name);
    }
    metrics.artifact_attestations = 1;

    rbfsafe::MemoryArtifactInput remote_input;
    remote_input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    remote_input.deployment_id = fixture.name + "-robot";
    remote_input.robot_digest = robot.value().digest();
    remote_input.scene_digest = scene.value().digest();
    remote_input.task_id = fixture.name + "-remote-transfer";
    remote_input.content_digest = "4753e499617943c6b3dfa197752908e793797df2a669c7696ab3e53e534df4bd";
    remote_input.locator = "fixtures/" + fixture.name + "/remote-atlas";
    remote_input.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    remote_input.tags = {"release", "remote"};
    auto remote_artifact = memory.register_artifact(std::move(remote_input));
    if (!remote_artifact)
        return remote_artifact.error();
    const std::string remote_payload = "immutable atlas payload\n";
    const auto remote_bytes = std::as_bytes(std::span(remote_payload.data(), remote_payload.size()));
    auto publish_request =
        rbfsafe::prepare_artifact_publish(memory, remote_artifact.value().id, remote_bytes,
                                          "release-artifact-service", 1, "application/vnd.rbfsafe.atlas");
    if (!publish_request)
        return publish_request.error();
    auto unsigned_receipt = rbfsafe::make_artifact_publish_receipt(publish_request.value(), 2);
    if (!unsigned_receipt)
        return unsigned_receipt.error();
    auto publish_receipt = rbfsafe::authenticate_artifact_publish_receipt(
        unsigned_receipt.value(), "release-test-key", authentication_key);
    if (!publish_receipt)
        return publish_receipt.error();
    auto verified_transfer =
        rbfsafe::verify_artifact_publish(memory, publish_request.value(), publish_receipt.value(),
                                         remote_bytes, "release-test-key", authentication_key);
    if (!verified_transfer)
        return verified_transfer.error();
    rbfsafe::ArtifactTransferJournal transfer_journal;
    auto transfer_record = transfer_journal.append(verified_transfer.value(), "");
    if (!transfer_record || !transfer_journal.valid() || transfer_journal.records().size() != 1) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture remote artifact transfer was inconsistent",
            fixture.name);
    }
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> service_seed{};
    for (std::size_t index = 0; index < service_seed.size(); ++index)
        service_seed[index] = static_cast<std::byte>(index + 1);
    auto service_key_pair = rbfsafe::ed25519_key_pair_from_seed(service_seed);
    if (!service_key_pair)
        return service_key_pair.error();
    auto service_key =
        rbfsafe::make_service_public_key("release-artifact-service", service_key_pair.value().public_key, 1,
                                         0, rbfsafe::ServiceKeyState::Active, true, true, true);
    if (!service_key)
        return service_key.error();
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> governance_seed{};
    for (std::size_t index = 0; index < governance_seed.size(); ++index)
        governance_seed[index] = static_cast<std::byte>(index + 33);
    auto governance_key_pair = rbfsafe::ed25519_key_pair_from_seed(governance_seed);
    if (!governance_key_pair)
        return governance_key_pair.error();
    auto governance_key = rbfsafe::make_service_public_key(
        "release-rotation-governance", governance_key_pair.value().public_key, 1, 0,
        rbfsafe::ServiceKeyState::Active, false, true, true);
    if (!governance_key)
        return governance_key.error();
    rbfsafe::ServiceTrustRotationPolicy rotation_policy;
    rotation_policy.minimum_signatures = 2;
    rotation_policy.require_distinct_services = true;
    auto trust_bundle = rbfsafe::ServiceTrustBundle::create_with_rotation_policy(
        1, "", {service_key.value(), governance_key.value()}, rotation_policy);
    if (!trust_bundle)
        return trust_bundle.error();
    auto public_request = rbfsafe::prepare_artifact_publish(
        memory, remote_artifact.value().id, remote_bytes, "release-artifact-service", 2,
        "application/vnd.rbfsafe.atlas", rbfsafe::ArtifactTransferAuthentication::Ed25519);
    if (!public_request)
        return public_request.error();
    auto unsigned_public_receipt = rbfsafe::make_artifact_publish_receipt(public_request.value(), 3);
    if (!unsigned_public_receipt)
        return unsigned_public_receipt.error();
    auto public_receipt = rbfsafe::sign_artifact_publish_receipt(
        unsigned_public_receipt.value(), service_key.value().id, service_key_pair.value().secret_key);
    if (!public_receipt)
        return public_receipt.error();
    auto public_transfer = rbfsafe::verify_artifact_publish_offline(
        memory, public_request.value(), public_receipt.value(), remote_bytes, trust_bundle.value());
    if (!public_transfer || public_transfer.value().verification_key_id != service_key.value().id ||
        public_transfer.value().trust_bundle_id != trust_bundle.value().id())
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture public-key transfer was inconsistent",
                                                     fixture.name);
    auto public_record =
        transfer_journal.append(public_transfer.value(), transfer_journal.current_record_id());
    if (!public_record || !transfer_journal.valid())
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture public-key transfer journal was inconsistent", fixture.name);
    metrics.artifact_transfers = 2;
    metrics.artifact_transfer_records = transfer_journal.records().size();
    metrics.public_key_transfers = 1;
    metrics.trusted_service_keys = trust_bundle.value().keys().size();
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> successor_seed{};
    for (std::size_t index = 0; index < successor_seed.size(); ++index)
        successor_seed[index] = static_cast<std::byte>(index + 65);
    auto successor_key_pair = rbfsafe::ed25519_key_pair_from_seed(successor_seed);
    if (!successor_key_pair)
        return successor_key_pair.error();
    auto successor_key =
        rbfsafe::make_service_public_key("release-artifact-service", successor_key_pair.value().public_key, 2,
                                         0, rbfsafe::ServiceKeyState::Active, true, true, true);
    if (!successor_key)
        return successor_key.error();
    auto retired_service_key = service_key.value();
    retired_service_key.state = rbfsafe::ServiceKeyState::Retired;
    retired_service_key.valid_through_sequence = 1;
    auto successor_bundle = rbfsafe::rotate_service_trust_bundle(
        trust_bundle.value(), {retired_service_key, successor_key.value(), governance_key.value()});
    if (!successor_bundle)
        return successor_bundle.error();
    auto service_authorization = rbfsafe::authorize_service_trust_bundle_successor(
        trust_bundle.value(), successor_bundle.value(), service_key.value().service_id,
        service_key.value().id, service_key_pair.value().secret_key);
    auto governance_authorization = rbfsafe::authorize_service_trust_bundle_successor(
        trust_bundle.value(), successor_bundle.value(), governance_key.value().service_id,
        governance_key.value().id, governance_key_pair.value().secret_key);
    if (!service_authorization || !governance_authorization) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture trust-bundle signatures were inconsistent",
            fixture.name);
    }
    auto successor_authorizations = rbfsafe::assemble_service_trust_bundle_authorizations(
        trust_bundle.value(), successor_bundle.value(),
        {governance_authorization.value(), service_authorization.value()});
    if (!successor_authorizations ||
        !rbfsafe::verify_service_trust_bundle_successor(trust_bundle.value(), successor_bundle.value(),
                                                        successor_authorizations.value())) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture trust-bundle authorization was inconsistent",
            fixture.name);
    }
    std::error_code temporary_error;
    auto temporary_root = std::filesystem::temp_directory_path(temporary_error);
    if (temporary_error) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::IoError,
                                                     "cannot locate release benchmark temporary directory",
                                                     fixture.name);
    }
    ScopedDirectory trust_history_directory(temporary_root /
                                            ("rbfsafe-release-trust-" + fixture.name + "-" +
                                             std::to_string(Clock::now().time_since_epoch().count())));
    auto trust_history = rbfsafe::ServiceTrustHistory::create(
        trust_history_directory.path(), trust_bundle.value(), trust_bundle.value().id());
    if (!trust_history)
        return trust_history.error();
    auto trust_rotation = trust_history.value().publish(
        successor_bundle.value(), successor_authorizations.value(), trust_bundle.value().id());
    if (!trust_rotation)
        return trust_rotation.error();
    auto replayed_trust_history = rbfsafe::ServiceTrustHistory::open(
        trust_history_directory.path(), trust_bundle.value().id(), successor_bundle.value().id());
    if (!replayed_trust_history || !replayed_trust_history.value().valid() ||
        replayed_trust_history.value().records().size() != 2) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture service trust-history replay was inconsistent", fixture.name);
    }
    auto service_checkpoint_signature = rbfsafe::sign_service_trust_checkpoint(
        replayed_trust_history.value(), successor_key.value().service_id, successor_key.value().id,
        successor_key_pair.value().secret_key);
    auto governance_checkpoint_signature = rbfsafe::sign_service_trust_checkpoint(
        replayed_trust_history.value(), governance_key.value().service_id, governance_key.value().id,
        governance_key_pair.value().secret_key);
    if (!service_checkpoint_signature || !governance_checkpoint_signature) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture trust-checkpoint signatures were inconsistent", fixture.name);
    }
    auto checkpoint = rbfsafe::assemble_service_trust_checkpoint(
        replayed_trust_history.value(),
        {governance_checkpoint_signature.value(), service_checkpoint_signature.value()});
    if (!checkpoint || !rbfsafe::verify_service_trust_checkpoint(replayed_trust_history.value(),
                                                                 checkpoint.value(), checkpoint.value().id)) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture trust checkpoint was inconsistent",
                                                     fixture.name);
    }
    metrics.trust_bundle_authorizations = successor_authorizations.value().authorizations.size();
    metrics.trust_history_records = replayed_trust_history.value().records().size();
    metrics.trust_checkpoint_signatures = checkpoint.value().signatures.size();
    metrics.trust_checkpoints = 1;

    rbfsafe::DeploymentProfileInput deployment_input;
    deployment_input.deployment_id = fixture.name + "-reviewed-deployment";
    deployment_input.robot_digest = robot.value().digest();
    deployment_input.controller_digest = scene.value().digest();
    deployment_input.platform_digest = service_key.value().id;
    deployment_input.runtime_digest = governance_key.value().id;
    deployment_input.trust_root_bundle_id = trust_bundle.value().id();
    deployment_input.trust_checkpoint_id = checkpoint.value().id;
    deployment_input.trust_bundle_id = successor_bundle.value().id();
    deployment_input.trust_bundle_sequence = successor_bundle.value().sequence();
    deployment_input.runtime_constraints.maximum_observation_age_ns = 20'000'000;
    deployment_input.runtime_constraints.maximum_command_latency_ns = 10'000'000;
    deployment_input.runtime_constraints.maximum_control_period_ns = 5'000'000;
    deployment_input.runtime_constraints.maximum_consecutive_missed_cycles = 1;
    deployment_input.review_policy.minimum_approvals = 2;
    deployment_input.review_policy.require_distinct_services = true;
    deployment_input.review_policy.required_roles = {rbfsafe::DeploymentReviewRole::Safety,
                                                     rbfsafe::DeploymentReviewRole::Controls};
    auto deployment_profile = rbfsafe::DeploymentProfile::create(std::move(deployment_input));
    if (!deployment_profile)
        return deployment_profile.error();
    auto safety_approval = rbfsafe::sign_deployment_profile_approval(
        deployment_profile.value(), successor_key.value().service_id, successor_key.value().id,
        rbfsafe::DeploymentReviewRole::Safety, successor_key_pair.value().secret_key);
    auto controls_approval = rbfsafe::sign_deployment_profile_approval(
        deployment_profile.value(), governance_key.value().service_id, governance_key.value().id,
        rbfsafe::DeploymentReviewRole::Controls, governance_key_pair.value().secret_key);
    if (!safety_approval || !controls_approval) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture deployment-profile signatures were inconsistent", fixture.name);
    }
    auto deployment_approvals = rbfsafe::assemble_deployment_profile_approvals(
        deployment_profile.value(), {controls_approval.value(), safety_approval.value()});
    if (!deployment_approvals)
        return deployment_approvals.error();
    auto reviewed_deployment = rbfsafe::ReviewedDeploymentProfile::create(
        deployment_profile.value(), deployment_approvals.value(), replayed_trust_history.value(),
        checkpoint.value(), checkpoint.value().id);
    if (!reviewed_deployment || reviewed_deployment.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture reviewed deployment profile was inconsistent", fixture.name);
    }
    auto deployment_transparency_anchor = rbfsafe::DeploymentTransparencyAnchor::create(
        reviewed_deployment.value(), replayed_trust_history.value(), checkpoint.value(),
        checkpoint.value().id);
    if (!deployment_transparency_anchor ||
        deployment_transparency_anchor.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        deployment_transparency_anchor.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture deployment transparency anchor was inconsistent", fixture.name);
    }
    rbfsafe::DeploymentRuntimeSnapshot deployment_snapshot;
    deployment_snapshot.deployment_id = deployment_profile.value().deployment_id;
    deployment_snapshot.robot_digest = deployment_profile.value().robot_digest;
    deployment_snapshot.controller_digest = deployment_profile.value().controller_digest;
    deployment_snapshot.platform_digest = deployment_profile.value().platform_digest;
    deployment_snapshot.runtime_digest = deployment_profile.value().runtime_digest;
    deployment_snapshot.observation_age_ns = 1'000'000;
    deployment_snapshot.command_latency_ns = 1'000'000;
    deployment_snapshot.control_period_ns = 1'000'000;
    deployment_snapshot.runtime_monitor_active = true;
    deployment_snapshot.fail_closed_transport_active = true;
    deployment_snapshot.authenticated_artifacts = true;
    auto deployment_assessment = reviewed_deployment.value().assess(deployment_snapshot);
    if (!deployment_assessment ||
        deployment_assessment.value().status != rbfsafe::DeploymentProfileAssessmentStatus::Conformant ||
        deployment_assessment.value().evidence != rbfsafe::EvidenceLevel::Unknown ||
        deployment_assessment.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture deployment-profile assessment was inconsistent", fixture.name);
    }
    metrics.deployment_profiles = 1;
    metrics.deployment_profile_approvals = deployment_approvals.value().approvals.size();
    metrics.deployment_profile_assessments = 1;
    metrics.conformant_deployment_profiles = 1;

    auto execution_sequence =
        rbfsafe::ExecutionCommandSequence::create(atlas, {fixture.start, fixture.goal}, {0, 1'000'000});
    if (!execution_sequence)
        return execution_sequence.error();
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> controller_seed{};
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> monitor_seed{};
    for (std::size_t index = 0; index < controller_seed.size(); ++index) {
        controller_seed[index] = static_cast<std::byte>(index + 97);
        monitor_seed[index] = static_cast<std::byte>(index + 129);
    }
    auto controller_key_pair = rbfsafe::ed25519_key_pair_from_seed(controller_seed);
    auto monitor_key_pair = rbfsafe::ed25519_key_pair_from_seed(monitor_seed);
    if (!controller_key_pair || !monitor_key_pair) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture execution endpoint keys were inconsistent",
            fixture.name);
    }
    auto controller_endpoint = rbfsafe::make_execution_endpoint_key(
        fixture.name + "-controller", rbfsafe::ExecutionEndpointRole::Controller,
        controller_key_pair.value().public_key);
    auto monitor_endpoint = rbfsafe::make_execution_endpoint_key(
        fixture.name + "-runtime-monitor", rbfsafe::ExecutionEndpointRole::RuntimeMonitor,
        monitor_key_pair.value().public_key);
    if (!controller_endpoint || !monitor_endpoint) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture execution endpoint identities were inconsistent", fixture.name);
    }
    rbfsafe::ExecutionSessionRequestInput execution_request_input;
    execution_request_input.session_nonce = atlas.version_info().id;
    execution_request_input.controller = controller_endpoint.value();
    execution_request_input.runtime_monitor = monitor_endpoint.value();
    execution_request_input.limits.maximum_start_delay_ns = 1'000'000;
    execution_request_input.limits.maximum_duration_ns = 2'000'000;
    execution_request_input.limits.maximum_commands = 2;
    auto execution_request = rbfsafe::ExecutionSessionRequest::create(
        reviewed_deployment.value(), execution_sequence.value(), execution_request_input);
    if (!execution_request)
        return execution_request.error();
    auto execution_safety_approval = rbfsafe::sign_execution_session_approval(
        execution_request.value(), safety_approval.value(), successor_key_pair.value().secret_key);
    auto execution_controls_approval = rbfsafe::sign_execution_session_approval(
        execution_request.value(), controls_approval.value(), governance_key_pair.value().secret_key);
    if (!execution_safety_approval || !execution_controls_approval) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture execution review signatures were inconsistent", fixture.name);
    }
    auto execution_approvals = rbfsafe::assemble_execution_session_approvals(
        execution_request.value(), reviewed_deployment.value(),
        {execution_controls_approval.value(), execution_safety_approval.value()});
    auto controller_acknowledgement = rbfsafe::sign_execution_controller_acknowledgement(
        execution_request.value(), controller_key_pair.value().secret_key);
    if (!execution_approvals || !controller_acknowledgement) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture execution request approval was inconsistent",
            fixture.name);
    }
    rbfsafe::ExecutionRuntimeObservationInput runtime_observation_input;
    runtime_observation_input.runtime = deployment_snapshot;
    runtime_observation_input.observation_sequence = 1;
    runtime_observation_input.observed_monotonic_ns = 1'000'000'000;
    runtime_observation_input.monitor_state = rbfsafe::ExecutionMonitorState::ArmedCertifiedSequence;
    auto runtime_observation =
        rbfsafe::ExecutionRuntimeObservation::create(execution_request.value(), runtime_observation_input);
    if (!runtime_observation)
        return runtime_observation.error();
    auto monitor_acknowledgement = rbfsafe::sign_execution_monitor_acknowledgement(
        execution_request.value(), runtime_observation.value(), monitor_key_pair.value().secret_key);
    if (!monitor_acknowledgement)
        return monitor_acknowledgement.error();
    auto execution_session = rbfsafe::BoundedExecutionSession::create(
        execution_request.value(), execution_sequence.value(), execution_approvals.value(),
        controller_acknowledgement.value(), monitor_acknowledgement.value(), reviewed_deployment.value(),
        successor_bundle.value(), atlas);
    if (!execution_session || execution_session.value().authorizes_execution() ||
        execution_session.value().evidence() != rbfsafe::EvidenceLevel::Unknown) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture bounded execution session was inconsistent",
            fixture.name);
    }
    auto command_authorization = execution_session.value().authorize_command(1, fixture.goal, 1'001'000'000);
    if (!command_authorization || !command_authorization.value() || !command_authorization.value()->valid() ||
        command_authorization.value()->evidence != rbfsafe::EvidenceLevel::RuntimeExecutable ||
        command_authorization.value()->open_ended()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture exact execution command was not authorized",
            fixture.name);
    }
    auto wrong_execution_command =
        execution_session.value().authorize_command(1, fixture.start, 1'001'000'000);
    if (!wrong_execution_command || wrong_execution_command.value()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture changed execution command was not rejected",
            fixture.name);
    }
    ScopedDirectory execution_ledger_directory(temporary_root /
                                               ("rbfsafe-release-execution-ledger-" + fixture.name + "-" +
                                                std::to_string(Clock::now().time_since_epoch().count())));
    auto execution_ledger =
        rbfsafe::ExecutionLedger::create(execution_ledger_directory.path(), execution_session.value());
    if (!execution_ledger)
        return execution_ledger.error();
    const std::array<std::uint64_t, 2> dispatch_times{1'000'000'000, 1'001'000'000};
    const std::array<std::uint64_t, 2> completion_times{1'000'000'001, 1'001'000'001};
    const std::array<rbfsafe::Configuration, 2> command_configurations{fixture.start, fixture.goal};
    std::optional<rbfsafe::RuntimeObservationAttestationSet> independent_observation;
    for (std::size_t index = 0; index < command_configurations.size(); ++index) {
        auto ledger_decision = execution_ledger.value().authorize_command(
            execution_session.value(), reviewed_deployment.value(), replayed_trust_history.value(),
            checkpoint.value(), checkpoint.value().id, atlas, index, command_configurations[index],
            dispatch_times[index], execution_ledger.value().current_record_id());
        if (!ledger_decision || !ledger_decision.value().authorization ||
            ledger_decision.value().evidence() != rbfsafe::EvidenceLevel::RuntimeExecutable ||
            ledger_decision.value().open_ended()) {
            return rbfsafe::Result<CaseMetrics>::failure(
                rbfsafe::StatusCode::InternalError,
                "release fixture execution ledger withheld an exact command", fixture.name);
        }
        if (index == 0) {
            rbfsafe::IndependentRuntimeObservationInput observation_input;
            observation_input.runtime = deployment_snapshot;
            observation_input.observation_sequence = 2;
            observation_input.observed_monotonic_ns = dispatch_times[index] + 1;
            observation_input.monitor_state = rbfsafe::ExecutionMonitorState::ArmedCertifiedSequence;
            observation_input.configuration_digest = robot.value().digest();
            auto observation = rbfsafe::IndependentRuntimeObservation::create(
                execution_session.value(), execution_ledger.value(), *ledger_decision.value().authorization,
                std::move(observation_input));
            if (!observation)
                return observation.error();
            auto service_observation = rbfsafe::sign_runtime_observation(
                observation.value(), successor_key.value().service_id, successor_key.value().id,
                successor_key_pair.value().secret_key);
            auto governance_observation = rbfsafe::sign_runtime_observation(
                observation.value(), governance_key.value().service_id, governance_key.value().id,
                governance_key_pair.value().secret_key);
            if (!service_observation || !governance_observation) {
                return rbfsafe::Result<CaseMetrics>::failure(
                    rbfsafe::StatusCode::InternalError,
                    "release fixture independent runtime observation signatures were inconsistent",
                    fixture.name);
            }
            rbfsafe::RuntimeObservationPolicy observation_policy;
            observation_policy.minimum_attestations = 2;
            auto assembled_observation = rbfsafe::assemble_runtime_observation_attestations(
                execution_session.value(), observation.value(), observation_policy,
                {governance_observation.value(), service_observation.value()}, successor_bundle.value());
            if (!assembled_observation ||
                assembled_observation.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
                assembled_observation.value().authorizes_execution()) {
                return rbfsafe::Result<CaseMetrics>::failure(
                    rbfsafe::StatusCode::InternalError,
                    "release fixture independent runtime observation quorum was inconsistent", fixture.name);
            }
            independent_observation = std::move(assembled_observation.value());
        }
        rbfsafe::ExecutionControllerCompletionInput completion_input;
        completion_input.outcome = rbfsafe::ExecutionCompletionOutcome::Completed;
        completion_input.completed_monotonic_ns = completion_times[index];
        completion_input.result_digest = index == 0 ? robot.value().digest() : scene.value().digest();
        auto completion = rbfsafe::sign_execution_controller_completion(
            execution_session.value(), *ledger_decision.value().authorization, completion_input,
            controller_key_pair.value().secret_key);
        if (!completion)
            return completion.error();
        auto recorded = execution_ledger.value().record_completion(
            execution_session.value(), reviewed_deployment.value(), replayed_trust_history.value(), atlas,
            completion.value(), execution_ledger.value().current_record_id());
        if (!recorded)
            return recorded.error();
    }
    auto execution_ledger_audit = execution_ledger.value().audit(
        execution_session.value(), reviewed_deployment.value(), replayed_trust_history.value(), atlas);
    if (!execution_ledger_audit ||
        execution_ledger_audit.value().status != rbfsafe::ExecutionLedgerStatus::Completed ||
        execution_ledger_audit.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        execution_ledger_audit.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture execution-ledger audit was inconsistent",
            fixture.name);
    }
    if (!independent_observation) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture did not produce an independent runtime observation", fixture.name);
    }

    std::array<std::byte, rbfsafe::kEd25519SeedBytes> transparency_seed{};
    for (std::size_t index = 0; index < transparency_seed.size(); ++index)
        transparency_seed[index] = static_cast<std::byte>((index + 225) & 0xffU);
    auto transparency_key_pair = rbfsafe::ed25519_key_pair_from_seed(transparency_seed);
    if (!transparency_key_pair)
        return transparency_key_pair.error();
    auto transparency_key =
        rbfsafe::make_service_public_key("release-transparency-log", transparency_key_pair.value().public_key,
                                         1, 0, rbfsafe::ServiceKeyState::Active, false, true, false);
    if (!transparency_key)
        return transparency_key.error();
    auto transparency_identity = rbfsafe::TransparencyLogIdentity::create(
        "rbfsafe-release-fixtures-v1", transparency_key.value().service_id, transparency_key.value().id,
        transparency_key_pair.value().public_key);
    if (!transparency_identity)
        return transparency_identity.error();
    ScopedDirectory transparency_directory(temporary_root /
                                           ("rbfsafe-release-transparency-" + fixture.name + "-" +
                                            std::to_string(Clock::now().time_since_epoch().count())));
    auto transparency_log =
        rbfsafe::TransparencyLog::create(transparency_directory.path(), transparency_identity.value());
    if (!transparency_log)
        return transparency_log.error();
    auto anchor_record = transparency_log.value().publish_deployment_anchor(
        deployment_transparency_anchor.value(), transparency_key_pair.value().secret_key,
        transparency_log.value().current_checkpoint_id());
    if (!anchor_record)
        return anchor_record.error();
    auto observation_record = transparency_log.value().publish_runtime_observation(
        *independent_observation, transparency_key_pair.value().secret_key,
        transparency_log.value().current_checkpoint_id());
    if (!observation_record)
        return observation_record.error();
    auto inclusion_proof = transparency_log.value().inclusion_proof(0);
    auto consistency_witness = transparency_log.value().consistency_witness(1);
    auto compact_consistency_proof = transparency_log.value().compact_consistency_proof(1);
    if (!inclusion_proof || !consistency_witness || !compact_consistency_proof ||
        !rbfsafe::verify_transparency_inclusion(transparency_identity.value(),
                                                observation_record.value().checkpoint,
                                                anchor_record.value().leaf, inclusion_proof.value()) ||
        !rbfsafe::verify_transparency_consistency(
            transparency_identity.value(), anchor_record.value().checkpoint,
            observation_record.value().checkpoint, consistency_witness.value()) ||
        !rbfsafe::verify_transparency_compact_consistency(
            transparency_identity.value(), anchor_record.value().checkpoint,
            observation_record.value().checkpoint, compact_consistency_proof.value())) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture transparency proofs were inconsistent",
                                                     fixture.name);
    }
    auto transparency_audit = transparency_log.value().audit();
    auto reopened_transparency =
        rbfsafe::TransparencyLog::open(transparency_directory.path(), transparency_identity.value(),
                                       transparency_log.value().current_checkpoint_id());
    if (!transparency_audit || !reopened_transparency || transparency_audit.value().verified_records != 2 ||
        transparency_audit.value().deployment_anchor_count != 1 ||
        transparency_audit.value().runtime_observation_count != 1 ||
        transparency_audit.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        transparency_audit.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture transparency log audit was inconsistent",
            fixture.name);
    }

    rbfsafe::TransparencyCheckpointWitnessPolicy witness_policy;
    auto first_service_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), anchor_record.value().checkpoint, successor_bundle.value(),
        successor_key.value().service_id, successor_key.value().id, successor_key_pair.value().secret_key);
    auto first_governance_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), anchor_record.value().checkpoint, successor_bundle.value(),
        governance_key.value().service_id, governance_key.value().id, governance_key_pair.value().secret_key);
    auto second_service_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), observation_record.value().checkpoint, successor_bundle.value(),
        successor_key.value().service_id, successor_key.value().id, successor_key_pair.value().secret_key);
    auto second_governance_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), observation_record.value().checkpoint, successor_bundle.value(),
        governance_key.value().service_id, governance_key.value().id, governance_key_pair.value().secret_key);
    if (!first_service_witness || !first_governance_witness || !second_service_witness ||
        !second_governance_witness) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture transparency witness signatures were inconsistent", fixture.name);
    }
    auto witnessed_first = rbfsafe::assemble_witnessed_transparency_checkpoint(
        transparency_identity.value(), anchor_record.value().checkpoint, witness_policy,
        {first_governance_witness.value(), first_service_witness.value()}, successor_bundle.value());
    auto witnessed_second = rbfsafe::assemble_witnessed_transparency_checkpoint(
        transparency_identity.value(), observation_record.value().checkpoint, witness_policy,
        {second_governance_witness.value(), second_service_witness.value()}, successor_bundle.value());
    if (!witnessed_first || !witnessed_second ||
        !rbfsafe::verify_witnessed_transparency_checkpoint(
            transparency_identity.value(), witnessed_first.value(), successor_bundle.value()) ||
        !rbfsafe::verify_witnessed_transparency_checkpoint(
            transparency_identity.value(), witnessed_second.value(), successor_bundle.value()) ||
        witnessed_second.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        witnessed_second.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture witnessed checkpoints were inconsistent",
            fixture.name);
    }
    auto first_gossip = rbfsafe::sign_transparency_checkpoint_gossip(
        transparency_identity.value(), witnessed_first.value(), std::nullopt, "release-transparency-auditor",
        1, "", successor_bundle.value(), successor_key.value().service_id, successor_key.value().id,
        successor_key_pair.value().secret_key);
    auto second_gossip = rbfsafe::sign_transparency_checkpoint_gossip(
        transparency_identity.value(), witnessed_second.value(), compact_consistency_proof.value(),
        "release-transparency-auditor", 2, first_gossip ? first_gossip.value().id : std::string{},
        successor_bundle.value(), successor_key.value().service_id, successor_key.value().id,
        successor_key_pair.value().secret_key);
    if (!first_gossip || !second_gossip) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError,
            "release fixture authenticated transparency gossip was inconsistent", fixture.name);
    }

    ScopedDirectory gossip_directory(temporary_root /
                                     ("rbfsafe-release-gossip-" + fixture.name + "-" +
                                      std::to_string(Clock::now().time_since_epoch().count())));
    auto gossip_archive = rbfsafe::TransparencyGossipArchive::create(
        gossip_directory.path(), transparency_identity.value(), successor_bundle.value());
    if (!gossip_archive)
        return gossip_archive.error();
    auto first_gossip_record = gossip_archive.value().publish(first_gossip.value(), "");
    auto second_gossip_record =
        first_gossip_record
            ? gossip_archive.value().publish(second_gossip.value(), first_gossip_record.value().id)
            : rbfsafe::Result<rbfsafe::TransparencyGossipRecord>::failure(
                  rbfsafe::StatusCode::InternalError, "first release gossip record was not published");
    auto consistent_gossip_audit = gossip_archive.value().audit();
    if (!first_gossip_record || !second_gossip_record || !consistent_gossip_audit ||
        consistent_gossip_audit.value().status != rbfsafe::TransparencyGossipStatus::Consistent ||
        consistent_gossip_audit.value().linked_checkpoint_pairs != 1 ||
        consistent_gossip_audit.value().evidence() != rbfsafe::EvidenceLevel::Unknown ||
        consistent_gossip_audit.value().authorizes_execution()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture consistent transparency gossip audit failed",
            fixture.name);
    }

    ScopedDirectory fork_directory(temporary_root /
                                   ("rbfsafe-release-transparency-fork-" + fixture.name + "-" +
                                    std::to_string(Clock::now().time_since_epoch().count())));
    auto fork_log = rbfsafe::TransparencyLog::create(fork_directory.path(), transparency_identity.value());
    if (!fork_log)
        return fork_log.error();
    auto fork_first = fork_log.value().publish_deployment_anchor(
        deployment_transparency_anchor.value(), transparency_key_pair.value().secret_key, "");
    auto fork_second =
        fork_first ? fork_log.value().publish_deployment_anchor(deployment_transparency_anchor.value(),
                                                                transparency_key_pair.value().secret_key,
                                                                fork_first.value().checkpoint.id)
                   : rbfsafe::Result<rbfsafe::TransparencyLogRecord>::failure(
                         rbfsafe::StatusCode::InternalError,
                         "first release transparency fork record was not published");
    if (!fork_first || !fork_second ||
        fork_first.value().checkpoint.id != anchor_record.value().checkpoint.id ||
        fork_second.value().checkpoint.tree_size != observation_record.value().checkpoint.tree_size ||
        fork_second.value().checkpoint.id == observation_record.value().checkpoint.id) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture transparency fork was inconsistent",
                                                     fixture.name);
    }
    auto fork_service_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), fork_second.value().checkpoint, successor_bundle.value(),
        successor_key.value().service_id, successor_key.value().id, successor_key_pair.value().secret_key);
    auto fork_governance_witness = rbfsafe::sign_transparency_checkpoint_witness(
        transparency_identity.value(), fork_second.value().checkpoint, successor_bundle.value(),
        governance_key.value().service_id, governance_key.value().id, governance_key_pair.value().secret_key);
    auto witnessed_fork =
        fork_service_witness && fork_governance_witness
            ? rbfsafe::assemble_witnessed_transparency_checkpoint(
                  transparency_identity.value(), fork_second.value().checkpoint, witness_policy,
                  {fork_governance_witness.value(), fork_service_witness.value()}, successor_bundle.value())
            : rbfsafe::Result<rbfsafe::WitnessedTransparencyCheckpoint>::failure(
                  rbfsafe::StatusCode::InternalError, "release transparency fork witnesses were not signed");
    auto fork_compact_proof = fork_log.value().compact_consistency_proof(1);
    auto fork_gossip =
        witnessed_fork && fork_compact_proof
            ? rbfsafe::sign_transparency_checkpoint_gossip(
                  transparency_identity.value(), witnessed_fork.value(), fork_compact_proof.value(),
                  "release-transparency-auditor", 1, "", successor_bundle.value(),
                  governance_key.value().service_id, governance_key.value().id,
                  governance_key_pair.value().secret_key)
            : rbfsafe::Result<rbfsafe::TransparencyCheckpointGossip>::failure(
                  rbfsafe::StatusCode::InternalError, "release transparency fork gossip was not constructed");
    if (!fork_gossip)
        return fork_gossip.error();
    auto fork_gossip_record =
        gossip_archive.value().publish(fork_gossip.value(), gossip_archive.value().current_record_id());
    auto split_view_audit = gossip_archive.value().audit();
    auto reopened_gossip_archive = rbfsafe::TransparencyGossipArchive::open(
        gossip_directory.path(), transparency_identity.value(), successor_bundle.value(),
        successor_bundle.value().id(), gossip_archive.value().current_record_id());
    if (!fork_gossip_record || !split_view_audit || !reopened_gossip_archive ||
        split_view_audit.value().status != rbfsafe::TransparencyGossipStatus::SplitView ||
        split_view_audit.value().conflicts.size() != 1 ||
        split_view_audit.value().conflicts.front().type !=
            rbfsafe::TransparencyGossipConflictType::SameSizeEquivocation) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture transparency split-view audit failed",
                                                     fixture.name);
    }
    metrics.execution_sessions = 1;
    metrics.execution_session_approvals = execution_approvals.value().approvals.size();
    metrics.execution_endpoint_acknowledgements = 2;
    metrics.execution_ledgers = 1;
    metrics.execution_ledger_records = execution_ledger_audit.value().verified_records;
    metrics.execution_controller_completions = execution_ledger_audit.value().completion_count;
    metrics.execution_checkpoint_revalidations = execution_ledger_audit.value().verified_checkpoints;
    metrics.runtime_executable_commands = 3;
    metrics.transparency_logs = 1;
    metrics.transparency_records = transparency_audit.value().verified_records;
    metrics.deployment_transparency_anchors = transparency_audit.value().deployment_anchor_count;
    metrics.runtime_observation_attestations = independent_observation->attestations.size();
    metrics.transparency_inclusion_proofs = 1;
    metrics.transparency_consistency_witnesses = 1;
    metrics.transparency_compact_consistency_proofs = 2;
    metrics.transparency_checkpoint_cosignatures = witnessed_first.value().cosignatures.size() +
                                                   witnessed_second.value().cosignatures.size() +
                                                   witnessed_fork.value().cosignatures.size();
    metrics.transparency_gossip_archives = 1;
    metrics.transparency_gossip_records = gossip_archive.value().records().size();
    metrics.transparency_gossip_consistent_audits = 1;
    metrics.transparency_split_view_conflicts = split_view_audit.value().conflicts.size();
    metrics.memory_artifacts = memory.summary().artifacts;

    const rbfsafe::WorkspaceAabb operating_envelope{{-1.0e6, -1.0e6, -1.0e6}, {1.0e6, 1.0e6, 1.0e6}};
    auto fleet =
        rbfsafe::make_fleet_snapshot(fixture.name + "-fleet", scene.value().digest(),
                                     {{fixture.name + "-robot", robot.value().digest(), operating_envelope}});
    if (!fleet)
        return fleet.error();
    const rbfsafe::WorkspaceAabb declared_occupancy{{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}};
    auto reservation =
        rbfsafe::make_fleet_reservation(fleet.value(), memory, fixture.name + "-robot",
                                        memory_artifact.value().id, declared_occupancy, 0, 1, 0.0);
    if (!reservation)
        return reservation.error();
    const std::vector<rbfsafe::FleetReservation> reservations{reservation.value()};
    auto schedule = rbfsafe::analyze_fleet_schedule(fleet.value(), memory, reservations);
    if (!schedule ||
        schedule.value().status != rbfsafe::FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture fleet schedule was inconsistent",
                                                     fixture.name);
    }
    metrics.fleet_schedule_checks = 1;
    auto schedule_archive = rbfsafe::FleetScheduleArchive::create(fleet.value().fleet_id);
    if (!schedule_archive)
        return schedule_archive.error();
    auto schedule_version = schedule_archive.value().publish(fleet.value(), memory, reservations, "");
    if (!schedule_version || !schedule_archive.value().valid()) {
        return rbfsafe::Result<CaseMetrics>::failure(
            rbfsafe::StatusCode::InternalError, "release fixture fleet schedule archive was inconsistent",
            fixture.name);
    }
    auto verified_schedule =
        schedule_archive.value().verify_version(schedule_version.value().id, fleet.value(), memory);
    if (!verified_schedule || verified_schedule.value().id != schedule.value().id) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture fleet schedule replay was inconsistent",
                                                     fixture.name);
    }
    metrics.fleet_schedule_versions = schedule_archive.value().versions().size();
    rbfsafe::SceneSnapshot next_scene(scene.value().obstacles(), scene.value().version() + "-refresh");
    rbfsafe::Result<rbfsafe::AtlasUpdateResult> updated =
        rbfsafe::Result<rbfsafe::AtlasUpdateResult>::failure(rbfsafe::StatusCode::InternalError,
                                                             "benchmark update did not run");
    metrics.update_ms = elapsed_ms(
        [&] { updated = rbfsafe::AtlasUpdater{}.update(robot.value(), scene.value(), next_scene, atlas); });
    if (!updated)
        return updated.error();
    auto updated_compatibility = updated.value().atlas.verify_compatible(robot.value(), next_scene);
    if (!updated_compatibility)
        return updated_compatibility.error();
    metrics.inherited_certificates = updated.value().stats.certificates_inherited;
    if (!updated.value().atlas.contains(fixture.goal) || metrics.inherited_certificates == 0) {
        return rbfsafe::Result<CaseMetrics>::failure(rbfsafe::StatusCode::InternalError,
                                                     "release fixture update lost certified coverage",
                                                     fixture.name);
    }

    hash_field(logical_hash, fixture.name);
    hash_field(logical_hash, robot.value().digest());
    hash_field(logical_hash, scene.value().digest());
    hash_field(logical_hash, std::to_string(metrics.dimension));
    hash_field(logical_hash, std::to_string(metrics.regions));
    hash_field(logical_hash, std::to_string(metrics.certificates));
    hash_field(logical_hash, std::to_string(metrics.false_safe));
    hash_field(logical_hash, std::to_string(metrics.inherited_certificates));
    hash_field(logical_hash, "trajectory-certified");
    hash_field(logical_hash, "shield-accept");
    hash_field(logical_hash, "policy-selected-accept");
    hash_field(logical_hash, "policy-rejected-low-confidence");
    hash_field(logical_hash, std::to_string(metrics.policy_feedback_records));
    hash_field(logical_hash, "policy-calibration-conservative-selected-accept");
    hash_field(logical_hash, std::to_string(metrics.policy_calibration_profiles));
    hash_field(logical_hash, "policy-calibration-drift-stable");
    hash_field(logical_hash, "policy-calibration-lifecycle-active");
    hash_field(logical_hash, std::to_string(metrics.policy_calibration_lifecycles));
    hash_field(logical_hash, "memory-direct-cross-task-reuse");
    hash_field(logical_hash, std::to_string(metrics.memory_artifacts));
    hash_field(logical_hash, std::to_string(metrics.memory_reuses));
    hash_field(logical_hash, "artifact-attestation-verified");
    hash_field(logical_hash, std::to_string(metrics.artifact_attestations));
    hash_field(logical_hash, "remote-artifact-publish-request-response-authenticated");
    hash_field(logical_hash, std::to_string(metrics.artifact_transfers));
    hash_field(logical_hash, "artifact-transfer-journal-valid");
    hash_field(logical_hash, std::to_string(metrics.artifact_transfer_records));
    hash_field(logical_hash, "ed25519-public-transfer-offline-verified");
    hash_field(logical_hash, std::to_string(metrics.public_key_transfers));
    hash_field(logical_hash, std::to_string(metrics.trusted_service_keys));
    hash_field(logical_hash, "ed25519-quorum-trust-successor-authorized");
    hash_field(logical_hash, std::to_string(metrics.trust_bundle_authorizations));
    hash_field(logical_hash, "service-trust-history-replayed");
    hash_field(logical_hash, std::to_string(metrics.trust_history_records));
    hash_field(logical_hash, "service-trust-checkpoint-verified");
    hash_field(logical_hash, std::to_string(metrics.trust_checkpoint_signatures));
    hash_field(logical_hash, std::to_string(metrics.trust_checkpoints));
    hash_field(logical_hash, "reviewed-deployment-profile-conformant-but-non-executable");
    hash_field(logical_hash, deployment_profile.value().id);
    hash_field(logical_hash, deployment_approvals.value().id);
    hash_field(logical_hash, deployment_assessment.value().id);
    hash_field(logical_hash, std::to_string(metrics.deployment_profiles));
    hash_field(logical_hash, std::to_string(metrics.deployment_profile_approvals));
    hash_field(logical_hash, std::to_string(metrics.deployment_profile_assessments));
    hash_field(logical_hash, std::to_string(metrics.conformant_deployment_profiles));
    hash_field(logical_hash, "bounded-execution-session-verified-but-non-authorizing");
    hash_field(logical_hash, std::to_string(metrics.execution_sessions));
    hash_field(logical_hash, std::to_string(metrics.execution_session_approvals));
    hash_field(logical_hash, std::to_string(metrics.execution_endpoint_acknowledgements));
    hash_field(logical_hash, "exact-command-runtime-executable-closed-window");
    hash_field(logical_hash, std::to_string(metrics.runtime_executable_commands));
    hash_field(logical_hash, "revocation-aware-execution-ledger-completed-offline-audited");
    hash_field(logical_hash, std::to_string(metrics.execution_ledgers));
    hash_field(logical_hash, std::to_string(metrics.execution_ledger_records));
    hash_field(logical_hash, std::to_string(metrics.execution_controller_completions));
    hash_field(logical_hash, std::to_string(metrics.execution_checkpoint_revalidations));
    hash_field(logical_hash, "signed-transparency-log-deployment-and-independent-runtime-observation");
    hash_field(logical_hash, std::to_string(metrics.transparency_logs));
    hash_field(logical_hash, std::to_string(metrics.transparency_records));
    hash_field(logical_hash, std::to_string(metrics.deployment_transparency_anchors));
    hash_field(logical_hash, std::to_string(metrics.runtime_observation_attestations));
    hash_field(logical_hash, std::to_string(metrics.transparency_inclusion_proofs));
    hash_field(logical_hash, std::to_string(metrics.transparency_consistency_witnesses));
    hash_field(logical_hash, "compact-consistency-witness-quorum-gossip-split-view-detected");
    hash_field(logical_hash, std::to_string(metrics.transparency_compact_consistency_proofs));
    hash_field(logical_hash, std::to_string(metrics.transparency_checkpoint_cosignatures));
    hash_field(logical_hash, std::to_string(metrics.transparency_gossip_archives));
    hash_field(logical_hash, std::to_string(metrics.transparency_gossip_records));
    hash_field(logical_hash, std::to_string(metrics.transparency_gossip_consistent_audits));
    hash_field(logical_hash, std::to_string(metrics.transparency_split_view_conflicts));
    hash_field(logical_hash, "fleet-conflict-free-under-declared-envelopes");
    hash_field(logical_hash, std::to_string(metrics.fleet_schedule_checks));
    hash_field(logical_hash, "fleet-schedule-archive-valid");
    hash_field(logical_hash, std::to_string(metrics.fleet_schedule_versions));
    hash_field(logical_hash, "updated-compatible-and-covered");
    return metrics;
}

void print_json(std::span<const CaseMetrics> metrics, std::size_t iterations, std::uint64_t logical_hash) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "{\"schema\":1,\"library_version\":\"" << rbfsafe::kVersion
              << "\",\"iterations\":" << iterations << ",\"logical_digest\":\"" << hex64(logical_hash)
              << "\",\"cases\":[";
    for (std::size_t index = 0; index < metrics.size(); ++index) {
        if (index != 0)
            std::cout << ',';
        const auto& item = metrics[index];
        std::cout << "{\"name\":\"" << item.name << "\",\"dimension\":" << item.dimension
                  << ",\"regions\":" << item.regions << ",\"certificates\":" << item.certificates
                  << ",\"queries\":" << item.queries << ",\"false_safe\":" << item.false_safe
                  << ",\"estimated_memory_bytes\":" << item.estimated_memory_bytes
                  << ",\"inherited_certificates\":" << item.inherited_certificates
                  << ",\"policy_feedback_records\":" << item.policy_feedback_records
                  << ",\"policy_calibration_profiles\":" << item.policy_calibration_profiles
                  << ",\"policy_calibration_lifecycles\":" << item.policy_calibration_lifecycles
                  << ",\"fleet_schedule_versions\":" << item.fleet_schedule_versions
                  << ",\"artifact_attestations\":" << item.artifact_attestations
                  << ",\"artifact_transfers\":" << item.artifact_transfers
                  << ",\"artifact_transfer_records\":" << item.artifact_transfer_records
                  << ",\"public_key_transfers\":" << item.public_key_transfers
                  << ",\"trusted_service_keys\":" << item.trusted_service_keys
                  << ",\"trust_bundle_authorizations\":" << item.trust_bundle_authorizations
                  << ",\"trust_history_records\":" << item.trust_history_records
                  << ",\"trust_checkpoint_signatures\":" << item.trust_checkpoint_signatures
                  << ",\"trust_checkpoints\":" << item.trust_checkpoints
                  << ",\"deployment_profiles\":" << item.deployment_profiles
                  << ",\"deployment_profile_approvals\":" << item.deployment_profile_approvals
                  << ",\"deployment_profile_assessments\":" << item.deployment_profile_assessments
                  << ",\"conformant_deployment_profiles\":" << item.conformant_deployment_profiles
                  << ",\"execution_sessions\":" << item.execution_sessions
                  << ",\"execution_session_approvals\":" << item.execution_session_approvals
                  << ",\"execution_endpoint_acknowledgements\":" << item.execution_endpoint_acknowledgements
                  << ",\"execution_ledgers\":" << item.execution_ledgers
                  << ",\"execution_ledger_records\":" << item.execution_ledger_records
                  << ",\"execution_controller_completions\":" << item.execution_controller_completions
                  << ",\"execution_checkpoint_revalidations\":" << item.execution_checkpoint_revalidations
                  << ",\"runtime_executable_commands\":" << item.runtime_executable_commands
                  << ",\"transparency_logs\":" << item.transparency_logs
                  << ",\"transparency_records\":" << item.transparency_records
                  << ",\"deployment_transparency_anchors\":" << item.deployment_transparency_anchors
                  << ",\"runtime_observation_attestations\":" << item.runtime_observation_attestations
                  << ",\"transparency_inclusion_proofs\":" << item.transparency_inclusion_proofs
                  << ",\"transparency_consistency_witnesses\":" << item.transparency_consistency_witnesses
                  << ",\"transparency_compact_consistency_proofs\":"
                  << item.transparency_compact_consistency_proofs
                  << ",\"transparency_checkpoint_cosignatures\":" << item.transparency_checkpoint_cosignatures
                  << ",\"transparency_gossip_archives\":" << item.transparency_gossip_archives
                  << ",\"transparency_gossip_records\":" << item.transparency_gossip_records
                  << ",\"transparency_gossip_consistent_audits\":"
                  << item.transparency_gossip_consistent_audits
                  << ",\"transparency_split_view_conflicts\":" << item.transparency_split_view_conflicts
                  << ",\"certified_path_ratio\":" << item.certified_path_ratio
                  << ",\"build_ms\":" << item.build_ms << ",\"query_ms\":" << item.query_ms
                  << ",\"update_ms\":" << item.update_ms << '}';
    }
    std::cout << "]}\n";
}

void print_text(std::span<const CaseMetrics> metrics, std::size_t iterations, std::uint64_t logical_hash) {
    std::cout << "RBF-Safe release benchmark version=" << rbfsafe::kVersion << " iterations=" << iterations
              << " digest=" << hex64(logical_hash) << '\n';
    for (const auto& item : metrics) {
        std::cout << item.name << " dimension=" << item.dimension << " regions=" << item.regions
                  << " false_safe=" << item.false_safe << " coverage=" << item.certified_path_ratio
                  << " estimated_memory_bytes=" << item.estimated_memory_bytes
                  << " policy_feedback_records=" << item.policy_feedback_records
                  << " policy_calibration_profiles=" << item.policy_calibration_profiles
                  << " policy_calibration_lifecycles=" << item.policy_calibration_lifecycles
                  << " fleet_schedule_versions=" << item.fleet_schedule_versions
                  << " artifact_attestations=" << item.artifact_attestations << " build_ms=" << item.build_ms
                  << " artifact_transfers=" << item.artifact_transfers
                  << " artifact_transfer_records=" << item.artifact_transfer_records
                  << " public_key_transfers=" << item.public_key_transfers
                  << " trusted_service_keys=" << item.trusted_service_keys
                  << " trust_bundle_authorizations=" << item.trust_bundle_authorizations
                  << " trust_history_records=" << item.trust_history_records
                  << " trust_checkpoint_signatures=" << item.trust_checkpoint_signatures
                  << " trust_checkpoints=" << item.trust_checkpoints
                  << " deployment_profiles=" << item.deployment_profiles
                  << " deployment_profile_approvals=" << item.deployment_profile_approvals
                  << " deployment_profile_assessments=" << item.deployment_profile_assessments
                  << " conformant_deployment_profiles=" << item.conformant_deployment_profiles
                  << " execution_sessions=" << item.execution_sessions
                  << " execution_session_approvals=" << item.execution_session_approvals
                  << " execution_endpoint_acknowledgements=" << item.execution_endpoint_acknowledgements
                  << " execution_ledgers=" << item.execution_ledgers
                  << " execution_ledger_records=" << item.execution_ledger_records
                  << " execution_controller_completions=" << item.execution_controller_completions
                  << " execution_checkpoint_revalidations=" << item.execution_checkpoint_revalidations
                  << " runtime_executable_commands=" << item.runtime_executable_commands
                  << " transparency_logs=" << item.transparency_logs
                  << " transparency_records=" << item.transparency_records
                  << " deployment_transparency_anchors=" << item.deployment_transparency_anchors
                  << " runtime_observation_attestations=" << item.runtime_observation_attestations
                  << " transparency_inclusion_proofs=" << item.transparency_inclusion_proofs
                  << " transparency_consistency_witnesses=" << item.transparency_consistency_witnesses
                  << " transparency_compact_consistency_proofs="
                  << item.transparency_compact_consistency_proofs
                  << " transparency_checkpoint_cosignatures=" << item.transparency_checkpoint_cosignatures
                  << " transparency_gossip_archives=" << item.transparency_gossip_archives
                  << " transparency_gossip_records=" << item.transparency_gossip_records
                  << " transparency_gossip_consistent_audits=" << item.transparency_gossip_consistent_audits
                  << " transparency_split_view_conflicts=" << item.transparency_split_view_conflicts
                  << " query_ms=" << item.query_ms << " update_ms=" << item.update_ms << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "invalid arguments; use --help for usage\n";
        return 2;
    }
    if (options.help) {
        std::cout << "Usage: rbfsafe-release-benchmark --fixtures PATH "
                     "[--iterations N] [--json]\n";
        return 0;
    }
    std::vector<FixtureCase> fixtures;
    std::string error;
    if (!load_cases(options.fixtures, fixtures, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::string expected_digest;
    if (!load_expected_digest(options.fixtures, expected_digest, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::uint64_t logical_hash = 14695981039346656037ULL;
    std::vector<CaseMetrics> metrics;
    metrics.reserve(fixtures.size());
    for (const auto& fixture : fixtures) {
        auto result = run_case(fixture, options.iterations, logical_hash);
        if (!result) {
            std::cerr << result.error().describe() << '\n';
            return 1;
        }
        metrics.push_back(std::move(result).value());
    }
    auto provenance = replay_provenance_fixture(options.fixtures, logical_hash);
    if (!provenance) {
        std::cerr << provenance.error().describe() << '\n';
        return 1;
    }
    auto occupancy = replay_continuous_occupancy_fixture(options.fixtures, logical_hash);
    if (!occupancy) {
        std::cerr << occupancy.error().describe() << '\n';
        return 1;
    }
    auto robot_scene_occupancy =
        replay_continuous_robot_scene_occupancy_fixture(options.fixtures, logical_hash);
    if (!robot_scene_occupancy) {
        std::cerr << robot_scene_occupancy.error().describe() << '\n';
        return 1;
    }
    auto occupancy_publication = replay_occupancy_publication_fixture(options.fixtures, logical_hash);
    if (!occupancy_publication) {
        std::cerr << occupancy_publication.error().describe() << '\n';
        return 1;
    }
    auto occupancy_publication_history =
        replay_occupancy_publication_history_fixture(options.fixtures, logical_hash);
    if (!occupancy_publication_history) {
        std::cerr << occupancy_publication_history.error().describe() << '\n';
        return 1;
    }
    const std::string actual_digest = hex64(logical_hash);
    if (actual_digest != expected_digest) {
        std::cerr << "logical digest mismatch: expected " << expected_digest << ", got " << actual_digest
                  << '\n';
        return 1;
    }
    if (options.json)
        print_json(metrics, options.iterations, logical_hash);
    else
        print_text(metrics, options.iterations, logical_hash);
    return 0;
}
