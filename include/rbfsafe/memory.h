#pragma once

#include <rbfsafe/atlas.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class MemoryArtifactType : std::uint8_t {
    SafeAtlas = 0,
    RegionDatabase = 1,
    SafeCorridor = 2,
    TrajectoryAudit = 3,
    PolicyFeedback = 4,
    RuntimeTrace = 5,
    FleetSchedule = 6,
};

enum class MemoryArtifactState : std::uint8_t {
    Active = 0,
    Stale = 1,
    Quarantined = 2,
    Retired = 3,
};

enum class MemoryEventType : std::uint8_t {
    Registered = 0,
    StateTransition = 1,
    ReuseRecorded = 2,
    SceneInvalidated = 3,
};

enum class ReuseDisposition : std::uint8_t {
    Direct = 0,
    RequiresRevalidation = 1,
    Ineligible = 2,
};

struct MemoryArtifactInput {
    MemoryArtifactType type = MemoryArtifactType::SafeAtlas;
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string task_id;
    std::string content_digest;
    std::string locator;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
    std::vector<std::string> tags;
};

struct MemoryArtifact {
    std::string id;
    MemoryArtifactType type = MemoryArtifactType::SafeAtlas;
    MemoryArtifactState state = MemoryArtifactState::Active;
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string task_id;
    std::string content_digest;
    std::string locator;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
    std::vector<std::string> tags;
    std::uint64_t generation = 0;
    std::uint64_t registered_sequence = 0;
};

struct MemoryEvent {
    std::string id;
    std::uint64_t sequence = 0;
    MemoryEventType type = MemoryEventType::Registered;
    std::string artifact_id;
    MemoryArtifactState previous_state = MemoryArtifactState::Active;
    MemoryArtifactState current_state = MemoryArtifactState::Active;
    std::string task_id;
    std::string detail;
};

struct MemoryReuseQuery {
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string target_task_id;
    std::optional<MemoryArtifactType> type;
    EvidenceLevel minimum_evidence = EvidenceLevel::Unknown;
    std::vector<std::string> required_tags;
    bool include_same_task = true;
    bool include_revalidation_candidates = false;
    std::size_t maximum_results = 100'000;
};

struct MemoryReuseCandidate {
    MemoryArtifact artifact;
    ReuseDisposition disposition = ReuseDisposition::Ineligible;
    bool cross_task = false;
    std::string reason;
};

struct SafetyMemorySummary {
    std::uint64_t artifacts = 0;
    std::uint64_t active = 0;
    std::uint64_t stale = 0;
    std::uint64_t quarantined = 0;
    std::uint64_t retired = 0;
    std::uint64_t events = 0;
    std::uint64_t recorded_reuses = 0;
};

struct SafetyMemoryLoadOptions {
    std::size_t maximum_artifacts = 1'000'000;
    std::size_t maximum_events = 4'000'000;
    std::uintmax_t maximum_payload_bytes = 536'870'912ULL;
};

class SafetyMemory;
Result<void> save_safety_memory_directory(const SafetyMemory& memory, const std::filesystem::path& directory,
                                          const SaveOptions& options);
Result<SafetyMemory> load_safety_memory_directory(const std::filesystem::path& directory,
                                                  const SafetyMemoryLoadOptions& options);

class SafetyMemory {
  public:
    SafetyMemory() = default;

    const std::vector<MemoryArtifact>& artifacts() const noexcept { return artifacts_; }
    const std::vector<MemoryEvent>& events() const noexcept { return events_; }
    std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    std::string identity() const;

    Result<MemoryArtifact> register_artifact(MemoryArtifactInput input,
                                             std::size_t maximum_artifacts = 1'000'000,
                                             std::size_t maximum_events = 4'000'000);
    Result<MemoryArtifact> transition(const std::string& artifact_id, std::uint64_t expected_generation,
                                      MemoryArtifactState target_state, std::string detail,
                                      std::size_t maximum_events = 4'000'000);
    Result<std::size_t> invalidate_scene(const std::string& deployment_id, const std::string& scene_digest,
                                         std::string detail, std::size_t maximum_events = 4'000'000);

    Result<std::optional<MemoryArtifact>> artifact(const std::string& artifact_id) const;
    Result<MemoryReuseCandidate> assess_reuse(const std::string& artifact_id,
                                              const MemoryReuseQuery& query) const;
    Result<std::vector<MemoryReuseCandidate>> query_reuse(const MemoryReuseQuery& query) const;
    Result<void> record_reuse(const std::string& artifact_id, const MemoryReuseQuery& query,
                              std::string detail, std::size_t maximum_events = 4'000'000);

    SafetyMemorySummary summary() const noexcept;
    bool valid() const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<SafetyMemory> load(const std::filesystem::path& directory,
                                     const SafetyMemoryLoadOptions& options = {});

  private:
    friend Result<void> save_safety_memory_directory(const SafetyMemory&, const std::filesystem::path&,
                                                     const SaveOptions&);
    friend Result<SafetyMemory> load_safety_memory_directory(const std::filesystem::path&,
                                                             const SafetyMemoryLoadOptions&);

    std::vector<MemoryArtifact> artifacts_;
    std::vector<MemoryEvent> events_;
    std::uint64_t next_sequence_ = 1;
};

struct SafetyMemoryRevisionInfo {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string memory_id;
};

struct SafetyMemoryStoreOpenOptions {
    std::size_t maximum_revisions = 1'000'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    SafetyMemoryLoadOptions memory_load;
};

class SafetyMemoryStore {
  public:
    static Result<SafetyMemoryStore> create(const std::filesystem::path& directory,
                                            const SafetyMemory& initial_memory);
    static Result<SafetyMemoryStore> open(const std::filesystem::path& directory,
                                          const SafetyMemoryStoreOpenOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& current_revision_id() const noexcept { return current_revision_id_; }
    const std::vector<SafetyMemoryRevisionInfo>& revisions() const noexcept { return revisions_; }

    Result<SafetyMemory> load_current() const;
    Result<SafetyMemory> load_revision(const std::string& revision_id) const;
    Result<SafetyMemoryRevisionInfo> publish(const SafetyMemory& memory,
                                             const std::string& expected_current_revision_id,
                                             std::size_t maximum_revisions = 1'000'000);

  private:
    std::filesystem::path directory_;
    std::string current_revision_id_;
    std::vector<SafetyMemoryRevisionInfo> revisions_;
    SafetyMemoryStoreOpenOptions options_;
};

struct FleetMember {
    std::string deployment_id;
    std::string robot_digest;
    WorkspaceAabb operating_envelope;
};

struct FleetSnapshot {
    std::string id;
    std::string fleet_id;
    std::string scene_digest;
    std::vector<FleetMember> members;
};

Result<FleetSnapshot> make_fleet_snapshot(std::string fleet_id, std::string scene_digest,
                                          std::vector<FleetMember> members);

struct FleetReservation {
    std::string id;
    std::string deployment_id;
    std::string source_artifact_id;
    WorkspaceAabb occupancy;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    double separation_margin = 0.0;
};

Result<FleetReservation> make_fleet_reservation(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                std::string deployment_id, std::string source_artifact_id,
                                                WorkspaceAabb occupancy, std::uint64_t begin_tick,
                                                std::uint64_t end_tick, double separation_margin = 0.0);

enum class FleetConflictReason : std::uint8_t {
    DuplicateRobotWindow = 0,
    WorkspaceOverlap = 1,
    SeparationMarginViolated = 2,
};

struct FleetConflict {
    std::string first_reservation_id;
    std::string second_reservation_id;
    FleetConflictReason reason = FleetConflictReason::WorkspaceOverlap;
    double clearance_lower_bound = 0.0;
    double required_margin = 0.0;
};

enum class FleetScheduleStatus : std::uint8_t {
    ConflictFreeUnderDeclaredEnvelopes = 0,
    Conflicted = 1,
};

struct FleetScheduleOptions {
    std::size_t maximum_reservations = 100'000;
    std::size_t maximum_pair_evaluations = 1'000'000;
    CancellationToken cancellation;
};

struct FleetScheduleReport {
    std::string id;
    std::string fleet_snapshot_id;
    FleetScheduleStatus status = FleetScheduleStatus::Conflicted;
    std::vector<FleetReservation> reservations;
    std::vector<FleetConflict> conflicts;
    std::size_t pair_evaluations = 0;
};

Result<FleetScheduleReport> analyze_fleet_schedule(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                   std::span<const FleetReservation> reservations,
                                                   const FleetScheduleOptions& options = {});

struct FleetScheduleVersion {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string memory_id;
    FleetSnapshot fleet;
    FleetScheduleReport report;
};

struct FleetScheduleArchiveLoadOptions {
    std::size_t maximum_versions = 100'000;
    std::size_t maximum_members = 1'000'000;
    std::size_t maximum_reservations = 1'000'000;
    std::size_t maximum_conflicts = 1'000'000;
    std::size_t maximum_pair_evaluations = 1'000'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class FleetScheduleArchive {
  public:
    static Result<FleetScheduleArchive> create(std::string fleet_id);

    const std::string& fleet_id() const noexcept { return fleet_id_; }
    const std::string& current_version_id() const noexcept { return current_version_id_; }
    const std::vector<FleetScheduleVersion>& versions() const noexcept { return versions_; }
    bool valid() const;

    Result<FleetScheduleVersion> current_version() const;
    Result<FleetScheduleVersion> version(const std::string& version_id) const;
    Result<FleetScheduleVersion> publish(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                         std::span<const FleetReservation> reservations,
                                         const std::string& expected_current_version_id,
                                         const FleetScheduleOptions& schedule_options = {},
                                         std::size_t maximum_versions = 100'000);
    Result<FleetScheduleReport> verify_version(const std::string& version_id, const FleetSnapshot& fleet,
                                               const SafetyMemory& memory,
                                               const FleetScheduleOptions& options = {}) const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<FleetScheduleArchive> load(const std::filesystem::path& directory,
                                             const FleetScheduleArchiveLoadOptions& options = {});

  private:
    std::string fleet_id_;
    std::string current_version_id_;
    std::vector<FleetScheduleVersion> versions_;
};

std::string memory_artifact_type_name(MemoryArtifactType type);
std::string memory_artifact_state_name(MemoryArtifactState state);
std::string memory_event_type_name(MemoryEventType type);
std::string reuse_disposition_name(ReuseDisposition disposition);
std::string fleet_conflict_reason_name(FleetConflictReason reason);
std::string fleet_schedule_status_name(FleetScheduleStatus status);

} // namespace rbfsafe
