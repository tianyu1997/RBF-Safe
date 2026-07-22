#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::MemoryArtifactInput artifact_input(std::string deployment, std::string robot, std::string scene,
                                            std::string task, std::string content, std::string locator) {
    rbfsafe::MemoryArtifactInput result;
    result.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    result.deployment_id = std::move(deployment);
    result.robot_digest = std::move(robot);
    result.scene_digest = std::move(scene);
    result.task_id = std::move(task);
    result.content_digest = std::move(content);
    result.locator = std::move(locator);
    result.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    result.tags = {"shelf", "production", "shelf"};
    return result;
}

rbfsafe::WorkspaceAabb box(double x0, double x1, double y0 = -0.1, double y1 = 0.1) {
    return {{x0, y0, -0.1}, {x1, y1, 0.1}};
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

} // namespace

int main() {
    using namespace rbfsafe;
    const auto robot_a = digest('a');
    const auto robot_b = digest('b');
    const auto scene_a = digest('c');
    const auto scene_b = digest('d');

    SafetyMemory memory;
    auto invalid_control =
        artifact_input("arm-a\nbad", robot_a, scene_a, "shelf-pick", digest('0'), "atlases/control");
    auto rejected_control = memory.register_artifact(std::move(invalid_control));
    CHECK(!rejected_control);
    CHECK(rejected_control.error().code == StatusCode::InvalidArgument);
    auto first = memory.register_artifact(
        artifact_input("arm-a", robot_a, scene_a, "shelf-pick", digest('e'), "atlases/pick"));
    CHECK(first);
    CHECK(first.value().id.size() == 64);
    CHECK(first.value().tags.size() == 2);
    CHECK(first.value().registered_sequence == 1);
    CHECK(first.value().generation == 1);
    CHECK(memory.valid());

    auto repeated = memory.register_artifact(
        artifact_input("arm-a", robot_a, scene_a, "shelf-pick", digest('e'), "atlases/pick"));
    CHECK(repeated);
    CHECK(repeated.value().id == first.value().id);
    CHECK(memory.artifacts().size() == 1);
    CHECK(memory.events().size() == 1);

    auto second_input =
        artifact_input("arm-a", robot_a, scene_a, "inspection", digest('f'), "atlases/inspect");
    second_input.type = MemoryArtifactType::SafeCorridor;
    auto second = memory.register_artifact(std::move(second_input));
    CHECK(second);
    CHECK(memory.valid());

    MemoryReuseQuery query;
    query.deployment_id = "arm-a";
    query.robot_digest = robot_a;
    query.scene_digest = scene_a;
    query.target_task_id = "packing";
    query.minimum_evidence = EvidenceLevel::CertifiedRegion;
    query.required_tags = {"production"};
    query.include_revalidation_candidates = true;
    auto candidates = memory.query_reuse(query);
    CHECK(candidates);
    CHECK(candidates.value().size() == 2);
    CHECK(candidates.value()[0].disposition == ReuseDisposition::Direct);
    CHECK(candidates.value()[0].cross_task);
    auto bounded_query = query;
    bounded_query.maximum_results = 1;
    auto bounded_candidates = memory.query_reuse(bounded_query);
    CHECK(!bounded_candidates);
    CHECK(bounded_candidates.error().code == StatusCode::ResourceLimit);
    auto over_registration_limit = memory.register_artifact(
        artifact_input("arm-a", robot_a, scene_a, "welding", digest('3'), "atlases/welding"), 2);
    CHECK(!over_registration_limit);
    CHECK(over_registration_limit.error().code == StatusCode::ResourceLimit);
    CHECK(memory.record_reuse(first.value().id, query, "packing run 17"));
    CHECK(memory.summary().recorded_reuses == 1);
    CHECK(memory.valid());

    auto wrong_scene_query = query;
    wrong_scene_query.scene_digest = scene_b;
    auto revalidation = memory.assess_reuse(first.value().id, wrong_scene_query);
    CHECK(revalidation);
    CHECK(revalidation.value().disposition == ReuseDisposition::RequiresRevalidation);
    auto wrong_robot_query = query;
    wrong_robot_query.robot_digest = robot_b;
    auto ineligible = memory.assess_reuse(first.value().id, wrong_robot_query);
    CHECK(ineligible);
    CHECK(ineligible.value().disposition == ReuseDisposition::Ineligible);

    auto stale = memory.transition(first.value().id, first.value().generation, MemoryArtifactState::Stale,
                                   "scheduled scene refresh");
    CHECK(stale);
    CHECK(stale.value().generation == 2);
    auto stale_reuse = memory.assess_reuse(first.value().id, query);
    CHECK(stale_reuse);
    CHECK(stale_reuse.value().disposition == ReuseDisposition::RequiresRevalidation);
    auto stale_again = memory.transition(first.value().id, first.value().generation,
                                         MemoryArtifactState::Retired, "wrong generation");
    CHECK(!stale_again);
    CHECK(stale_again.error().code == StatusCode::IdentityMismatch);
    auto forbidden = memory.transition(first.value().id, stale.value().generation,
                                       MemoryArtifactState::Active, "forbidden resurrection");
    CHECK(!forbidden);
    CHECK(forbidden.error().code == StatusCode::InvalidArgument);

    auto invalidated = memory.invalidate_scene("arm-a", scene_a, "obstacle layout changed");
    CHECK(invalidated);
    CHECK(invalidated.value() == 1);
    CHECK(memory.summary().stale == 2);
    CHECK(memory.valid());

    auto fixed = SafetyMemory::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "safety_memory_schema1");
    CHECK(fixed);
    CHECK(fixed.value().valid());
    CHECK(fixed.value().artifacts().size() == 2);
    CHECK(fixed.value().events().size() == 3);
    CHECK(fixed.value().next_sequence() == 4);
    CHECK(fixed.value().summary().recorded_reuses == 1);
    CHECK(fixed.value().artifacts().front().id ==
          "91df3038bfb06c60b2436fd242da58c585a037ae10a19c36955c027aa3563f28");

    const auto temporary = std::filesystem::temp_directory_path() /
                           ("rbfsafe-memory-test-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto stored = temporary / "memory";
    std::filesystem::remove_all(temporary);
    CHECK(memory.save(stored));
    CHECK(!memory.save(stored));
    auto loaded = SafetyMemory::load(stored);
    CHECK(loaded);
    CHECK(loaded.value().valid());
    CHECK(loaded.value().artifacts().size() == memory.artifacts().size());
    CHECK(loaded.value().events().size() == memory.events().size());
    CHECK(loaded.value().summary().stale == 2);
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(memory.save(stored, overwrite));

    const auto manifest_path = stored / "manifest.json";
    const auto original_manifest = read_text(manifest_path);
    auto unknown_manifest = original_manifest;
    const auto schema_position = unknown_manifest.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_manifest.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 999");
    write_text(manifest_path, unknown_manifest);
    auto unknown_schema = SafetyMemory::load(stored);
    CHECK(!unknown_schema);
    CHECK(unknown_schema.error().code == StatusCode::IncompatibleFormat);
    write_text(manifest_path, original_manifest);

    SafetyMemoryLoadOptions tiny;
    tiny.maximum_artifacts = 1;
    auto over_limit = SafetyMemory::load(stored, tiny);
    CHECK(!over_limit);
    CHECK(over_limit.error().code == StatusCode::ResourceLimit);
    const auto payload = stored / "memory.json";
    const auto original_payload = read_text(payload);
    write_text(payload, original_payload + " ");
    auto corrupted = SafetyMemory::load(stored);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);
    write_text(payload, original_payload);

    SafetyMemory fleet_memory;
    auto source_a = fleet_memory.register_artifact(
        artifact_input("arm-a", robot_a, scene_a, "assembly", digest('1'), "atlases/arm-a"));
    auto source_b = fleet_memory.register_artifact(
        artifact_input("arm-b", robot_b, scene_a, "assembly", digest('2'), "atlases/arm-b"));
    CHECK(source_a && source_b);
    auto fleet = make_fleet_snapshot(
        "cell-7", scene_a,
        {{"arm-b", robot_b, box(-2.0, 2.0, -2.0, 2.0)}, {"arm-a", robot_a, box(-2.0, 2.0, -2.0, 2.0)}});
    CHECK(fleet);
    CHECK(fleet.value().members.front().deployment_id == "arm-a");
    CHECK(fleet.value().id.size() == 64);

    auto reservation_a = make_fleet_reservation(fleet.value(), fleet_memory, "arm-a", source_a.value().id,
                                                box(-1.0, -0.8), 0, 10, 0.05);
    auto reservation_b = make_fleet_reservation(fleet.value(), fleet_memory, "arm-b", source_b.value().id,
                                                box(0.8, 1.0), 0, 10, 0.05);
    CHECK(reservation_a && reservation_b);
    std::vector<FleetReservation> clear_reservations{reservation_b.value(), reservation_a.value()};
    auto clear = analyze_fleet_schedule(fleet.value(), fleet_memory, clear_reservations);
    CHECK(clear);
    CHECK(clear.value().status == FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes);
    CHECK(clear.value().conflicts.empty());
    CHECK(clear.value().pair_evaluations == 1);
    CHECK(clear.value().id.size() == 64);

    auto colliding_b = make_fleet_reservation(fleet.value(), fleet_memory, "arm-b", source_b.value().id,
                                              box(-0.9, -0.7), 0, 10);
    CHECK(colliding_b);
    std::vector<FleetReservation> colliding{reservation_a.value(), colliding_b.value()};
    auto conflicted = analyze_fleet_schedule(fleet.value(), fleet_memory, colliding);
    CHECK(conflicted);
    CHECK(conflicted.value().status == FleetScheduleStatus::Conflicted);
    CHECK(conflicted.value().conflicts.size() == 1);
    CHECK(conflicted.value().conflicts.front().reason == FleetConflictReason::WorkspaceOverlap);

    auto near_b = make_fleet_reservation(fleet.value(), fleet_memory, "arm-b", source_b.value().id,
                                         box(-0.7, -0.6), 0, 10, 0.2);
    CHECK(near_b);
    std::vector<FleetReservation> near{reservation_a.value(), near_b.value()};
    auto margin_conflict = analyze_fleet_schedule(fleet.value(), fleet_memory, near);
    CHECK(margin_conflict);
    CHECK(margin_conflict.value().conflicts.front().reason == FleetConflictReason::SeparationMarginViolated);

    auto duplicate_a = make_fleet_reservation(fleet.value(), fleet_memory, "arm-a", source_a.value().id,
                                              box(0.2, 0.3), 5, 15);
    CHECK(duplicate_a);
    std::vector<FleetReservation> duplicate{reservation_a.value(), duplicate_a.value()};
    auto duplicate_report = analyze_fleet_schedule(fleet.value(), fleet_memory, duplicate);
    CHECK(duplicate_report);
    CHECK(duplicate_report.value().conflicts.front().reason == FleetConflictReason::DuplicateRobotWindow);

    FleetScheduleOptions budget;
    budget.maximum_pair_evaluations = 1;
    std::vector<FleetReservation> three{reservation_a.value(), reservation_b.value(), duplicate_a.value()};
    auto exhausted = analyze_fleet_schedule(fleet.value(), fleet_memory, three, budget);
    CHECK(!exhausted);
    CHECK(exhausted.error().code == StatusCode::ResourceLimit);
    FleetScheduleOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_report =
        analyze_fleet_schedule(fleet.value(), fleet_memory, clear_reservations, cancelled);
    CHECK(!cancelled_report);
    CHECK(cancelled_report.error().code == StatusCode::Cancelled);

    auto stale_source = fleet_memory.transition(source_a.value().id, source_a.value().generation,
                                                MemoryArtifactState::Stale, "scene changed");
    CHECK(stale_source);
    auto stale_schedule = analyze_fleet_schedule(fleet.value(), fleet_memory, clear_reservations);
    CHECK(!stale_schedule);
    CHECK(stale_schedule.error().code == StatusCode::IdentityMismatch);
    auto rejected_source = make_fleet_reservation(fleet.value(), fleet_memory, "arm-a", source_a.value().id,
                                                  box(-1.0, -0.8), 20, 30);
    CHECK(!rejected_source);
    CHECK(rejected_source.error().code == StatusCode::IdentityMismatch);

    CHECK(memory_artifact_type_name(MemoryArtifactType::SafeAtlas) == "safe_atlas");
    CHECK(memory_artifact_state_name(MemoryArtifactState::Stale) == "stale");
    CHECK(memory_event_type_name(MemoryEventType::ReuseRecorded) == "reuse_recorded");
    CHECK(reuse_disposition_name(ReuseDisposition::Direct) == "direct");
    CHECK(fleet_conflict_reason_name(FleetConflictReason::WorkspaceOverlap) == "workspace_overlap");
    CHECK(fleet_schedule_status_name(FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes) ==
          "conflict_free_under_declared_envelopes");

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
