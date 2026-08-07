#include <rbfsafe/modules/envelope.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr double kGeometryTolerance = 1e-10;
constexpr std::size_t kMaximumKdopDirections = 64;
constexpr std::size_t kMaximumSupportPoints = 100'000;

WorkspacePoint add(const WorkspacePoint& left, const WorkspacePoint& right) {
    return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
}

WorkspacePoint subtract(const WorkspacePoint& left, const WorkspacePoint& right) {
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

WorkspacePoint scale(const WorkspacePoint& value, double factor) {
    return {factor * value[0], factor * value[1], factor * value[2]};
}

double dot(const WorkspacePoint& left, const WorkspacePoint& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

WorkspacePoint cross(const WorkspacePoint& left, const WorkspacePoint& right) {
    return {left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

double squared_norm(const WorkspacePoint& value) { return dot(value, value); }

double norm(const WorkspacePoint& value) { return std::sqrt(squared_norm(value)); }

bool finite_point(const WorkspacePoint& value) {
    return std::all_of(value.begin(), value.end(),
                       [](double coordinate) { return std::isfinite(coordinate); });
}

WorkspacePoint normalized(const WorkspacePoint& value) {
    const double magnitude = norm(value);
    return magnitude > 0.0 ? scale(value, 1.0 / magnitude) : WorkspacePoint{};
}

double determinant(const WorkspacePoint& first, const WorkspacePoint& second, const WorkspacePoint& third) {
    return dot(first, cross(second, third));
}

bool solve_three_planes(const WorkspacePoint& first_normal, double first_offset,
                        const WorkspacePoint& second_normal, double second_offset,
                        const WorkspacePoint& third_normal, double third_offset, WorkspacePoint& result) {
    const double divisor = determinant(first_normal, second_normal, third_normal);
    if (std::abs(divisor) <= kGeometryTolerance)
        return false;
    result = scale(add(add(scale(cross(second_normal, third_normal), first_offset),
                           scale(cross(third_normal, first_normal), second_offset)),
                       scale(cross(first_normal, second_normal), third_offset)),
                   1.0 / divisor);
    return finite_point(result);
}

bool contains_kdop_point(const std::vector<WorkspacePoint>& directions,
                         const std::vector<Interval>& projections, const WorkspacePoint& point) {
    for (std::size_t index = 0; index < directions.size(); ++index) {
        const double projection = dot(directions[index], point);
        const double magnitude =
            std::max({1.0, std::abs(projections[index].lower), std::abs(projections[index].upper)});
        const double tolerance = 256.0 * std::numeric_limits<double>::epsilon() * magnitude;
        if (projection < projections[index].lower - tolerance ||
            projection > projections[index].upper + tolerance)
            return false;
    }
    return true;
}

std::vector<WorkspacePoint> kdop_vertices(const std::vector<WorkspacePoint>& directions,
                                          const std::vector<Interval>& projections) {
    std::vector<WorkspacePoint> normals;
    std::vector<double> offsets;
    normals.reserve(2u * directions.size());
    offsets.reserve(2u * directions.size());
    for (std::size_t index = 0; index < directions.size(); ++index) {
        normals.push_back(directions[index]);
        offsets.push_back(projections[index].upper);
        normals.push_back(scale(directions[index], -1.0));
        offsets.push_back(-projections[index].lower);
    }

    std::vector<WorkspacePoint> result;
    for (std::size_t first = 0; first < normals.size(); ++first) {
        for (std::size_t second = first + 1; second < normals.size(); ++second) {
            for (std::size_t third = second + 1; third < normals.size(); ++third) {
                WorkspacePoint candidate;
                if (!solve_three_planes(normals[first], offsets[first], normals[second], offsets[second],
                                        normals[third], offsets[third], candidate) ||
                    !contains_kdop_point(directions, projections, candidate))
                    continue;
                const bool duplicate = std::any_of(result.begin(), result.end(), [&](const auto& point) {
                    return squared_norm(subtract(point, candidate)) <= 1e-18;
                });
                if (!duplicate)
                    result.push_back(candidate);
            }
        }
    }
    return result;
}

WorkspacePoint envelope_center(const WorkspaceEnvelope& envelope) {
    if (const auto* box = envelope.aabb()) {
        return {0.5 * (box->lower[0] + box->upper[0]), 0.5 * (box->lower[1] + box->upper[1]),
                0.5 * (box->lower[2] + box->upper[2])};
    }
    if (const auto* box = envelope.obb())
        return box->center();
    if (const auto* hull = envelope.support_hull()) {
        WorkspacePoint center{};
        for (const auto& point : hull->points())
            center = add(center, point);
        return scale(center, 1.0 / static_cast<double>(hull->points().size()));
    }
    const auto box = envelope.enclosing_aabb();
    return {0.5 * (box.lower[0] + box.upper[0]), 0.5 * (box.lower[1] + box.upper[1]),
            0.5 * (box.lower[2] + box.upper[2])};
}

WorkspacePoint minkowski_support(const WorkspaceEnvelope& first, const WorkspaceEnvelope& second,
                                 const WorkspacePoint& direction) {
    return subtract(first.support_point(direction), second.support_point(scale(direction, -1.0)));
}

double projection_gap(const WorkspaceEnvelope& first, const WorkspaceEnvelope& second,
                      const WorkspacePoint& direction) {
    const double magnitude = norm(direction);
    if (!(magnitude > kGeometryTolerance) || !std::isfinite(magnitude))
        return 0.0;
    const WorkspacePoint unit = scale(direction, 1.0 / magnitude);
    const double first_max = dot(first.support_point(unit), unit);
    const double first_min = dot(first.support_point(scale(unit, -1.0)), unit);
    const double second_max = dot(second.support_point(unit), unit);
    const double second_min = dot(second.support_point(scale(unit, -1.0)), unit);
    return std::max({0.0, second_min - first_max, first_min - second_max});
}

bool same_direction(const WorkspacePoint& direction, const WorkspacePoint& toward) {
    return dot(direction, toward) > 0.0;
}

bool update_line(std::vector<WorkspacePoint>& simplex, WorkspacePoint& direction) {
    const WorkspacePoint a = simplex.back();
    const WorkspacePoint b = simplex[simplex.size() - 2];
    const WorkspacePoint ab = subtract(b, a);
    const WorkspacePoint ao = scale(a, -1.0);
    if (same_direction(ab, ao)) {
        direction = cross(cross(ab, ao), ab);
        if (squared_norm(direction) <= kGeometryTolerance * kGeometryTolerance)
            direction = cross(ab, {1.0, 0.0, 0.0});
        if (squared_norm(direction) <= kGeometryTolerance * kGeometryTolerance)
            direction = cross(ab, {0.0, 1.0, 0.0});
    } else {
        simplex = {a};
        direction = ao;
    }
    return false;
}

bool update_triangle(std::vector<WorkspacePoint>& simplex, WorkspacePoint& direction) {
    const WorkspacePoint a = simplex[2];
    const WorkspacePoint b = simplex[1];
    const WorkspacePoint c = simplex[0];
    const WorkspacePoint ab = subtract(b, a);
    const WorkspacePoint ac = subtract(c, a);
    const WorkspacePoint ao = scale(a, -1.0);
    const WorkspacePoint abc = cross(ab, ac);

    if (same_direction(cross(abc, ac), ao)) {
        if (same_direction(ac, ao)) {
            simplex = {c, a};
            direction = cross(cross(ac, ao), ac);
        } else {
            simplex = {b, a};
            return update_line(simplex, direction);
        }
    } else if (same_direction(cross(ab, abc), ao)) {
        simplex = {b, a};
        return update_line(simplex, direction);
    } else if (same_direction(abc, ao)) {
        direction = abc;
    } else {
        simplex = {b, c, a};
        direction = scale(abc, -1.0);
    }
    return false;
}

bool update_tetrahedron(std::vector<WorkspacePoint>& simplex, WorkspacePoint& direction) {
    const WorkspacePoint a = simplex[3];
    const WorkspacePoint b = simplex[2];
    const WorkspacePoint c = simplex[1];
    const WorkspacePoint d = simplex[0];
    const WorkspacePoint ao = scale(a, -1.0);
    const WorkspacePoint ab = subtract(b, a);
    const WorkspacePoint ac = subtract(c, a);
    const WorkspacePoint ad = subtract(d, a);

    WorkspacePoint abc = cross(ab, ac);
    if (dot(abc, ad) > 0.0)
        abc = scale(abc, -1.0);
    if (same_direction(abc, ao)) {
        simplex = {c, b, a};
        return update_triangle(simplex, direction);
    }

    WorkspacePoint acd = cross(ac, ad);
    if (dot(acd, ab) > 0.0)
        acd = scale(acd, -1.0);
    if (same_direction(acd, ao)) {
        simplex = {d, c, a};
        return update_triangle(simplex, direction);
    }

    WorkspacePoint adb = cross(ad, ab);
    if (dot(adb, ac) > 0.0)
        adb = scale(adb, -1.0);
    if (same_direction(adb, ao)) {
        simplex = {b, d, a};
        return update_triangle(simplex, direction);
    }
    return true;
}

bool update_simplex(std::vector<WorkspacePoint>& simplex, WorkspacePoint& direction) {
    if (simplex.size() == 2)
        return update_line(simplex, direction);
    if (simplex.size() == 3)
        return update_triangle(simplex, direction);
    return update_tetrahedron(simplex, direction);
}

void append_shape_directions(const WorkspaceEnvelope& envelope, std::vector<WorkspacePoint>& result) {
    if (const auto* box = envelope.obb()) {
        for (std::size_t axis = 0; axis < 3; ++axis)
            result.push_back(
                {box->basis()[axis * 3], box->basis()[axis * 3 + 1], box->basis()[axis * 3 + 2]});
    } else if (const auto* kdop = envelope.kdop()) {
        result.insert(result.end(), kdop->directions().begin(), kdop->directions().end());
    }
}

double separation_lower_bound(const WorkspaceEnvelope& first, const WorkspaceEnvelope& second) {
    if (!first.valid() || !second.valid())
        return 0.0;

    std::vector<WorkspacePoint> directions{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    append_shape_directions(first, directions);
    append_shape_directions(second, directions);
    const WorkspacePoint center_direction = subtract(envelope_center(second), envelope_center(first));
    directions.push_back(center_direction);

    if (const auto* first_obb = first.obb()) {
        if (const auto* second_obb = second.obb()) {
            for (std::size_t first_axis = 0; first_axis < 3; ++first_axis) {
                const WorkspacePoint left{first_obb->basis()[first_axis * 3],
                                          first_obb->basis()[first_axis * 3 + 1],
                                          first_obb->basis()[first_axis * 3 + 2]};
                for (std::size_t second_axis = 0; second_axis < 3; ++second_axis) {
                    const WorkspacePoint right{second_obb->basis()[second_axis * 3],
                                               second_obb->basis()[second_axis * 3 + 1],
                                               second_obb->basis()[second_axis * 3 + 2]};
                    directions.push_back(cross(left, right));
                }
            }
        }
    }

    double lower_bound = 0.0;
    for (const auto& direction : directions)
        lower_bound = std::max(lower_bound, projection_gap(first, second, direction));
    if (lower_bound > 0.0)
        return lower_bound;

    WorkspacePoint direction = center_direction;
    if (squared_norm(direction) <= kGeometryTolerance * kGeometryTolerance)
        direction = {1.0, 0.0, 0.0};
    std::vector<WorkspacePoint> simplex;
    simplex.reserve(4);
    simplex.push_back(minkowski_support(first, second, direction));
    direction = scale(simplex.back(), -1.0);
    for (std::size_t iteration = 0; iteration < 64; ++iteration) {
        if (squared_norm(direction) <= kGeometryTolerance * kGeometryTolerance)
            return lower_bound;
        const WorkspacePoint support = minkowski_support(first, second, direction);
        if (dot(support, direction) < 0.0)
            return std::max(lower_bound, projection_gap(first, second, direction));
        if (std::any_of(simplex.begin(), simplex.end(),
                        [&](const auto& point) { return squared_norm(subtract(point, support)) <= 1e-24; }))
            return lower_bound;
        simplex.push_back(support);
        if (update_simplex(simplex, direction))
            return lower_bound;
    }
    return lower_bound;
}

} // namespace

Result<WorkspaceObb> WorkspaceObb::create(WorkspacePoint center, std::array<double, 9> basis,
                                          WorkspacePoint half_widths) {
    WorkspaceObb result;
    result.center_ = center;
    result.basis_ = basis;
    result.half_widths_ = half_widths;
    if (!result.valid()) {
        return Result<WorkspaceObb>::failure(
            StatusCode::InvalidArgument,
            "workspace OBB requires finite center/extents and a right-handed orthonormal basis");
    }
    return result;
}

bool WorkspaceObb::valid() const noexcept {
    if (!finite_point(center_) || !finite_point(half_widths_) ||
        !std::all_of(half_widths_.begin(), half_widths_.end(), [](double value) { return value >= 0.0; }) ||
        !std::all_of(basis_.begin(), basis_.end(), [](double value) { return std::isfinite(value); }))
        return false;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t other = 0; other < 3; ++other) {
            double product = 0.0;
            for (std::size_t column = 0; column < 3; ++column)
                product += basis_[row * 3 + column] * basis_[other * 3 + column];
            if (std::abs(product - (row == other ? 1.0 : 0.0)) > 1e-9)
                return false;
        }
    }
    const WorkspacePoint first{basis_[0], basis_[1], basis_[2]};
    const WorkspacePoint second{basis_[3], basis_[4], basis_[5]};
    const WorkspacePoint third{basis_[6], basis_[7], basis_[8]};
    return determinant(first, second, third) > 1.0 - 1e-9;
}

WorkspaceAabb WorkspaceObb::enclosing_aabb() const noexcept {
    WorkspaceAabb result;
    for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
        double radius = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis)
            radius += std::abs(basis_[axis * 3 + coordinate]) * half_widths_[axis];
        result.lower[coordinate] =
            std::nextafter(center_[coordinate] - radius, -std::numeric_limits<double>::infinity());
        result.upper[coordinate] =
            std::nextafter(center_[coordinate] + radius, std::numeric_limits<double>::infinity());
    }
    return result;
}

WorkspacePoint WorkspaceObb::support_point(const WorkspacePoint& direction) const noexcept {
    WorkspacePoint result = center_;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const WorkspacePoint local_axis{basis_[axis * 3], basis_[axis * 3 + 1], basis_[axis * 3 + 2]};
        const double extent = dot(local_axis, direction) >= 0.0 ? half_widths_[axis] : -half_widths_[axis];
        result = add(result, scale(local_axis, extent));
    }
    return result;
}

Result<WorkspaceKdop> WorkspaceKdop::create(std::vector<WorkspacePoint> directions,
                                            std::vector<Interval> projections) {
    if (directions.size() < 3 || directions.size() > kMaximumKdopDirections ||
        projections.size() != directions.size()) {
        return Result<WorkspaceKdop>::failure(
            StatusCode::InvalidArgument,
            "workspace k-DOP requires 3..64 directions and one projection interval per direction");
    }
    for (std::size_t index = 0; index < directions.size(); ++index) {
        const double magnitude = norm(directions[index]);
        if (!finite_point(directions[index]) || !std::isfinite(magnitude) ||
            magnitude <= kGeometryTolerance || !projections[index].valid()) {
            return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                                  "workspace k-DOP direction or projection is invalid");
        }
        if (std::abs(magnitude - 1.0) > 1e-12) {
            directions[index] = scale(directions[index], 1.0 / magnitude);
            projections[index].lower = std::nextafter(projections[index].lower / magnitude,
                                                      -std::numeric_limits<double>::infinity());
            projections[index].upper =
                std::nextafter(projections[index].upper / magnitude, std::numeric_limits<double>::infinity());
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::abs(dot(directions[index], directions[previous])) > 1.0 - 1e-9) {
                return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                                      "workspace k-DOP directions must be unique up to sign");
            }
        }
    }
    bool spans_workspace = false;
    for (std::size_t first = 0; first < directions.size() && !spans_workspace; ++first) {
        for (std::size_t second = first + 1; second < directions.size() && !spans_workspace; ++second) {
            for (std::size_t third = second + 1; third < directions.size(); ++third) {
                if (std::abs(determinant(directions[first], directions[second], directions[third])) > 1e-9) {
                    spans_workspace = true;
                    break;
                }
            }
        }
    }
    if (!spans_workspace) {
        return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                              "workspace k-DOP directions do not span 3D workspace");
    }
    WorkspaceKdop result;
    result.directions_ = std::move(directions);
    result.projections_ = std::move(projections);
    result.vertices_ = kdop_vertices(result.directions_, result.projections_);
    if (result.vertices_.empty()) {
        return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                              "workspace k-DOP half-spaces have no bounded intersection");
    }
    return result;
}

Result<WorkspaceKdop> WorkspaceKdop::from_points(std::vector<WorkspacePoint> points,
                                                 std::vector<WorkspacePoint> directions, double padding) {
    if (points.empty() || points.size() > kMaximumSupportPoints ||
        !std::all_of(points.begin(), points.end(), finite_point) || !std::isfinite(padding) ||
        padding < 0.0) {
        return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                              "workspace k-DOP source points or padding are invalid");
    }
    std::vector<Interval> projections;
    projections.reserve(directions.size());
    for (const auto& direction : directions) {
        const double magnitude = norm(direction);
        if (!finite_point(direction) || magnitude <= kGeometryTolerance) {
            return Result<WorkspaceKdop>::failure(StatusCode::InvalidArgument,
                                                  "workspace k-DOP direction is invalid");
        }
        double lower = std::numeric_limits<double>::infinity();
        double upper = -std::numeric_limits<double>::infinity();
        for (const auto& point : points) {
            const double projection = dot(direction, point);
            lower = std::min(lower, projection);
            upper = std::max(upper, projection);
        }
        projections.emplace_back(
            std::nextafter(lower - padding * magnitude, -std::numeric_limits<double>::infinity()),
            std::nextafter(upper + padding * magnitude, std::numeric_limits<double>::infinity()));
    }
    return create(std::move(directions), std::move(projections));
}

Result<std::vector<WorkspacePoint>> WorkspaceKdop::standard_directions(std::size_t k) {
    if (k != 6 && k != 14 && k != 18 && k != 26) {
        return Result<std::vector<WorkspacePoint>>::failure(
            StatusCode::InvalidArgument, "standard workspace k-DOP supports k = 6, 14, 18, or 26");
    }
    std::vector<WorkspacePoint> result{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    if (k == 18 || k == 26) {
        result.insert(result.end(), {{1.0, 1.0, 0.0},
                                     {1.0, -1.0, 0.0},
                                     {1.0, 0.0, 1.0},
                                     {1.0, 0.0, -1.0},
                                     {0.0, 1.0, 1.0},
                                     {0.0, 1.0, -1.0}});
    }
    if (k == 14 || k == 26) {
        result.insert(result.end(), {{1.0, 1.0, 1.0}, {1.0, 1.0, -1.0}, {1.0, -1.0, 1.0}, {-1.0, 1.0, 1.0}});
    }
    return result;
}

Result<WorkspaceKdop> WorkspaceKdop::from_points(std::vector<WorkspacePoint> points, std::size_t k,
                                                 double padding) {
    auto directions = standard_directions(k);
    if (!directions)
        return directions.error();
    return from_points(std::move(points), std::move(directions).value(), padding);
}

bool WorkspaceKdop::valid() const noexcept {
    return directions_.size() >= 3 && directions_.size() <= kMaximumKdopDirections &&
           projections_.size() == directions_.size() && !vertices_.empty() &&
           std::all_of(directions_.begin(), directions_.end(),
                       [](const auto& direction) {
                           return finite_point(direction) && std::abs(norm(direction) - 1.0) <= 1e-9;
                       }) &&
           std::all_of(projections_.begin(), projections_.end(),
                       [](const auto& projection) { return projection.valid(); }) &&
           std::all_of(vertices_.begin(), vertices_.end(), finite_point);
}

WorkspaceAabb WorkspaceKdop::enclosing_aabb() const noexcept {
    WorkspaceAabb result;
    result.lower.fill(std::numeric_limits<double>::infinity());
    result.upper.fill(-std::numeric_limits<double>::infinity());
    for (const auto& point : vertices_) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.lower[axis] = std::min(result.lower[axis], point[axis]);
            result.upper[axis] = std::max(result.upper[axis], point[axis]);
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.lower[axis] = std::nextafter(result.lower[axis], -std::numeric_limits<double>::infinity());
        result.upper[axis] = std::nextafter(result.upper[axis], std::numeric_limits<double>::infinity());
    }
    return result;
}

WorkspacePoint WorkspaceKdop::support_point(const WorkspacePoint& direction) const noexcept {
    if (vertices_.empty())
        return {};
    WorkspacePoint result = vertices_.front();
    double best = dot(result, direction);
    for (std::size_t index = 1; index < vertices_.size(); ++index) {
        const double candidate = dot(vertices_[index], direction);
        if (candidate > best) {
            result = vertices_[index];
            best = candidate;
        }
    }
    return result;
}

Result<WorkspaceSupportHull> WorkspaceSupportHull::create(std::vector<WorkspacePoint> points, double radius) {
    WorkspaceSupportHull result;
    result.points_ = std::move(points);
    result.radius_ = radius;
    if (!result.valid()) {
        return Result<WorkspaceSupportHull>::failure(
            StatusCode::InvalidArgument,
            "workspace support hull requires 1..100000 finite points and a non-negative finite radius");
    }
    std::sort(result.points_.begin(), result.points_.end());
    result.points_.erase(std::unique(result.points_.begin(), result.points_.end()), result.points_.end());
    return result;
}

bool WorkspaceSupportHull::valid() const noexcept {
    return !points_.empty() && points_.size() <= kMaximumSupportPoints && std::isfinite(radius_) &&
           radius_ >= 0.0 && std::all_of(points_.begin(), points_.end(), finite_point);
}

WorkspaceAabb WorkspaceSupportHull::enclosing_aabb() const noexcept {
    WorkspaceAabb result;
    result.lower.fill(std::numeric_limits<double>::infinity());
    result.upper.fill(-std::numeric_limits<double>::infinity());
    for (const auto& point : points_) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.lower[axis] = std::min(result.lower[axis], point[axis] - radius_);
            result.upper[axis] = std::max(result.upper[axis], point[axis] + radius_);
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.lower[axis] = std::nextafter(result.lower[axis], -std::numeric_limits<double>::infinity());
        result.upper[axis] = std::nextafter(result.upper[axis], std::numeric_limits<double>::infinity());
    }
    return result;
}

WorkspacePoint WorkspaceSupportHull::support_point(const WorkspacePoint& direction) const noexcept {
    if (points_.empty())
        return {};
    WorkspacePoint result = points_.front();
    double best = dot(result, direction);
    for (std::size_t index = 1; index < points_.size(); ++index) {
        const double candidate = dot(points_[index], direction);
        if (candidate > best) {
            result = points_[index];
            best = candidate;
        }
    }
    const double magnitude = norm(direction);
    if (magnitude > 0.0)
        result = add(result, scale(direction, radius_ / magnitude));
    return result;
}

WorkspaceEnvelopeType WorkspaceEnvelope::type() const noexcept {
    return static_cast<WorkspaceEnvelopeType>(value_.index());
}

bool WorkspaceEnvelope::valid() const noexcept {
    return std::visit([](const auto& value) { return value.valid(); }, value_);
}

WorkspaceAabb WorkspaceEnvelope::enclosing_aabb() const noexcept {
    return std::visit(
        [](const auto& value) -> WorkspaceAabb {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, WorkspaceAabb>)
                return value;
            else
                return value.enclosing_aabb();
        },
        value_);
}

WorkspacePoint WorkspaceEnvelope::support_point(const WorkspacePoint& direction) const noexcept {
    return std::visit(
        [&](const auto& value) -> WorkspacePoint {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, WorkspaceAabb>) {
                return {direction[0] >= 0.0 ? value.upper[0] : value.lower[0],
                        direction[1] >= 0.0 ? value.upper[1] : value.lower[1],
                        direction[2] >= 0.0 ? value.upper[2] : value.lower[2]};
            } else {
                return value.support_point(direction);
            }
        },
        value_);
}

bool WorkspaceEnvelope::overlaps(const WorkspaceEnvelope& other, double tolerance) const noexcept {
    if (!std::isfinite(tolerance) || tolerance < 0.0 || !valid() || !other.valid())
        return true;
    return separation_lower_bound(*this, other) <= tolerance;
}

double WorkspaceEnvelope::distance_lower_bound(const WorkspaceEnvelope& other) const noexcept {
    return separation_lower_bound(*this, other);
}

const char* workspace_envelope_type_name(WorkspaceEnvelopeType type) noexcept {
    switch (type) {
    case WorkspaceEnvelopeType::Aabb:
        return "aabb";
    case WorkspaceEnvelopeType::Obb:
        return "obb";
    case WorkspaceEnvelopeType::Kdop:
        return "kdop";
    case WorkspaceEnvelopeType::SupportHull:
        return "support_hull";
    }
    return "aabb";
}

} // namespace rbfsafe
