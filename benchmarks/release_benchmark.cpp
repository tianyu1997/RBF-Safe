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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
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
    std::size_t memory_artifacts = 0;
    std::size_t memory_reuses = 0;
    std::size_t fleet_schedule_checks = 0;
    std::size_t fleet_schedule_versions = 0;
    std::size_t artifact_attestations = 0;
    double build_ms = 0.0;
    double query_ms = 0.0;
    double update_ms = 0.0;
    double certified_path_ratio = 0.0;
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
    metrics.memory_artifacts = memory.summary().artifacts;
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
    hash_field(logical_hash, "memory-direct-cross-task-reuse");
    hash_field(logical_hash, memory.identity());
    hash_field(logical_hash, std::to_string(metrics.memory_artifacts));
    hash_field(logical_hash, std::to_string(metrics.memory_reuses));
    hash_field(logical_hash, "artifact-attestation-verified");
    hash_field(logical_hash, attestation.value().id);
    hash_field(logical_hash, std::to_string(metrics.artifact_attestations));
    hash_field(logical_hash, "fleet-conflict-free-under-declared-envelopes");
    hash_field(logical_hash, std::to_string(metrics.fleet_schedule_checks));
    hash_field(logical_hash, "fleet-schedule-archive-valid");
    hash_field(logical_hash, schedule_version.value().id);
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
                  << ",\"fleet_schedule_versions\":" << item.fleet_schedule_versions
                  << ",\"artifact_attestations\":" << item.artifact_attestations
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
                  << " fleet_schedule_versions=" << item.fleet_schedule_versions
                  << " artifact_attestations=" << item.artifact_attestations << " build_ms=" << item.build_ms
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
