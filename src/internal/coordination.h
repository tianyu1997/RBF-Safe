#pragma once

#include <rbfsafe/coordination.h>

#include <string>

namespace rbfsafe::internal {

std::string occupancy_publication_identity(const OccupancyPublication& publication);
std::string occupancy_publication_signature_message(const OccupancyPublication& publication);
std::string verified_occupancy_publication_identity(const VerifiedOccupancyPublication& verification);

} // namespace rbfsafe::internal
