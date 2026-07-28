#pragma once

#include <rbfsafe/occupancy.h>

#include "json.h"

#include <string>

namespace rbfsafe::internal {

Json interval_json(const Interval& interval);
Json cspace_aabb_json(const CspaceAabb& domain);
Json workspace_aabb_json(const WorkspaceAabb& box);
Json configuration_json(std::span<const double> configuration);
Json timed_configuration_json(const TimedConfiguration& waypoint);
Json swept_slice_json(const SweptLinkOccupancySlice& slice, bool include_id);
Json continuous_occupancy_conflict_json(const ContinuousOccupancyConflict& conflict);
Json robot_trajectory_occupancy_json(const RobotTrajectoryOccupancy& occupancy, bool include_id);
Json continuous_fleet_occupancy_report_json(const ContinuousFleetOccupancyReport& report, bool include_id);
Json continuous_fleet_occupancy_bundle_payload_json(const ContinuousFleetOccupancyBundle& bundle,
                                                    bool include_id);

std::string swept_link_occupancy_slice_identity(const RobotTrajectoryOccupancy& occupancy,
                                                const SweptLinkOccupancySlice& slice);
std::string robot_trajectory_occupancy_identity(const RobotTrajectoryOccupancy& occupancy);
std::string continuous_fleet_occupancy_report_identity(const ContinuousFleetOccupancyReport& report);
std::string continuous_fleet_occupancy_bundle_identity(const ContinuousFleetOccupancyBundle& bundle);

} // namespace rbfsafe::internal
