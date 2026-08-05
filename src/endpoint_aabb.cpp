#include <rbfsafe/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace rbfsafe {
namespace {

constexpr double kCritSampleNarrowThreshold = 0.01;
constexpr std::size_t kCritSampleMaxCombinations = 8192;

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

struct AffineScalar {
    double center = 0.0;
    std::vector<double> linear;
    double remainder = 0.0;

    explicit AffineScalar(std::size_t dimension = 0) : linear(dimension, 0.0) {}

    double radius() const {
        double result = std::max(0.0, remainder);
        for (const double coefficient : linear)
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

AffineMatrix make_affine_matrix(std::size_t dimension) {
    AffineMatrix result;
    for (auto& value : result)
        value = AffineScalar(dimension);
    return result;
}

AffineMatrix affine_identity(std::size_t dimension) {
    auto result = make_affine_matrix(dimension);
    result[0].center = result[5].center = result[10].center = result[15].center = 1.0;
    return result;
}

AffineMatrix affine_joint(const DhJoint& joint, const Interval& interval, std::size_t joint_index,
                          std::size_t dimension) {
    auto matrix = make_affine_matrix(dimension);
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
    for (std::size_t index = 0; index < result.linear.size(); ++index)
        result.linear[index] = left.center * right.linear[index] + left.linear[index] * right.center;

    double left_linear_radius = 0.0;
    double right_linear_radius = 0.0;
    for (const double value : left.linear)
        left_linear_radius += std::abs(value);
    for (const double value : right.linear)
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
    auto output = make_affine_matrix(dimension);
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

WorkspaceAabb endpoint_box(const AffineMatrix& matrix) {
    WorkspaceAabb result;
    constexpr std::array<std::size_t, 3> translation_indices{3, 7, 11};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto interval = matrix[translation_indices[axis]].interval();
        result.lower[axis] = interval.lower;
        result.upper[axis] = interval.upper;
    }
    return result;
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

using RigidMatrix = std::array<double, 12>;

RigidMatrix rigid_identity() {
    RigidMatrix result{};
    result[0] = result[5] = result[10] = 1.0;
    return result;
}

RigidMatrix rigid_joint(const DhJoint& joint, double value) {
    const double d = joint.type == JointType::Prismatic ? joint.d + value : joint.d;
    const double angle = joint.type == JointType::Revolute ? joint.theta + value : joint.theta;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double cosine_alpha = std::cos(joint.alpha);
    const double sine_alpha = std::sin(joint.alpha);
    return {cosine,
            -sine,
            0.0,
            joint.a,
            sine * cosine_alpha,
            cosine * cosine_alpha,
            -sine_alpha,
            -d * sine_alpha,
            sine * sine_alpha,
            cosine * sine_alpha,
            cosine_alpha,
            d * cosine_alpha};
}

RigidMatrix rigid_multiply(const RigidMatrix& left, const RigidMatrix& right) {
    RigidMatrix result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t inner = 0; inner < 3; ++inner)
                result[row * 4 + column] += left[row * 4 + inner] * right[inner * 4 + column];
        }
        result[row * 4 + 3] = left[row * 4 + 3];
        for (std::size_t inner = 0; inner < 3; ++inner)
            result[row * 4 + 3] += left[row * 4 + inner] * right[inner * 4 + 3];
    }
    return result;
}

WorkspacePoint translation(const RigidMatrix& matrix) { return {matrix[3], matrix[7], matrix[11]}; }

void expand_sampled_endpoint(WorkspaceAabb& bounds, const WorkspacePoint& point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds.lower[axis] = std::min(bounds.lower[axis], point[axis]);
        bounds.upper[axis] = std::max(bounds.upper[axis], point[axis]);
    }
}

void update_sampled_endpoints(EndpointAabbResult& result, const std::vector<RigidMatrix>& prefix,
                              const std::optional<RigidMatrix>& tool) {
    for (std::size_t link = 0; link < prefix.size() - 1; ++link) {
        expand_sampled_endpoint(result.endpoints[link * 2], translation(prefix[link]));
        expand_sampled_endpoint(result.endpoints[link * 2 + 1], translation(prefix[link + 1]));
    }
    if (tool) {
        const std::size_t link = prefix.size() - 1;
        expand_sampled_endpoint(result.endpoints[link * 2], translation(prefix.back()));
        expand_sampled_endpoint(result.endpoints[link * 2 + 1], translation(*tool));
    }
}

Result<EndpointAabbResult> compute_critical_sample_endpoint_aabbs(const SerialRobotModel& robot,
                                                                  const CspaceAabb& domain) {
    const auto candidates = capped_critical_candidates(domain);
    const std::size_t total = combination_count(candidates);
    if (total == 0 || total == std::numeric_limits<std::size_t>::max()) {
        return Result<EndpointAabbResult>::failure(StatusCode::ResourceLimit,
                                                   "critical-sample combination count is invalid");
    }

    std::vector<std::vector<RigidMatrix>> candidate_matrices(robot.dimension());
    for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
        candidate_matrices[joint].reserve(candidates[joint].size());
        for (const double value : candidates[joint])
            candidate_matrices[joint].push_back(rigid_joint(robot.joints()[joint], value));
    }
    const std::optional<RigidMatrix> tool_matrix =
        robot.tool_frame() ? std::optional<RigidMatrix>(rigid_joint(*robot.tool_frame(), 0.0)) : std::nullopt;

    EndpointAabbResult result;
    result.source = EndpointAabbSource::CritSample;
    result.certified = false;
    result.endpoints.resize(robot.link_count() * 2);
    for (auto& endpoint : result.endpoints) {
        endpoint.lower.fill(std::numeric_limits<double>::infinity());
        endpoint.upper.fill(-std::numeric_limits<double>::infinity());
    }

    std::vector<std::size_t> indices(robot.dimension(), 0);
    std::vector<RigidMatrix> prefix(robot.dimension() + 1);
    prefix.front() = rigid_identity();
    for (std::size_t joint = 0; joint < robot.dimension(); ++joint)
        prefix[joint + 1] = rigid_multiply(prefix[joint], candidate_matrices[joint].front());

    for (std::size_t combination = 0; combination < total; ++combination) {
        const std::optional<RigidMatrix> tool =
            tool_matrix ? std::optional<RigidMatrix>(rigid_multiply(prefix.back(), *tool_matrix))
                        : std::nullopt;
        update_sampled_endpoints(result, prefix, tool);
        ++result.evaluated_configurations;

        if (combination + 1 == total)
            break;

        std::size_t changed_joint = robot.dimension();
        while (changed_joint > 0) {
            --changed_joint;
            ++indices[changed_joint];
            if (indices[changed_joint] < candidates[changed_joint].size())
                break;
            indices[changed_joint] = 0;
        }
        for (std::size_t joint = changed_joint; joint < robot.dimension(); ++joint) {
            prefix[joint + 1] = rigid_multiply(prefix[joint], candidate_matrices[joint][indices[joint]]);
        }
    }
    return result;
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

} // namespace rbfsafe
