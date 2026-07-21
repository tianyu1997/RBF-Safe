#include <rbfsafe/trajectory.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe {
namespace {

struct RegionInterval {
    double start = 0.0;
    double end = 0.0;
    RegionId region_id = 0;
};

struct CoveredInterval {
    double start = 0.0;
    double end = 0.0;
};

std::optional<CoveredInterval> intersect_segment(const Configuration& first, const Configuration& second,
                                                 const CspaceAabb& bounds) {
    double entry = 0.0;
    double exit = 1.0;
    for (std::size_t axis = 0; axis < bounds.dimension(); ++axis) {
        const double delta = second[axis] - first[axis];
        const Interval& slab = bounds.axes()[axis];
        if (delta == 0.0) {
            if (!slab.contains(first[axis]))
                return std::nullopt;
            continue;
        }
        double lower = (slab.lower - first[axis]) / delta;
        double upper = (slab.upper - first[axis]) / delta;
        if (lower > upper)
            std::swap(lower, upper);
        entry = std::max(entry, lower);
        exit = std::min(exit, upper);
        if (entry > exit)
            return std::nullopt;
    }
    if (exit < 0.0 || entry > 1.0)
        return std::nullopt;
    return CoveredInterval{std::clamp(entry, 0.0, 1.0), std::clamp(exit, 0.0, 1.0)};
}

std::vector<CoveredInterval> merge_coverage(const std::vector<RegionInterval>& intersections) {
    std::vector<CoveredInterval> covered;
    covered.reserve(intersections.size());
    for (const RegionInterval& intersection : intersections) {
        if (covered.empty() || intersection.start > covered.back().end) {
            covered.push_back({intersection.start, intersection.end});
        } else {
            covered.back().end = std::max(covered.back().end, intersection.end);
        }
    }
    return covered;
}

void append_region_sequence(const std::vector<RegionInterval>& intersections,
                            std::vector<RegionId>& sequence) {
    for (const RegionInterval& intersection : intersections) {
        if (sequence.empty() || sequence.back() != intersection.region_id)
            sequence.push_back(intersection.region_id);
    }
}

} // namespace

Result<TrajectoryAuditReport> TrajectoryAuditor::audit(const SafeAtlas& atlas,
                                                       std::span<const Configuration> trajectory,
                                                       const TrajectoryAuditOptions& options) const {
    if (trajectory.size() < 2) {
        return Result<TrajectoryAuditReport>::failure(
            StatusCode::InvalidArgument, "trajectory must contain at least two waypoints", "trajectory");
    }
    if (options.maximum_region_tests == 0) {
        return Result<TrajectoryAuditReport>::failure(
            StatusCode::InvalidArgument, "maximum_region_tests must be positive", "trajectory options");
    }
    for (const SafeRegion& region : atlas.regions()) {
        if (region.certificate_index >= atlas.certificates().size()) {
            return Result<TrajectoryAuditReport>::failure(
                StatusCode::InternalError, "Atlas region has no matching certificate", "trajectory Atlas");
        }
        const Certificate& certificate = atlas.certificates()[region.certificate_index];
        if (certificate.level != EvidenceLevel::CertifiedRegion ||
            certificate.robot_digest != atlas.robot_digest() ||
            certificate.scene_digest != atlas.scene_digest()) {
            return Result<TrajectoryAuditReport>::failure(StatusCode::InternalError,
                                                          "Atlas region certificate is not audit-compatible",
                                                          "trajectory Atlas");
        }
    }
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
        auto valid = validate_configuration(trajectory[index], atlas.dimension(),
                                            "trajectory waypoint " + std::to_string(index));
        if (!valid)
            return Result<TrajectoryAuditReport>(valid.error());
    }
    for (std::size_t segment = 0; segment + 1 < trajectory.size(); ++segment) {
        for (std::size_t axis = 0; axis < atlas.dimension(); ++axis) {
            if (!std::isfinite(trajectory[segment + 1][axis] - trajectory[segment][axis])) {
                return Result<TrajectoryAuditReport>::failure(
                    StatusCode::InvalidArgument, "trajectory segment delta is not finite",
                    "trajectory segment " + std::to_string(segment));
            }
        }
    }

    TrajectoryAuditReport report;
    report.waypoint_count = trajectory.size();
    report.segment_count = trajectory.size() - 1;
    double covered_measure = 0.0;
    bool has_any_coverage = false;

    for (std::size_t segment = 0; segment < report.segment_count; ++segment) {
        if (options.cancellation.cancelled()) {
            return Result<TrajectoryAuditReport>::failure(StatusCode::Cancelled,
                                                          "trajectory audit was cancelled", "trajectory");
        }
        std::vector<RegionInterval> intersections;
        intersections.reserve(atlas.regions().size());
        for (const SafeRegion& region : atlas.regions()) {
            if (report.region_tests == options.maximum_region_tests) {
                return Result<TrajectoryAuditReport>::failure(
                    StatusCode::ResourceLimit, "trajectory audit region-test budget exhausted", "trajectory");
            }
            ++report.region_tests;
            if ((report.region_tests & 1023U) == 0U && options.cancellation.cancelled()) {
                return Result<TrajectoryAuditReport>::failure(StatusCode::Cancelled,
                                                              "trajectory audit was cancelled", "trajectory");
            }
            const auto overlap =
                intersect_segment(trajectory[segment], trajectory[segment + 1], region.bounds);
            if (overlap)
                intersections.push_back({overlap->start, overlap->end, region.id});
        }
        std::sort(intersections.begin(), intersections.end(),
                  [](const RegionInterval& left, const RegionInterval& right) {
                      if (left.start != right.start)
                          return left.start < right.start;
                      if (left.end != right.end)
                          return left.end > right.end;
                      return left.region_id < right.region_id;
                  });

        has_any_coverage = has_any_coverage || !intersections.empty();
        append_region_sequence(intersections, report.region_sequence);
        const std::vector<CoveredInterval> covered = merge_coverage(intersections);
        double cursor = 0.0;
        bool cursor_is_covered = false;
        for (const CoveredInterval& interval : covered) {
            if (interval.start > cursor)
                report.uncovered_intervals.push_back(
                    {segment, cursor, interval.start, !cursor_is_covered, false});
            covered_measure += std::max(0.0, interval.end - interval.start);
            cursor = std::max(cursor, interval.end);
            cursor_is_covered = true;
        }
        if (cursor < 1.0)
            report.uncovered_intervals.push_back({segment, cursor, 1.0, !cursor_is_covered, true});
    }

    report.coverage_ratio = std::clamp(covered_measure / static_cast<double>(report.segment_count), 0.0, 1.0);
    if (report.uncovered_intervals.empty())
        report.status = TrajectoryAuditStatus::Certified;
    else if (has_any_coverage)
        report.status = TrajectoryAuditStatus::Partial;
    else
        report.status = TrajectoryAuditStatus::Invalid;
    return Result<TrajectoryAuditReport>::success(std::move(report));
}

} // namespace rbfsafe
