#include <rbfsafe/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace rbfsafe {
namespace {

WorkspaceAabb link_box(const WorkspaceAabb& proximal, const WorkspaceAabb& distal, double radius) {
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.lower[axis] = std::min(proximal.lower[axis], distal.lower[axis]) - radius;
        result.upper[axis] = std::max(proximal.upper[axis], distal.upper[axis]) + radius;
    }
    return result;
}

using EndpointCorners = std::array<WorkspacePoint, 16>;

void write_box_corners(const WorkspaceAabb& box, EndpointCorners& result, std::size_t offset) {
    for (std::size_t mask = 0; mask < 8; ++mask) {
        result[offset + mask] = {(mask & 1u) != 0 ? box.upper[0] : box.lower[0],
                                 (mask & 2u) != 0 ? box.upper[1] : box.lower[1],
                                 (mask & 4u) != 0 ? box.upper[2] : box.lower[2]};
    }
}

EndpointCorners endpoint_corners(const WorkspaceAabb& proximal, const WorkspaceAabb& distal) {
    EndpointCorners result;
    write_box_corners(proximal, result, 0);
    write_box_corners(distal, result, 8);
    return result;
}

double point_dot(const WorkspacePoint& left, const WorkspacePoint& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

WorkspacePoint point_cross(const WorkspacePoint& left, const WorkspacePoint& right) {
    return {left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

WorkspacePoint point_normalized(const WorkspacePoint& value) {
    const double magnitude = std::sqrt(point_dot(value, value));
    if (!(magnitude > 1e-15))
        return {};
    return {value[0] / magnitude, value[1] / magnitude, value[2] / magnitude};
}

Result<WorkspaceObb> fitted_link_obb(const EndpointCorners& points, const WorkspaceAabb& proximal,
                                     const WorkspaceAabb& distal, double radius) {
    WorkspacePoint first_axis{
        0.5 * (distal.lower[0] + distal.upper[0] - proximal.lower[0] - proximal.upper[0]),
        0.5 * (distal.lower[1] + distal.upper[1] - proximal.lower[1] - proximal.upper[1]),
        0.5 * (distal.lower[2] + distal.upper[2] - proximal.lower[2] - proximal.upper[2])};
    first_axis = point_normalized(first_axis);
    if (point_dot(first_axis, first_axis) == 0.0)
        first_axis = {1.0, 0.0, 0.0};

    std::size_t reference_index = 0;
    for (std::size_t axis = 1; axis < 3; ++axis) {
        if (std::abs(first_axis[axis]) < std::abs(first_axis[reference_index]))
            reference_index = axis;
    }
    WorkspacePoint reference{};
    reference[reference_index] = 1.0;
    const WorkspacePoint second_axis = point_normalized(point_cross(reference, first_axis));
    const WorkspacePoint third_axis = point_cross(first_axis, second_axis);
    const std::array<WorkspacePoint, 3> axes{first_axis, second_axis, third_axis};

    WorkspacePoint lower;
    WorkspacePoint upper;
    lower.fill(std::numeric_limits<double>::infinity());
    upper.fill(-std::numeric_limits<double>::infinity());
    for (const auto& point : points) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double projection = point_dot(axes[axis], point);
            lower[axis] = std::min(lower[axis], projection);
            upper[axis] = std::max(upper[axis], projection);
        }
    }

    WorkspacePoint center{};
    WorkspacePoint half_widths{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double middle = 0.5 * (lower[axis] + upper[axis]);
        for (std::size_t coordinate = 0; coordinate < 3; ++coordinate)
            center[coordinate] += middle * axes[axis][coordinate];
        const double scale = std::max({1.0, std::abs(lower[axis]), std::abs(upper[axis]), radius});
        const double rounding = 256.0 * std::numeric_limits<double>::epsilon() * scale;
        half_widths[axis] = 0.5 * (upper[axis] - lower[axis]) + radius + rounding;
    }
    return WorkspaceObb::create(center,
                                {first_axis[0], first_axis[1], first_axis[2], second_axis[0], second_axis[1],
                                 second_axis[2], third_axis[0], third_axis[1], third_axis[2]},
                                half_widths);
}

Result<WorkspaceEnvelope> typed_link_envelope(const WorkspaceAabb& proximal, const WorkspaceAabb& distal,
                                              double radius, const EnvelopeOptions& options) {
    if (options.workspace_envelope_type == WorkspaceEnvelopeType::Aabb)
        return WorkspaceEnvelope(link_box(proximal, distal, radius));

    const auto corners = endpoint_corners(proximal, distal);
    switch (options.workspace_envelope_type) {
    case WorkspaceEnvelopeType::Obb: {
        auto box = fitted_link_obb(corners, proximal, distal, radius);
        if (!box)
            return box.error();
        return WorkspaceEnvelope(std::move(box).value());
    }
    case WorkspaceEnvelopeType::Kdop: {
        std::vector<WorkspacePoint> points(corners.begin(), corners.end());
        auto kdop = WorkspaceKdop::from_points(std::move(points), options.kdop_k, radius);
        if (!kdop)
            return kdop.error();
        return WorkspaceEnvelope(std::move(kdop).value());
    }
    case WorkspaceEnvelopeType::SupportHull: {
        std::vector<WorkspacePoint> points(corners.begin(), corners.end());
        auto hull = WorkspaceSupportHull::create(std::move(points), radius);
        if (!hull)
            return hull.error();
        return WorkspaceEnvelope(std::move(hull).value());
    }
    case WorkspaceEnvelopeType::Aabb:
        break;
    }
    return Result<WorkspaceEnvelope>::failure(StatusCode::InvalidArgument, "unknown workspace envelope type");
}

Result<LinkEnvelope> compute_link_envelope(const SerialRobotModel& robot, const CspaceAabb& domain,
                                           const EnvelopeOptions& options) {
    if (!std::isfinite(options.obstacle_padding) || options.obstacle_padding < 0.0) {
        return Result<LinkEnvelope>::failure(StatusCode::InvalidArgument,
                                             "obstacle padding must be finite and non-negative");
    }
    auto endpoints = compute_endpoint_aabbs(robot, domain, options);
    if (!endpoints)
        return endpoints.error();

    LinkEnvelope result;
    result.links.reserve(robot.link_count());
    for (std::size_t link = 0; link < robot.link_count(); ++link) {
        result.links.push_back(link_box(endpoints.value().endpoints[link * 2],
                                        endpoints.value().endpoints[link * 2 + 1],
                                        robot.link_radii()[link] + options.obstacle_padding));
    }
    return result;
}

} // namespace

Result<WorkspaceLinkEnvelope> compute_workspace_link_envelope(const SerialRobotModel& robot,
                                                              const CspaceAabb& domain,
                                                              const EnvelopeOptions& options) {
    if (!std::isfinite(options.obstacle_padding) || options.obstacle_padding < 0.0) {
        return Result<WorkspaceLinkEnvelope>::failure(StatusCode::InvalidArgument,
                                                      "obstacle padding must be finite and non-negative");
    }
    auto endpoints = compute_endpoint_aabbs(robot, domain, options);
    if (!endpoints)
        return endpoints.error();

    WorkspaceLinkEnvelope result;
    result.endpoint_aabb_source = endpoints.value().source;
    result.endpoint_bounds_certified = endpoints.value().certified;
    result.evaluated_configurations = endpoints.value().evaluated_configurations;
    result.links.reserve(robot.link_count());
    for (std::size_t link = 0; link < robot.link_count(); ++link) {
        auto envelope = typed_link_envelope(endpoints.value().endpoints[link * 2],
                                            endpoints.value().endpoints[link * 2 + 1],
                                            robot.link_radii()[link] + options.obstacle_padding, options);
        if (!envelope)
            return envelope.error();
        result.links.push_back(std::move(envelope).value());
    }
    return result;
}

Result<LinkEnvelope> compute_ifk_aa_link_envelope(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options) {
    auto ifk_options = options;
    ifk_options.endpoint_aabb_source = EndpointAabbSource::IfkAa;
    return compute_link_envelope(robot, domain, ifk_options);
}

Result<WorkspaceLinkEnvelope> compute_ifk_aa_workspace_link_envelope(const SerialRobotModel& robot,
                                                                     const CspaceAabb& domain,
                                                                     const EnvelopeOptions& options) {
    auto ifk_options = options;
    ifk_options.endpoint_aabb_source = EndpointAabbSource::IfkAa;
    return compute_workspace_link_envelope(robot, domain, ifk_options);
}

Result<bool> configuration_is_collision_free(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                             std::span<const double> configuration, double obstacle_padding) {
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    auto points = robot.forward_kinematics(configuration);
    if (!points)
        return points.error();
    if (!std::isfinite(obstacle_padding) || obstacle_padding < 0.0) {
        return Result<bool>::failure(StatusCode::InvalidArgument,
                                     "obstacle padding must be finite and non-negative");
    }
    for (std::size_t link = 0; link < robot.link_count(); ++link) {
        const double radius = robot.link_radii()[link] + obstacle_padding;
        auto link_bounds =
            WorkspaceSupportHull::create({points.value()[link], points.value()[link + 1]}, radius);
        if (!link_bounds)
            return link_bounds.error();
        const WorkspaceEnvelope link_envelope(std::move(link_bounds).value());
        for (const auto& obstacle : scene.obstacles()) {
            if (link_envelope.overlaps(obstacle.bounds))
                return false;
        }
    }
    return true;
}

Result<RegionValidation> IfkAaLinkAabbValidator::validate(const SerialRobotModel& robot,
                                                          const SceneSnapshot& scene,
                                                          const CspaceAabb& domain) const {
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    auto envelope = compute_ifk_aa_link_envelope(robot, domain, options_);
    if (!envelope)
        return envelope.error();

    RegionValidation result;
    result.disposition = ValidationDisposition::CertifiedFree;
    result.envelope = std::move(envelope).value();
    double minimum_clearance = std::numeric_limits<double>::infinity();
    for (const auto& link : result.envelope.links) {
        const WorkspaceEnvelope link_envelope(link);
        for (const auto& obstacle : scene.obstacles()) {
            if (link_envelope.overlaps(obstacle.bounds)) {
                result.disposition = ValidationDisposition::Undetermined;
                result.clearance_lower_bound = 0.0;
                return result;
            }
            minimum_clearance =
                std::min(minimum_clearance, link_envelope.distance_lower_bound(obstacle.bounds));
        }
    }
    result.clearance_lower_bound = std::isfinite(minimum_clearance) ? minimum_clearance : 0.0;
    return result;
}

Result<RegionValidation> IfkAaWorkspaceEnvelopeValidator::validate(const SerialRobotModel& robot,
                                                                   const SceneSnapshot& scene,
                                                                   const CspaceAabb& domain) const {
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    auto envelope = compute_ifk_aa_workspace_link_envelope(robot, domain, options_);
    if (!envelope)
        return envelope.error();

    RegionValidation result;
    result.disposition = ValidationDisposition::CertifiedFree;
    result.envelope.links.reserve(envelope.value().links.size());
    double minimum_clearance = std::numeric_limits<double>::infinity();
    for (const auto& link : envelope.value().links) {
        result.envelope.links.push_back(link.enclosing_aabb());
        for (const auto& obstacle : scene.obstacles()) {
            if (link.overlaps(obstacle.bounds)) {
                result.disposition = ValidationDisposition::Undetermined;
                result.clearance_lower_bound = 0.0;
                return result;
            }
            minimum_clearance = std::min(minimum_clearance, link.distance_lower_bound(obstacle.bounds));
        }
    }
    result.clearance_lower_bound = std::isfinite(minimum_clearance) ? minimum_clearance : 0.0;
    return result;
}

std::string IfkAaWorkspaceEnvelopeValidator::algorithm_name() const {
    switch (options_.workspace_envelope_type) {
    case WorkspaceEnvelopeType::Aabb:
        return "ifk-aa-link-iaabb";
    case WorkspaceEnvelopeType::Obb:
        return "ifk-aa-link-iobb";
    case WorkspaceEnvelopeType::Kdop:
        return "ifk-aa-link-i" + std::to_string(options_.kdop_k) + "dop";
    case WorkspaceEnvelopeType::SupportHull:
        return "ifk-aa-link-support-hull";
    }
    return "ifk-aa-link-unknown";
}

} // namespace rbfsafe
