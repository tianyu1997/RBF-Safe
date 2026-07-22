#pragma once

#include <rbfsafe/memory.h>

#include <string>

namespace rbfsafe::internal {

Result<void> validate_memory_artifact(const MemoryArtifact& artifact);
Result<void> validate_memory_event(const MemoryEvent& event);
std::string memory_artifact_identity(const MemoryArtifact& artifact);
std::string memory_event_identity(const MemoryEvent& event);
std::string fleet_snapshot_identity(const FleetSnapshot& fleet);
std::string fleet_reservation_identity(const FleetSnapshot& fleet, const FleetReservation& reservation);
std::string fleet_schedule_identity(const FleetScheduleReport& report);

} // namespace rbfsafe::internal
