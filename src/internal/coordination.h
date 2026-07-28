#pragma once

#include <rbfsafe/coordination.h>

#include <string>

namespace rbfsafe::internal {

std::string occupancy_publication_identity(const OccupancyPublication& publication);
std::string occupancy_publication_signature_message(const OccupancyPublication& publication);
std::string verified_occupancy_publication_identity(const VerifiedOccupancyPublication& verification);
std::string occupancy_publication_history_record_identity(const OccupancyPublicationHistoryRecord& record);
std::string occupancy_publication_history_audit_identity(const OccupancyPublicationHistoryAudit& audit);

} // namespace rbfsafe::internal
