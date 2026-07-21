#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <cstddef>
#include <span>
#include <vector>

namespace rbfsafe {

enum class TrajectoryAuditStatus {
    Certified,
    Partial,
    Invalid,
};

struct TrajectoryInterval {
    std::size_t segment_index = 0;
    double start_fraction = 0.0;
    double end_fraction = 0.0;
    bool start_included = true;
    bool end_included = true;
};

struct TrajectoryAuditOptions {
    std::size_t maximum_region_tests = 10'000'000;
    CancellationToken cancellation;
};

struct TrajectoryAuditReport {
    TrajectoryAuditStatus status = TrajectoryAuditStatus::Invalid;
    double coverage_ratio = 0.0;
    std::size_t waypoint_count = 0;
    std::size_t segment_count = 0;
    std::size_t region_tests = 0;
    std::vector<RegionId> region_sequence;
    std::vector<TrajectoryInterval> uncovered_intervals;
};

class TrajectoryAuditor {
  public:
    Result<TrajectoryAuditReport> audit(const SafeAtlas& atlas, std::span<const Configuration> trajectory,
                                        const TrajectoryAuditOptions& options = {}) const;
};

} // namespace rbfsafe
