#pragma once

#include <rbfsafe/higher_order.h>
#include <rbfsafe/region_database.h>

#include <span>
#include <string>

namespace rbfsafe::internal {

std::uint64_t digest_prefix_id(const std::string& digest);
std::string obb_subject_digest(const CspaceObb& region);
std::string portal_subject_digest(RegionId left, RegionId right, const CspacePortal& portal);
std::string trajectory_tube_subject_digest(const TrajectoryTubeGeometry& tube);
std::string zonotope_subject_digest(const CspaceZonotope& region);
std::string taylor_region_subject_digest(const CspaceTaylorRegion& region);
Result<std::string> primary_region_subject_digest(const RegionGeometry& geometry);
std::string region_record_identity(const std::string& robot_digest, const std::string& scene_digest,
                                   RegionType type, const std::string& subject_digest,
                                   std::string_view source = {});

} // namespace rbfsafe::internal
