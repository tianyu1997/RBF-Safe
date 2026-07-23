#include "internal/sha256.h"
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
    const auto first_memory_id = memory.identity();
    CHECK(first_memory_id.size() == 64);

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
    CHECK(memory.identity() != first_memory_id);

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
    CHECK(fixed.value().identity() == "b79cb94a4ce614b5800a14c4b89cc5059da88753d0348341b81e49636958f281");
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

    const auto store_path = temporary / "memory-store";
    auto store = SafetyMemoryStore::create(store_path, fixed.value());
    CHECK(store);
    CHECK(store.value().revisions().size() == 1);
    CHECK(store.value().revisions().front().sequence == 0);
    CHECK(store.value().revisions().front().parent_id.empty());
    CHECK(store.value().revisions().front().memory_id == fixed.value().identity());
    CHECK(store.value().current_revision_id() == store.value().revisions().front().id);
    auto reopened_store = SafetyMemoryStore::open(store_path);
    CHECK(reopened_store);
    auto root_memory = reopened_store.value().load_current();
    CHECK(root_memory);
    CHECK(root_memory.value().identity() == fixed.value().identity());

    auto next_memory = fixed.value();
    const auto root_id = store.value().current_revision_id();
    auto next_transition =
        next_memory.transition(next_memory.artifacts().front().id, next_memory.artifacts().front().generation,
                               MemoryArtifactState::Stale, "maintenance window");
    CHECK(next_transition);
    auto published = store.value().publish(next_memory, root_id);
    CHECK(published);
    CHECK(published.value().sequence == 1);
    CHECK(published.value().parent_id == root_id);
    CHECK(published.value().memory_id == next_memory.identity());
    CHECK(store.value().revisions().size() == 2);
    auto current_memory = store.value().load_current();
    CHECK(current_memory);
    CHECK(current_memory.value().summary().stale == 1);
    auto historical_memory = store.value().load_revision(root_id);
    CHECK(historical_memory);
    CHECK(historical_memory.value().summary().stale == 0);

    auto idempotent = store.value().publish(next_memory, published.value().id);
    CHECK(idempotent);
    CHECK(idempotent.value().id == published.value().id);
    CHECK(store.value().revisions().size() == 2);
    auto stale_publish = reopened_store.value().publish(next_memory, root_id);
    CHECK(!stale_publish);
    CHECK(stale_publish.error().code == StatusCode::IdentityMismatch);

    auto third_memory = next_memory;
    auto third_transition = third_memory.transition(third_memory.artifacts().front().id,
                                                    third_memory.artifacts().front().generation,
                                                    MemoryArtifactState::Retired, "retired by operator");
    CHECK(third_transition);
    auto revision_limit = store.value().publish(third_memory, published.value().id, 2);
    CHECK(!revision_limit);
    CHECK(revision_limit.error().code == StatusCode::ResourceLimit);
    CHECK(std::filesystem::create_directory(store_path / ".writer-lock"));
    auto locked_publish = store.value().publish(third_memory, published.value().id);
    CHECK(!locked_publish);
    CHECK(locked_publish.error().code == StatusCode::ResourceLimit);
    CHECK(std::filesystem::remove(store_path / ".writer-lock"));

    SafetyMemoryStoreOpenOptions one_revision;
    one_revision.maximum_revisions = 1;
    auto bounded_store = SafetyMemoryStore::open(store_path, one_revision);
    CHECK(!bounded_store);
    CHECK(bounded_store.error().code == StatusCode::ResourceLimit);
    SafetyMemoryStoreOpenOptions tiny_metadata;
    tiny_metadata.maximum_metadata_bytes = 1;
    auto oversized_metadata = SafetyMemoryStore::open(store_path, tiny_metadata);
    CHECK(!oversized_metadata);
    CHECK(oversized_metadata.error().code == StatusCode::ResourceLimit);

    const auto store_manifest_path = store_path / "manifest.json";
    const auto original_store_manifest = read_text(store_manifest_path);
    auto unknown_store_manifest = original_store_manifest;
    const auto store_schema_position = unknown_store_manifest.find("\"schema\": 1");
    CHECK(store_schema_position != std::string::npos);
    unknown_store_manifest.replace(store_schema_position, std::string("\"schema\": 1").size(),
                                   "\"schema\": 999");
    write_text(store_manifest_path, unknown_store_manifest);
    auto unknown_store_schema = SafetyMemoryStore::open(store_path);
    CHECK(!unknown_store_schema);
    CHECK(unknown_store_schema.error().code == StatusCode::IncompatibleFormat);
    write_text(store_manifest_path, original_store_manifest);
    CHECK(SafetyMemoryStore::open(store_path));

    std::filesystem::path current_commit_path;
    for (const auto& entry : std::filesystem::directory_iterator(store_path / "commits")) {
        if (entry.path().filename().string().find(published.value().id) != std::string::npos)
            current_commit_path = entry.path();
    }
    CHECK(!current_commit_path.empty());
    const auto original_commit = read_text(current_commit_path);
    auto corrupted_commit = original_commit;
    const auto memory_id_position = corrupted_commit.find(published.value().memory_id);
    CHECK(memory_id_position != std::string::npos);
    corrupted_commit[memory_id_position] = corrupted_commit[memory_id_position] == '0' ? '1' : '0';
    write_text(current_commit_path, corrupted_commit);
    auto invalid_commit = SafetyMemoryStore::open(store_path);
    CHECK(!invalid_commit);
    CHECK(invalid_commit.error().code == StatusCode::CorruptData);
    write_text(current_commit_path, original_commit);

    const auto current_payload_path = store_path / "revisions" / published.value().id / "memory.json";
    const auto staged_payload_path = current_payload_path.parent_path() / "memory.json.missing";
    std::filesystem::rename(current_payload_path, staged_payload_path);
    auto missing_payload_store = SafetyMemoryStore::open(store_path);
    CHECK(!missing_payload_store);
    CHECK(missing_payload_store.error().code == StatusCode::CorruptData);
    std::filesystem::rename(staged_payload_path, current_payload_path);
    CHECK(SafetyMemoryStore::open(store_path));

    auto fixed_store =
        SafetyMemoryStore::open(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "safety_memory_store_schema1");
    CHECK(fixed_store);
    CHECK(fixed_store.value().revisions().size() == 2);
    CHECK(fixed_store.value().revisions().front().id ==
          "033a8a61cc33066599d49f31c377540537b011ae1ab8963a2640d6c9743a483e");
    CHECK(fixed_store.value().current_revision_id() ==
          "1c361b8f1434e63f4a3e0f872a28db418bb208aa3fa4f0d63429d1997d4edc29");
    auto fixed_store_current = fixed_store.value().load_current();
    CHECK(fixed_store_current);
    CHECK(fixed_store_current.value().identity() ==
          "192ac7eb3b27a3d0575f91de68b3180338e50d36c09b3fe3fe45a54099e86c0d");
    CHECK(fixed_store_current.value().summary().stale == 1);

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

    CHECK(!FleetScheduleArchive::create("cell-7\nbad"));
    auto archive = FleetScheduleArchive::create("cell-7");
    CHECK(archive);
    CHECK(archive.value().valid());
    CHECK(archive.value().versions().empty());
    CHECK(!archive.value().current_version());
    CHECK(!archive.value().version(digest('9')));
    const auto empty_archive_path = temporary / "empty-fleet-schedule-archive";
    CHECK(archive.value().save(empty_archive_path));
    auto loaded_empty_archive = FleetScheduleArchive::load(empty_archive_path);
    CHECK(loaded_empty_archive);
    CHECK(loaded_empty_archive.value().valid());
    CHECK(loaded_empty_archive.value().versions().empty());
    CHECK(loaded_empty_archive.value().current_version_id().empty());
    auto first_schedule = archive.value().publish(fleet.value(), fleet_memory, clear_reservations, "");
    CHECK(first_schedule);
    CHECK(first_schedule.value().sequence == 0);
    CHECK(first_schedule.value().parent_id.empty());
    CHECK(first_schedule.value().memory_id == fleet_memory.identity());
    CHECK(first_schedule.value().report.id == clear.value().id);
    CHECK(archive.value().current_version_id() == first_schedule.value().id);
    CHECK(archive.value().valid());
    auto idempotent_schedule =
        archive.value().publish(fleet.value(), fleet_memory, clear_reservations, first_schedule.value().id);
    CHECK(idempotent_schedule);
    CHECK(idempotent_schedule.value().id == first_schedule.value().id);
    CHECK(archive.value().versions().size() == 1);
    auto stale_schedule_head = archive.value().publish(fleet.value(), fleet_memory, colliding, "");
    CHECK(!stale_schedule_head);
    CHECK(stale_schedule_head.error().code == StatusCode::IdentityMismatch);
    auto second_schedule =
        archive.value().publish(fleet.value(), fleet_memory, colliding, first_schedule.value().id);
    CHECK(second_schedule);
    CHECK(second_schedule.value().sequence == 1);
    CHECK(second_schedule.value().parent_id == first_schedule.value().id);
    CHECK(second_schedule.value().report.id == conflicted.value().id);
    CHECK(archive.value().versions().size() == 2);
    auto replayed_schedule =
        archive.value().verify_version(first_schedule.value().id, fleet.value(), fleet_memory);
    CHECK(replayed_schedule);
    CHECK(replayed_schedule.value().id == clear.value().id);
    auto selected_schedule = archive.value().version(first_schedule.value().id);
    CHECK(selected_schedule);
    CHECK(selected_schedule.value().report.status == FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes);

    auto fixed_archive = FleetScheduleArchive::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                    "fleet_schedule_archive_schema1");
    CHECK(fixed_archive);
    CHECK(fixed_archive.value().valid());
    CHECK(fixed_archive.value().fleet_id() == "cell-1");
    CHECK(fixed_archive.value().versions().size() == 2);
    CHECK(fixed_archive.value().versions().front().id ==
          "850f934f814d11a61c0f3c6fd10b514bb75d61b141d7735dbfc17abacaf325f8");
    CHECK(fixed_archive.value().current_version_id() ==
          "e87638cd2e4d302840138c416c4987fc3800951b078793ad2ed1ea351dc751aa");
    CHECK(fixed_archive.value().current_version().value().report.status == FleetScheduleStatus::Conflicted);

    const auto fleet_archive_path = temporary / "fleet-schedule-archive";
    CHECK(archive.value().save(fleet_archive_path));
    CHECK(!archive.value().save(fleet_archive_path));
    SaveOptions overwrite_archive;
    overwrite_archive.overwrite = true;
    CHECK(archive.value().save(fleet_archive_path, overwrite_archive));
    auto loaded_archive = FleetScheduleArchive::load(fleet_archive_path);
    CHECK(loaded_archive);
    CHECK(loaded_archive.value().fleet_id() == "cell-7");
    CHECK(loaded_archive.value().current_version_id() == second_schedule.value().id);
    CHECK(loaded_archive.value().versions().size() == 2);
    CHECK(loaded_archive.value().valid());
    CHECK(loaded_archive.value().current_version().value().report.status == FleetScheduleStatus::Conflicted);
    FleetScheduleArchiveLoadOptions one_schedule;
    one_schedule.maximum_versions = 1;
    auto schedule_version_limit = FleetScheduleArchive::load(fleet_archive_path, one_schedule);
    CHECK(!schedule_version_limit);
    CHECK(schedule_version_limit.error().code == StatusCode::ResourceLimit);
    FleetScheduleArchiveLoadOptions one_pair;
    one_pair.maximum_pair_evaluations = 1;
    auto schedule_pair_limit = FleetScheduleArchive::load(fleet_archive_path, one_pair);
    CHECK(!schedule_pair_limit);
    CHECK(schedule_pair_limit.error().code == StatusCode::ResourceLimit);
    FleetScheduleArchiveLoadOptions three_members;
    three_members.maximum_members = 3;
    auto schedule_member_limit = FleetScheduleArchive::load(fleet_archive_path, three_members);
    CHECK(!schedule_member_limit);
    CHECK(schedule_member_limit.error().code == StatusCode::ResourceLimit);
    FleetScheduleArchiveLoadOptions three_reservations;
    three_reservations.maximum_reservations = 3;
    auto schedule_reservation_limit = FleetScheduleArchive::load(fleet_archive_path, three_reservations);
    CHECK(!schedule_reservation_limit);
    CHECK(schedule_reservation_limit.error().code == StatusCode::ResourceLimit);
    FleetScheduleArchiveLoadOptions tiny_archive_metadata;
    tiny_archive_metadata.maximum_metadata_bytes = 1;
    auto schedule_metadata_limit = FleetScheduleArchive::load(fleet_archive_path, tiny_archive_metadata);
    CHECK(!schedule_metadata_limit);
    CHECK(schedule_metadata_limit.error().code == StatusCode::ResourceLimit);
    FleetScheduleArchiveLoadOptions tiny_archive_payload;
    tiny_archive_payload.maximum_payload_bytes = 1;
    auto schedule_payload_limit = FleetScheduleArchive::load(fleet_archive_path, tiny_archive_payload);
    CHECK(!schedule_payload_limit);
    CHECK(schedule_payload_limit.error().code == StatusCode::ResourceLimit);

    const auto fleet_manifest_path = fleet_archive_path / "manifest.json";
    const auto original_fleet_manifest = read_text(fleet_manifest_path);
    auto unknown_fleet_manifest = original_fleet_manifest;
    const auto fleet_schema_position = unknown_fleet_manifest.find("\"schema\": 1");
    CHECK(fleet_schema_position != std::string::npos);
    unknown_fleet_manifest.replace(fleet_schema_position, std::string("\"schema\": 1").size(),
                                   "\"schema\": 999");
    write_text(fleet_manifest_path, unknown_fleet_manifest);
    auto unknown_fleet_schema = FleetScheduleArchive::load(fleet_archive_path);
    CHECK(!unknown_fleet_schema);
    CHECK(unknown_fleet_schema.error().code == StatusCode::IncompatibleFormat);
    write_text(fleet_manifest_path, original_fleet_manifest);
    const auto fleet_payload_path = fleet_archive_path / "schedules.json";
    const auto original_fleet_payload = read_text(fleet_payload_path);
    write_text(fleet_payload_path, original_fleet_payload + " ");
    auto corrupted_fleet_archive = FleetScheduleArchive::load(fleet_archive_path);
    CHECK(!corrupted_fleet_archive);
    CHECK(corrupted_fleet_archive.error().code == StatusCode::CorruptData);
    write_text(fleet_payload_path, original_fleet_payload);
    CHECK(FleetScheduleArchive::load(fleet_archive_path));

    auto semantic_payload = original_fleet_payload;
    const auto status_position = semantic_payload.rfind("\"status\": 1");
    CHECK(status_position != std::string::npos);
    semantic_payload.replace(status_position, std::string("\"status\": 1").size(), "\"status\": 0");
    auto semantic_manifest = original_fleet_manifest;
    const auto original_payload_sha = internal::sha256(original_fleet_payload);
    const auto semantic_payload_sha = internal::sha256(semantic_payload);
    const auto checksum_position = semantic_manifest.find(original_payload_sha);
    CHECK(checksum_position != std::string::npos);
    semantic_manifest.replace(checksum_position, original_payload_sha.size(), semantic_payload_sha);
    write_text(fleet_payload_path, semantic_payload);
    write_text(fleet_manifest_path, semantic_manifest);
    auto semantically_corrupt_archive = FleetScheduleArchive::load(fleet_archive_path);
    CHECK(!semantically_corrupt_archive);
    CHECK(semantically_corrupt_archive.error().code == StatusCode::CorruptData);
    write_text(fleet_payload_path, original_fleet_payload);
    write_text(fleet_manifest_path, original_fleet_manifest);
    CHECK(FleetScheduleArchive::load(fleet_archive_path));

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
    auto stale_archive_replay =
        archive.value().verify_version(second_schedule.value().id, fleet.value(), fleet_memory);
    CHECK(!stale_archive_replay);
    CHECK(stale_archive_replay.error().code == StatusCode::IdentityMismatch);
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
