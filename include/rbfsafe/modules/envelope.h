#pragma once

#include <rbfsafe/modules/core.h>

// Standalone workspace-envelope values and predicates.

#include <array>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace rbfsafe {

using WorkspacePoint = std::array<double, 3>;

enum class WorkspaceEnvelopeType : std::uint8_t {
    Aabb = 0,
    Obb = 1,
    Kdop = 2,
    SupportHull = 3,
};

// A three-dimensional oriented bounding box. Basis row i is local axis i in
// workspace coordinates.
class WorkspaceObb {
  public:
    WorkspaceObb() = default;

    static Result<WorkspaceObb> create(WorkspacePoint center, std::array<double, 9> basis,
                                       WorkspacePoint half_widths);

    const WorkspacePoint& center() const noexcept { return center_; }
    const std::array<double, 9>& basis() const noexcept { return basis_; }
    const WorkspacePoint& half_widths() const noexcept { return half_widths_; }
    bool valid() const noexcept;
    WorkspaceAabb enclosing_aabb() const noexcept;
    WorkspacePoint support_point(const WorkspacePoint& direction) const noexcept;

    friend bool operator==(const WorkspaceObb&, const WorkspaceObb&) = default;

  private:
    WorkspacePoint center_{};
    std::array<double, 9> basis_{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    WorkspacePoint half_widths_{};
};

// A k-DOP is the intersection of paired half-spaces along normalized
// directions. k equals 2 * direction_count().
class WorkspaceKdop {
  public:
    WorkspaceKdop() = default;

    static Result<WorkspaceKdop> create(std::vector<WorkspacePoint> directions,
                                        std::vector<Interval> projections);
    static Result<WorkspaceKdop> from_points(std::vector<WorkspacePoint> points,
                                             std::vector<WorkspacePoint> directions, double padding = 0.0);
    static Result<std::vector<WorkspacePoint>> standard_directions(std::size_t k);
    static Result<WorkspaceKdop> from_points(std::vector<WorkspacePoint> points, std::size_t k,
                                             double padding = 0.0);

    const std::vector<WorkspacePoint>& directions() const noexcept { return directions_; }
    const std::vector<Interval>& projections() const noexcept { return projections_; }
    const std::vector<WorkspacePoint>& vertices() const noexcept { return vertices_; }
    std::size_t direction_count() const noexcept { return directions_.size(); }
    std::size_t k() const noexcept { return 2u * directions_.size(); }
    bool valid() const noexcept;
    WorkspaceAabb enclosing_aabb() const noexcept;
    WorkspacePoint support_point(const WorkspacePoint& direction) const noexcept;

    friend bool operator==(const WorkspaceKdop&, const WorkspaceKdop&) = default;

  private:
    std::vector<WorkspacePoint> directions_;
    std::vector<Interval> projections_;
    std::vector<WorkspacePoint> vertices_;
};

// Convex hull of support points, optionally Minkowski-expanded by a sphere.
// Two points plus a positive radius represent a capsule without meshing it.
class WorkspaceSupportHull {
  public:
    WorkspaceSupportHull() = default;

    static Result<WorkspaceSupportHull> create(std::vector<WorkspacePoint> points, double radius = 0.0);

    const std::vector<WorkspacePoint>& points() const noexcept { return points_; }
    double radius() const noexcept { return radius_; }
    bool valid() const noexcept;
    WorkspaceAabb enclosing_aabb() const noexcept;
    WorkspacePoint support_point(const WorkspacePoint& direction) const noexcept;

    friend bool operator==(const WorkspaceSupportHull&, const WorkspaceSupportHull&) = default;

  private:
    std::vector<WorkspacePoint> points_;
    double radius_ = 0.0;
};

using WorkspaceEnvelopeValue = std::variant<WorkspaceAabb, WorkspaceObb, WorkspaceKdop, WorkspaceSupportHull>;

class WorkspaceEnvelope {
  public:
    WorkspaceEnvelope() = default;
    WorkspaceEnvelope(WorkspaceAabb value) : value_(std::move(value)) {}
    WorkspaceEnvelope(WorkspaceObb value) : value_(std::move(value)) {}
    WorkspaceEnvelope(WorkspaceKdop value) : value_(std::move(value)) {}
    WorkspaceEnvelope(WorkspaceSupportHull value) : value_(std::move(value)) {}

    WorkspaceEnvelopeType type() const noexcept;
    const WorkspaceEnvelopeValue& value() const noexcept { return value_; }
    bool valid() const noexcept;
    WorkspaceAabb enclosing_aabb() const noexcept;
    WorkspacePoint support_point(const WorkspacePoint& direction) const noexcept;

    // Returns true when intersection cannot be ruled out. False is always
    // backed by a verified separating direction and is safe for certification.
    bool overlaps(const WorkspaceEnvelope& other, double tolerance = 0.0) const noexcept;
    double distance_lower_bound(const WorkspaceEnvelope& other) const noexcept;

    const WorkspaceAabb* aabb() const noexcept { return std::get_if<WorkspaceAabb>(&value_); }
    const WorkspaceObb* obb() const noexcept { return std::get_if<WorkspaceObb>(&value_); }
    const WorkspaceKdop* kdop() const noexcept { return std::get_if<WorkspaceKdop>(&value_); }
    const WorkspaceSupportHull* support_hull() const noexcept {
        return std::get_if<WorkspaceSupportHull>(&value_);
    }

    friend bool operator==(const WorkspaceEnvelope&, const WorkspaceEnvelope&) = default;

  private:
    WorkspaceEnvelopeValue value_{WorkspaceAabb{}};
};

const char* workspace_envelope_type_name(WorkspaceEnvelopeType type) noexcept;

} // namespace rbfsafe
