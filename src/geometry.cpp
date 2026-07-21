#include <rbfsafe/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

} // namespace

Result<LinkEnvelope> compute_ifk_aa_link_envelope(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options) {
    auto status = validate_domain(robot, domain);
    if (!status)
        return status.error();
    if (!std::isfinite(options.obstacle_padding) || options.obstacle_padding < 0.0) {
        return Result<LinkEnvelope>::failure(StatusCode::InvalidArgument,
                                             "obstacle padding must be finite and non-negative");
    }
    const std::size_t dimension = robot.dimension();
    std::vector<AffineMatrix> prefix;
    prefix.reserve(dimension + 1);
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
    LinkEnvelope result;
    result.links.reserve(robot.link_count());
    for (std::size_t index = 0; index < robot.link_count(); ++index) {
        const auto proximal = endpoint_box(prefix[index]);
        const auto distal = endpoint_box(prefix[index + 1]);
        result.links.push_back(
            link_box(proximal, distal, robot.link_radii()[index] + options.obstacle_padding));
    }
    return result;
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
        WorkspaceAabb link_bounds;
        const double radius = robot.link_radii()[link] + obstacle_padding;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            link_bounds.lower[axis] =
                std::min(points.value()[link][axis], points.value()[link + 1][axis]) - radius;
            link_bounds.upper[axis] =
                std::max(points.value()[link][axis], points.value()[link + 1][axis]) + radius;
        }
        for (const auto& obstacle : scene.obstacles()) {
            if (link_bounds.overlaps(obstacle.bounds))
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

} // namespace rbfsafe
