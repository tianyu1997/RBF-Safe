#include <rbfsafe/modules/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace rbfsafe {
namespace {

struct AffineScalar {
    double center = 0.0;
    std::vector<double> linear;
    double remainder = 0.0;

    explicit AffineScalar(std::size_t dimension = 0) : linear(dimension, 0.0) {}

    double radius() const {
        double result = std::max(0.0, remainder);
        for (const auto coefficient : linear)
            result += std::abs(coefficient);
        return result;
    }
    Interval interval() const {
        const double bound = radius();
        const double magnitude = std::max({1.0, std::abs(center - bound), std::abs(center + bound)});
        const double rounding = 1024.0 * std::numeric_limits<double>::epsilon() *
                                static_cast<double>(linear.size() + 1u) * magnitude;
        return {std::nextafter(center - bound - rounding, -std::numeric_limits<double>::infinity()),
                std::nextafter(center + bound + rounding, std::numeric_limits<double>::infinity())};
    }
};

using AffineMatrix = std::array<AffineScalar, 16>;

AffineMatrix make_matrix(std::size_t dimension) {
    AffineMatrix result;
    for (auto& value : result)
        value = AffineScalar(dimension);
    return result;
}

AffineMatrix affine_identity(std::size_t dimension) {
    auto result = make_matrix(dimension);
    result[0].center = result[5].center = result[10].center = result[15].center = 1.0;
    return result;
}

AffineMatrix affine_joint(const DhJoint& joint, const Interval& interval, std::size_t joint_index,
                          std::size_t dimension) {
    auto matrix = make_matrix(dimension);
    matrix[15].center = 1.0;
    const double cosine_alpha = std::cos(joint.alpha);
    const double sine_alpha = std::sin(joint.alpha);
    if (joint.type == JointType::Revolute) {
        const double middle = interval.center() + joint.theta;
        const double delta = 0.5 * interval.width();
        const double cosine = std::cos(middle);
        const double sine = std::sin(middle);
        const double cosine_linear = -sine * delta;
        const double sine_linear = cosine * delta;
        const double trig_remainder = 0.5 * delta * delta;

        matrix[0].center = cosine;
        matrix[0].linear[joint_index] = cosine_linear;
        matrix[0].remainder = trig_remainder;
        matrix[1].center = -sine;
        matrix[1].linear[joint_index] = -sine_linear;
        matrix[1].remainder = trig_remainder;
        matrix[3].center = joint.a;
        matrix[4].center = sine * cosine_alpha;
        matrix[4].linear[joint_index] = sine_linear * cosine_alpha;
        matrix[4].remainder = trig_remainder * std::abs(cosine_alpha);
        matrix[5].center = cosine * cosine_alpha;
        matrix[5].linear[joint_index] = cosine_linear * cosine_alpha;
        matrix[5].remainder = trig_remainder * std::abs(cosine_alpha);
        matrix[6].center = -sine_alpha;
        matrix[7].center = -joint.d * sine_alpha;
        matrix[8].center = sine * sine_alpha;
        matrix[8].linear[joint_index] = sine_linear * sine_alpha;
        matrix[8].remainder = trig_remainder * std::abs(sine_alpha);
        matrix[9].center = cosine * sine_alpha;
        matrix[9].linear[joint_index] = cosine_linear * sine_alpha;
        matrix[9].remainder = trig_remainder * std::abs(sine_alpha);
        matrix[10].center = cosine_alpha;
        matrix[11].center = joint.d * cosine_alpha;
    } else {
        const double cosine = std::cos(joint.theta);
        const double sine = std::sin(joint.theta);
        const double middle_d = joint.d + interval.center();
        const double delta = 0.5 * interval.width();
        matrix[0].center = cosine;
        matrix[1].center = -sine;
        matrix[3].center = joint.a;
        matrix[4].center = sine * cosine_alpha;
        matrix[5].center = cosine * cosine_alpha;
        matrix[6].center = -sine_alpha;
        matrix[7].center = -middle_d * sine_alpha;
        matrix[7].linear[joint_index] = -delta * sine_alpha;
        matrix[8].center = sine * sine_alpha;
        matrix[9].center = cosine * sine_alpha;
        matrix[10].center = cosine_alpha;
        matrix[11].center = middle_d * cosine_alpha;
        matrix[11].linear[joint_index] = delta * cosine_alpha;
    }
    return matrix;
}

AffineScalar multiply_scalar(const AffineScalar& left, const AffineScalar& right) {
    AffineScalar result(left.linear.size());
    result.center = left.center * right.center;
    for (std::size_t index = 0; index < result.linear.size(); ++index) {
        result.linear[index] = left.center * right.linear[index] + left.linear[index] * right.center;
    }
    double left_linear_radius = 0.0;
    double right_linear_radius = 0.0;
    for (const auto value : left.linear)
        left_linear_radius += std::abs(value);
    for (const auto value : right.linear)
        right_linear_radius += std::abs(value);
    result.remainder = left_linear_radius * right_linear_radius + std::abs(left.center) * right.remainder +
                       std::abs(right.center) * left.remainder + left_linear_radius * right.remainder +
                       right_linear_radius * left.remainder + 2.0 * left.remainder * right.remainder;
    const double scale =
        1.0 + std::abs(result.center) + result.remainder + left_linear_radius + right_linear_radius;
    result.remainder += 256.0 * std::numeric_limits<double>::epsilon() *
                        static_cast<double>(result.linear.size() + 1u) * scale;
    return result;
}

void add_to(AffineScalar& target, const AffineScalar& value) {
    target.center += value.center;
    target.remainder += value.remainder;
    for (std::size_t index = 0; index < target.linear.size(); ++index)
        target.linear[index] += value.linear[index];
    target.remainder += 64.0 * std::numeric_limits<double>::epsilon() *
                        static_cast<double>(target.linear.size() + 1u) *
                        (1.0 + std::abs(target.center) + target.remainder);
}

AffineMatrix affine_multiply(const AffineMatrix& left, const AffineMatrix& right, std::size_t dimension) {
    auto output = make_matrix(dimension);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                add_to(output[row * 4 + column],
                       multiply_scalar(left[row * 4 + inner], right[inner * 4 + column]));
            }
        }
    }
    return output;
}

Result<void> validate_domain(const SerialRobotModel& robot, const CspaceAabb& domain) {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status;
    if (!domain.valid())
        return Result<void>::failure(StatusCode::InvalidArgument, "C-space domain is invalid");
    if (domain.dimension() != robot.dimension()) {
        return Result<void>::failure(StatusCode::DimensionMismatch,
                                     "C-space domain dimension does not match robot");
    }
    for (std::size_t index = 0; index < domain.dimension(); ++index) {
        const auto& axis = domain.axes()[index];
        const auto& limit = robot.joint_limits()[index];
        if (axis.lower < limit.lower - 1e-12 || axis.upper > limit.upper + 1e-12) {
            return Result<void>::failure(StatusCode::InvalidArgument, "C-space domain exceeds joint limits",
                                         std::to_string(index));
        }
    }
    return Result<void>::success();
}

WorkspaceAabb endpoint_box(const AffineMatrix& matrix) {
    WorkspaceAabb result;
    const std::array<std::size_t, 3> translation_indices{3, 7, 11};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto interval = matrix[translation_indices[axis]].interval();
        result.lower[axis] = interval.lower;
        result.upper[axis] = interval.upper;
    }
    return result;
}

WorkspaceAabb link_box(const WorkspaceAabb& proximal, const WorkspaceAabb& distal, double radius) {
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.lower[axis] = std::min(proximal.lower[axis], distal.lower[axis]) - radius;
        result.upper[axis] = std::max(proximal.upper[axis], distal.upper[axis]) + radius;
    }
    return result;
}

constexpr double kCritSampleNarrowThreshold = 0.01;
constexpr std::size_t kCritSampleMaxCombinations = 8192;

std::vector<double> critical_candidates(const Interval& interval) {
    if (interval.width() < kCritSampleNarrowThreshold)
        return {interval.center()};

    std::vector<double> result{interval.lower, interval.upper};
    constexpr double half_pi = std::numbers::pi_v<double> * 0.5;
    const auto first = static_cast<long long>(std::ceil(interval.lower / half_pi));
    const auto last = static_cast<long long>(std::floor(interval.upper / half_pi));
    for (long long index = first; index <= last; ++index) {
        const double candidate = static_cast<double>(index) * half_pi;
        if (candidate > interval.lower + 1e-12 && candidate < interval.upper - 1e-12)
            result.push_back(candidate);
    }
    return result;
}

std::size_t combination_count(const std::vector<std::vector<double>>& candidates) {
    std::size_t result = 1;
    for (const auto& dimension : candidates) {
        if (dimension.empty())
            return 0;
        if (result > std::numeric_limits<std::size_t>::max() / dimension.size())
            return std::numeric_limits<std::size_t>::max();
        result *= dimension.size();
    }
    return result;
}

std::vector<std::vector<double>> capped_critical_candidates(const CspaceAabb& domain) {
    std::vector<std::vector<double>> result;
    result.reserve(domain.dimension());
    for (const auto& interval : domain.axes())
        result.push_back(critical_candidates(interval));

    std::size_t total = combination_count(result);
    while (total > kCritSampleMaxCombinations) {
        std::size_t widest_candidate_set = 0;
        for (std::size_t index = 1; index < result.size(); ++index) {
            if (result[index].size() > result[widest_candidate_set].size())
                widest_candidate_set = index;
        }
        if (result[widest_candidate_set].size() <= 3)
            break;
        const auto& interval = domain.axes()[widest_candidate_set];
        result[widest_candidate_set] = {interval.lower, interval.center(), interval.upper};
        total = combination_count(result);
    }
    return result;
}

void expand_sampled_endpoint(WorkspaceAabb& bounds, const WorkspacePoint& point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds.lower[axis] = std::min(bounds.lower[axis], point[axis]);
        bounds.upper[axis] = std::max(bounds.upper[axis], point[axis]);
    }
}

Result<EndpointAabbResult> compute_ifk_aa_endpoint_aabbs(const SerialRobotModel& robot,
                                                         const CspaceAabb& domain) {
    const std::size_t dimension = robot.dimension();
    std::vector<AffineMatrix> prefix;
    prefix.reserve(dimension + 2);
    prefix.push_back(affine_identity(dimension));
    for (std::size_t index = 0; index < dimension; ++index) {
        prefix.push_back(affine_multiply(
            prefix.back(), affine_joint(robot.joints()[index], domain.axes()[index], index, dimension),
            dimension));
    }
    if (robot.tool_frame()) {
        prefix.push_back(affine_multiply(
            prefix.back(), affine_joint(*robot.tool_frame(), {0.0, 0.0}, 0, dimension), dimension));
    }

    EndpointAabbResult result;
    result.source = EndpointAabbSource::IfkAa;
    result.certified = true;
    result.endpoints.reserve(robot.link_count() * 2);
    for (std::size_t link = 0; link < robot.link_count(); ++link) {
        result.endpoints.push_back(endpoint_box(prefix[link]));
        result.endpoints.push_back(endpoint_box(prefix[link + 1]));
    }
    return result;
}

Result<EndpointAabbResult> compute_critical_sample_endpoint_aabbs(const SerialRobotModel& robot,
                                                                  const CspaceAabb& domain) {
    const auto candidates = capped_critical_candidates(domain);
    const std::size_t total = combination_count(candidates);
    if (total == 0 || total == std::numeric_limits<std::size_t>::max()) {
        return Result<EndpointAabbResult>::failure(StatusCode::ResourceLimit,
                                                   "critical-sample combination count is invalid");
    }

    EndpointAabbResult result;
    result.source = EndpointAabbSource::CritSample;
    result.certified = false;
    result.endpoints.resize(robot.link_count() * 2);
    for (auto& endpoint : result.endpoints) {
        endpoint.lower.fill(std::numeric_limits<double>::infinity());
        endpoint.upper.fill(-std::numeric_limits<double>::infinity());
    }

    Configuration configuration(robot.dimension(), 0.0);
    std::vector<std::size_t> indices(robot.dimension(), 0);
    for (std::size_t combination = 0; combination < total; ++combination) {
        for (std::size_t joint = 0; joint < robot.dimension(); ++joint)
            configuration[joint] = candidates[joint][indices[joint]];
        auto points = robot.forward_kinematics(configuration);
        if (!points)
            return points.error();
        for (std::size_t link = 0; link < robot.link_count(); ++link) {
            expand_sampled_endpoint(result.endpoints[link * 2], points.value()[link]);
            expand_sampled_endpoint(result.endpoints[link * 2 + 1], points.value()[link + 1]);
        }
        ++result.evaluated_configurations;

        for (std::size_t joint = robot.dimension(); joint-- > 0;) {
            ++indices[joint];
            if (indices[joint] < candidates[joint].size())
                break;
            indices[joint] = 0;
        }
    }
    return result;
}

std::vector<WorkspacePoint> box_corners(const WorkspaceAabb& box) {
    std::vector<WorkspacePoint> result;
    result.reserve(8);
    for (std::size_t mask = 0; mask < 8; ++mask) {
        result.push_back({(mask & 1u) != 0 ? box.upper[0] : box.lower[0],
                          (mask & 2u) != 0 ? box.upper[1] : box.lower[1],
                          (mask & 4u) != 0 ? box.upper[2] : box.lower[2]});
    }
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

Result<WorkspaceObb> fitted_link_obb(const std::vector<WorkspacePoint>& points, const WorkspaceAabb& proximal,
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
    WorkspacePoint second_axis = point_normalized(point_cross(reference, first_axis));
    WorkspacePoint third_axis = point_cross(first_axis, second_axis);
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

    auto points = box_corners(proximal);
    auto distal_points = box_corners(distal);
    points.insert(points.end(), distal_points.begin(), distal_points.end());
    if (options.workspace_envelope_type == WorkspaceEnvelopeType::Obb) {
        auto box = fitted_link_obb(points, proximal, distal, radius);
        if (!box)
            return box.error();
        return WorkspaceEnvelope(std::move(box).value());
    }
    if (options.workspace_envelope_type == WorkspaceEnvelopeType::Kdop) {
        auto kdop = WorkspaceKdop::from_points(std::move(points), options.kdop_k, radius);
        if (!kdop)
            return kdop.error();
        return WorkspaceEnvelope(std::move(kdop).value());
    }
    if (options.workspace_envelope_type == WorkspaceEnvelopeType::SupportHull) {
        auto hull = WorkspaceSupportHull::create(std::move(points), radius);
        if (!hull)
            return hull.error();
        return WorkspaceEnvelope(std::move(hull).value());
    }
    return Result<WorkspaceEnvelope>::failure(StatusCode::InvalidArgument, "unknown workspace envelope type");
}

} // namespace

const char* endpoint_aabb_source_name(EndpointAabbSource source) noexcept {
    switch (source) {
    case EndpointAabbSource::IfkAa:
        return "ifk-aa";
    case EndpointAabbSource::CritSample:
        return "crit-sample";
    }
    return "unknown";
}

Result<EndpointAabbResult> compute_endpoint_aabbs(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options) {
    auto status = validate_domain(robot, domain);
    if (!status)
        return status.error();
    switch (options.endpoint_aabb_source) {
    case EndpointAabbSource::IfkAa:
        return compute_ifk_aa_endpoint_aabbs(robot, domain);
    case EndpointAabbSource::CritSample:
        return compute_critical_sample_endpoint_aabbs(robot, domain);
    }
    return Result<EndpointAabbResult>::failure(StatusCode::InvalidArgument, "unknown endpoint AABB source");
}

namespace {

Result<LinkEnvelope> compute_link_envelope_impl(const SerialRobotModel& robot, const CspaceAabb& domain,
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
    if (options.workspace_envelope_type == WorkspaceEnvelopeType::Kdop) {
        auto directions = WorkspaceKdop::standard_directions(options.kdop_k);
        if (!directions)
            return directions.error();
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
    return compute_link_envelope_impl(robot, domain, ifk_options);
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
