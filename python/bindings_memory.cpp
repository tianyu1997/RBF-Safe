#include "binding_support.h"

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <utility>
#include <vector>

namespace rbfsafe::python_binding {

void bind_memory(py::module_& module) {
    py::enum_<MemoryArtifactType>(module, "MemoryArtifactType")
        .value("SAFE_ATLAS", MemoryArtifactType::SafeAtlas)
        .value("REGION_DATABASE", MemoryArtifactType::RegionDatabase)
        .value("SAFE_CORRIDOR", MemoryArtifactType::SafeCorridor)
        .value("TRAJECTORY_AUDIT", MemoryArtifactType::TrajectoryAudit)
        .value("POLICY_FEEDBACK", MemoryArtifactType::PolicyFeedback)
        .value("RUNTIME_TRACE", MemoryArtifactType::RuntimeTrace)
        .value("FLEET_SCHEDULE", MemoryArtifactType::FleetSchedule);

    py::enum_<MemoryArtifactState>(module, "MemoryArtifactState")
        .value("ACTIVE", MemoryArtifactState::Active)
        .value("STALE", MemoryArtifactState::Stale)
        .value("QUARANTINED", MemoryArtifactState::Quarantined)
        .value("RETIRED", MemoryArtifactState::Retired);

    py::enum_<MemoryEventType>(module, "MemoryEventType")
        .value("REGISTERED", MemoryEventType::Registered)
        .value("STATE_TRANSITION", MemoryEventType::StateTransition)
        .value("REUSE_RECORDED", MemoryEventType::ReuseRecorded)
        .value("SCENE_INVALIDATED", MemoryEventType::SceneInvalidated);

    py::enum_<ReuseDisposition>(module, "ReuseDisposition")
        .value("DIRECT", ReuseDisposition::Direct)
        .value("REQUIRES_REVALIDATION", ReuseDisposition::RequiresRevalidation)
        .value("INELIGIBLE", ReuseDisposition::Ineligible);

    py::class_<MemoryArtifactInput>(module, "MemoryArtifactInput")
        .def(py::init<>())
        .def_readwrite("type", &MemoryArtifactInput::type)
        .def_readwrite("deployment_id", &MemoryArtifactInput::deployment_id)
        .def_readwrite("robot_digest", &MemoryArtifactInput::robot_digest)
        .def_readwrite("scene_digest", &MemoryArtifactInput::scene_digest)
        .def_readwrite("task_id", &MemoryArtifactInput::task_id)
        .def_readwrite("content_digest", &MemoryArtifactInput::content_digest)
        .def_readwrite("locator", &MemoryArtifactInput::locator)
        .def_readwrite("evidence", &MemoryArtifactInput::evidence)
        .def_readwrite("tags", &MemoryArtifactInput::tags);

    py::class_<MemoryArtifact>(module, "MemoryArtifact")
        .def_readonly("id", &MemoryArtifact::id)
        .def_readonly("type", &MemoryArtifact::type)
        .def_readonly("state", &MemoryArtifact::state)
        .def_readonly("deployment_id", &MemoryArtifact::deployment_id)
        .def_readonly("robot_digest", &MemoryArtifact::robot_digest)
        .def_readonly("scene_digest", &MemoryArtifact::scene_digest)
        .def_readonly("task_id", &MemoryArtifact::task_id)
        .def_readonly("content_digest", &MemoryArtifact::content_digest)
        .def_readonly("locator", &MemoryArtifact::locator)
        .def_readonly("evidence", &MemoryArtifact::evidence)
        .def_readonly("tags", &MemoryArtifact::tags)
        .def_readonly("generation", &MemoryArtifact::generation)
        .def_readonly("registered_sequence", &MemoryArtifact::registered_sequence);

    py::class_<MemoryEvent>(module, "MemoryEvent")
        .def_readonly("id", &MemoryEvent::id)
        .def_readonly("sequence", &MemoryEvent::sequence)
        .def_readonly("type", &MemoryEvent::type)
        .def_readonly("artifact_id", &MemoryEvent::artifact_id)
        .def_readonly("previous_state", &MemoryEvent::previous_state)
        .def_readonly("current_state", &MemoryEvent::current_state)
        .def_readonly("task_id", &MemoryEvent::task_id)
        .def_readonly("detail", &MemoryEvent::detail);

    py::class_<MemoryReuseQuery>(module, "MemoryReuseQuery")
        .def(py::init<>())
        .def_readwrite("deployment_id", &MemoryReuseQuery::deployment_id)
        .def_readwrite("robot_digest", &MemoryReuseQuery::robot_digest)
        .def_readwrite("scene_digest", &MemoryReuseQuery::scene_digest)
        .def_readwrite("target_task_id", &MemoryReuseQuery::target_task_id)
        .def_readwrite("type", &MemoryReuseQuery::type)
        .def_readwrite("minimum_evidence", &MemoryReuseQuery::minimum_evidence)
        .def_readwrite("required_tags", &MemoryReuseQuery::required_tags)
        .def_readwrite("include_same_task", &MemoryReuseQuery::include_same_task)
        .def_readwrite("include_revalidation_candidates", &MemoryReuseQuery::include_revalidation_candidates)
        .def_readwrite("maximum_results", &MemoryReuseQuery::maximum_results);

    py::class_<MemoryReuseCandidate>(module, "MemoryReuseCandidate")
        .def_readonly("artifact", &MemoryReuseCandidate::artifact)
        .def_readonly("disposition", &MemoryReuseCandidate::disposition)
        .def_readonly("cross_task", &MemoryReuseCandidate::cross_task)
        .def_readonly("reason", &MemoryReuseCandidate::reason);

    py::class_<SafetyMemorySummary>(module, "SafetyMemorySummary")
        .def_readonly("artifacts", &SafetyMemorySummary::artifacts)
        .def_readonly("active", &SafetyMemorySummary::active)
        .def_readonly("stale", &SafetyMemorySummary::stale)
        .def_readonly("quarantined", &SafetyMemorySummary::quarantined)
        .def_readonly("retired", &SafetyMemorySummary::retired)
        .def_readonly("events", &SafetyMemorySummary::events)
        .def_readonly("recorded_reuses", &SafetyMemorySummary::recorded_reuses);

    py::class_<SafetyMemoryLoadOptions>(module, "SafetyMemoryLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_artifacts", &SafetyMemoryLoadOptions::maximum_artifacts)
        .def_readwrite("maximum_events", &SafetyMemoryLoadOptions::maximum_events)
        .def_readwrite("maximum_payload_bytes", &SafetyMemoryLoadOptions::maximum_payload_bytes);

    py::class_<SafetyMemory>(module, "SafetyMemory")
        .def(py::init<>())
        .def_property_readonly("artifacts", &SafetyMemory::artifacts)
        .def_property_readonly("events", &SafetyMemory::events)
        .def_property_readonly("next_sequence", &SafetyMemory::next_sequence)
        .def_property_readonly("identity", &SafetyMemory::identity)
        .def(
            "register_artifact",
            [](SafetyMemory& memory, MemoryArtifactInput input, std::size_t maximum_artifacts,
               std::size_t maximum_events) {
                return unwrap(memory.register_artifact(std::move(input), maximum_artifacts, maximum_events));
            },
            py::arg("artifact"), py::arg("maximum_artifacts") = 1'000'000,
            py::arg("maximum_events") = 4'000'000)
        .def(
            "transition",
            [](SafetyMemory& memory, const std::string& artifact_id, std::uint64_t expected_generation,
               MemoryArtifactState state, std::string detail, std::size_t maximum_events) {
                return unwrap(memory.transition(artifact_id, expected_generation, state, std::move(detail),
                                                maximum_events));
            },
            py::arg("artifact_id"), py::arg("expected_generation"), py::arg("state"), py::arg("detail"),
            py::arg("maximum_events") = 4'000'000)
        .def(
            "invalidate_scene",
            [](SafetyMemory& memory, const std::string& deployment_id, const std::string& scene_digest,
               std::string detail, std::size_t maximum_events) {
                return unwrap(
                    memory.invalidate_scene(deployment_id, scene_digest, std::move(detail), maximum_events));
            },
            py::arg("deployment_id"), py::arg("scene_digest"), py::arg("detail"),
            py::arg("maximum_events") = 4'000'000)
        .def("artifact",
             [](const SafetyMemory& memory, const std::string& id) { return unwrap(memory.artifact(id)); })
        .def("assess_reuse",
             [](const SafetyMemory& memory, const std::string& id, const MemoryReuseQuery& query) {
                 return unwrap(memory.assess_reuse(id, query));
             })
        .def("query_reuse", [](const SafetyMemory& memory,
                               const MemoryReuseQuery& query) { return unwrap(memory.query_reuse(query)); })
        .def(
            "record_reuse",
            [](SafetyMemory& memory, const std::string& id, const MemoryReuseQuery& query, std::string detail,
               std::size_t maximum_events) {
                unwrap_void(memory.record_reuse(id, query, std::move(detail), maximum_events));
            },
            py::arg("artifact_id"), py::arg("query"), py::arg("detail"),
            py::arg("maximum_events") = 4'000'000)
        .def_property_readonly("summary", &SafetyMemory::summary)
        .def("valid", &SafetyMemory::valid)
        .def(
            "save",
            [](const SafetyMemory& memory, const std::filesystem::path& path, bool overwrite) {
                unwrap_void(memory.save(path, SaveOptions{overwrite}));
            },
            py::arg("path"), py::arg("overwrite") = false)
        .def_static(
            "load",
            [](const std::filesystem::path& path, const SafetyMemoryLoadOptions& options) {
                return unwrap(SafetyMemory::load(path, options));
            },
            py::arg("path"), py::arg("options") = SafetyMemoryLoadOptions{});

    py::class_<SafetyMemoryRevisionInfo>(module, "SafetyMemoryRevisionInfo")
        .def_readonly("sequence", &SafetyMemoryRevisionInfo::sequence)
        .def_readonly("id", &SafetyMemoryRevisionInfo::id)
        .def_readonly("parent_id", &SafetyMemoryRevisionInfo::parent_id)
        .def_readonly("memory_id", &SafetyMemoryRevisionInfo::memory_id);

    py::class_<SafetyMemoryStoreOpenOptions>(module, "SafetyMemoryStoreOpenOptions")
        .def(py::init<>())
        .def_readwrite("maximum_revisions", &SafetyMemoryStoreOpenOptions::maximum_revisions)
        .def_readwrite("maximum_metadata_bytes", &SafetyMemoryStoreOpenOptions::maximum_metadata_bytes)
        .def_readwrite("memory_load", &SafetyMemoryStoreOpenOptions::memory_load);

    py::class_<SafetyMemoryStore>(module, "SafetyMemoryStore")
        .def_static("create",
                    [](const std::filesystem::path& path, const SafetyMemory& memory) {
                        return unwrap(SafetyMemoryStore::create(path, memory));
                    })
        .def_static(
            "open",
            [](const std::filesystem::path& path, const SafetyMemoryStoreOpenOptions& options) {
                return unwrap(SafetyMemoryStore::open(path, options));
            },
            py::arg("path"), py::arg("options") = SafetyMemoryStoreOpenOptions{})
        .def_property_readonly("directory", &SafetyMemoryStore::directory)
        .def_property_readonly("current_revision_id", &SafetyMemoryStore::current_revision_id)
        .def_property_readonly("revisions", &SafetyMemoryStore::revisions)
        .def("load_current", [](const SafetyMemoryStore& store) { return unwrap(store.load_current()); })
        .def("load_revision", [](const SafetyMemoryStore& store,
                                 const std::string& id) { return unwrap(store.load_revision(id)); })
        .def(
            "publish",
            [](SafetyMemoryStore& store, const SafetyMemory& memory, const std::string& expected,
               std::size_t maximum_revisions) {
                return unwrap(store.publish(memory, expected, maximum_revisions));
            },
            py::arg("memory"), py::arg("expected_current_revision_id"),
            py::arg("maximum_revisions") = 1'000'000);

    py::class_<FleetMember>(module, "FleetMember")
        .def(py::init<>())
        .def(py::init<std::string, std::string, WorkspaceAabb>(), py::arg("deployment_id"),
             py::arg("robot_digest"), py::arg("operating_envelope"))
        .def_readwrite("deployment_id", &FleetMember::deployment_id)
        .def_readwrite("robot_digest", &FleetMember::robot_digest)
        .def_readwrite("operating_envelope", &FleetMember::operating_envelope);

    py::class_<FleetSnapshot>(module, "FleetSnapshot")
        .def_readonly("id", &FleetSnapshot::id)
        .def_readonly("fleet_id", &FleetSnapshot::fleet_id)
        .def_readonly("scene_digest", &FleetSnapshot::scene_digest)
        .def_readonly("members", &FleetSnapshot::members);

    py::class_<FleetReservation>(module, "FleetReservation")
        .def_readonly("id", &FleetReservation::id)
        .def_readonly("deployment_id", &FleetReservation::deployment_id)
        .def_readonly("source_artifact_id", &FleetReservation::source_artifact_id)
        .def_readonly("occupancy", &FleetReservation::occupancy)
        .def_readonly("begin_tick", &FleetReservation::begin_tick)
        .def_readonly("end_tick", &FleetReservation::end_tick)
        .def_readonly("separation_margin", &FleetReservation::separation_margin);

    py::enum_<FleetConflictReason>(module, "FleetConflictReason")
        .value("DUPLICATE_ROBOT_WINDOW", FleetConflictReason::DuplicateRobotWindow)
        .value("WORKSPACE_OVERLAP", FleetConflictReason::WorkspaceOverlap)
        .value("SEPARATION_MARGIN_VIOLATED", FleetConflictReason::SeparationMarginViolated);

    py::class_<FleetConflict>(module, "FleetConflict")
        .def_readonly("first_reservation_id", &FleetConflict::first_reservation_id)
        .def_readonly("second_reservation_id", &FleetConflict::second_reservation_id)
        .def_readonly("reason", &FleetConflict::reason)
        .def_readonly("clearance_lower_bound", &FleetConflict::clearance_lower_bound)
        .def_readonly("required_margin", &FleetConflict::required_margin);

    py::enum_<FleetScheduleStatus>(module, "FleetScheduleStatus")
        .value("CONFLICT_FREE_UNDER_DECLARED_ENVELOPES",
               FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes)
        .value("CONFLICTED", FleetScheduleStatus::Conflicted);

    py::class_<FleetScheduleOptions>(module, "FleetScheduleOptions")
        .def(py::init<>())
        .def_readwrite("maximum_reservations", &FleetScheduleOptions::maximum_reservations)
        .def_readwrite("maximum_pair_evaluations", &FleetScheduleOptions::maximum_pair_evaluations)
        .def_readwrite("cancellation", &FleetScheduleOptions::cancellation);

    py::class_<FleetScheduleReport>(module, "FleetScheduleReport")
        .def_readonly("id", &FleetScheduleReport::id)
        .def_readonly("fleet_snapshot_id", &FleetScheduleReport::fleet_snapshot_id)
        .def_readonly("status", &FleetScheduleReport::status)
        .def_readonly("reservations", &FleetScheduleReport::reservations)
        .def_readonly("conflicts", &FleetScheduleReport::conflicts)
        .def_readonly("pair_evaluations", &FleetScheduleReport::pair_evaluations);

    module.def("make_fleet_snapshot", [](std::string fleet_id, std::string scene_digest,
                                         std::vector<FleetMember> members) {
        return unwrap(make_fleet_snapshot(std::move(fleet_id), std::move(scene_digest), std::move(members)));
    });
    module.def(
        "make_fleet_reservation",
        [](const FleetSnapshot& fleet, const SafetyMemory& memory, std::string deployment_id,
           std::string artifact_id, WorkspaceAabb occupancy, std::uint64_t begin_tick, std::uint64_t end_tick,
           double separation_margin) {
            return unwrap(make_fleet_reservation(fleet, memory, std::move(deployment_id),
                                                 std::move(artifact_id), occupancy, begin_tick, end_tick,
                                                 separation_margin));
        },
        py::arg("fleet"), py::arg("memory"), py::arg("deployment_id"), py::arg("artifact_id"),
        py::arg("occupancy"), py::arg("begin_tick"), py::arg("end_tick"), py::arg("separation_margin") = 0.0);
    module.def(
        "analyze_fleet_schedule",
        [](const FleetSnapshot& fleet, const SafetyMemory& memory,
           const std::vector<FleetReservation>& reservations, const FleetScheduleOptions& options) {
            return unwrap(analyze_fleet_schedule(fleet, memory, reservations, options));
        },
        py::arg("fleet"), py::arg("memory"), py::arg("reservations"),
        py::arg("options") = FleetScheduleOptions{});

    module.def("memory_artifact_type_name", &memory_artifact_type_name);
    module.def("memory_artifact_state_name", &memory_artifact_state_name);
    module.def("memory_event_type_name", &memory_event_type_name);
    module.def("reuse_disposition_name", &reuse_disposition_name);
    module.def("fleet_conflict_reason_name", &fleet_conflict_reason_name);
    module.def("fleet_schedule_status_name", &fleet_schedule_status_name);
}

} // namespace rbfsafe::python_binding
