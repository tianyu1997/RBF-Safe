#include <rbfsafe/higher_order.h>

#include "internal/certificate_utils.h"
#include "internal/region_identity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumMembershipIterations = 1'000'000;

bool finite_values(std::span<const double> values) {
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
}

Result<bool> zonotope_contains(const Configuration& center, std::size_t generator_count,
                               const std::vector<double>& generators, std::span<const double> configuration,
                               double tolerance, std::size_t maximum_iterations) {
    const std::size_t dimension = center.size();
    auto query = validate_configuration(configuration, dimension, "zonotope membership query");
    if (!query)
        return query.error();
    if (!std::isfinite(tolerance) || tolerance < 0.0 || maximum_iterations == 0 ||
        maximum_iterations > kMaximumMembershipIterations) {
        return Result<bool>::failure(StatusCode::InvalidArgument, "invalid zonotope membership options");
    }
    Configuration delta(dimension, 0.0);
    double query_scale = 1.0;
    for (std::size_t axis = 0; axis < dimension; ++axis) {
        delta[axis] = configuration[axis] - center[axis];
        query_scale = std::max(query_scale, std::abs(configuration[axis]));
    }
    if (generator_count == 0) {
        return std::all_of(delta.begin(), delta.end(),
                           [&](double value) { return std::abs(value) <= tolerance * query_scale; });
    }

    std::vector<double> generator_norms(generator_count, 0.0);
    double total_norm = 0.0;
    for (std::size_t generator = 0; generator < generator_count; ++generator) {
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            const double value = generators[generator * dimension + axis];
            generator_norms[generator] += value * value;
        }
        total_norm += generator_norms[generator];
    }
    if (!(total_norm > 0.0)) {
        return std::all_of(delta.begin(), delta.end(),
                           [&](double value) { return std::abs(value) <= tolerance * query_scale; });
    }
    std::vector<double> coefficients(generator_count, 0.0);
    Configuration residual = delta;
    for (std::size_t iteration = 0; iteration < maximum_iterations; ++iteration) {
        double maximum_residual = 0.0;
        for (const double value : residual)
            maximum_residual = std::max(maximum_residual, std::abs(value));
        if (maximum_residual <= tolerance * query_scale)
            return true;

        double maximum_change = 0.0;
        for (std::size_t generator = 0; generator < generator_count; ++generator) {
            if (!(generator_norms[generator] > 0.0))
                continue;
            double correction = 0.0;
            for (std::size_t axis = 0; axis < dimension; ++axis) {
                correction += generators[generator * dimension + axis] * residual[axis];
            }
            const double next =
                std::clamp(coefficients[generator] + correction / generator_norms[generator], -1.0, 1.0);
            const double change = next - coefficients[generator];
            maximum_change = std::max(maximum_change, std::abs(change));
            coefficients[generator] = next;
            for (std::size_t axis = 0; axis < dimension; ++axis)
                residual[axis] -= generators[generator * dimension + axis] * change;
        }
        if (maximum_change <= std::numeric_limits<double>::epsilon())
            break;
    }

    residual = delta;
    for (std::size_t generator = 0; generator < generator_count; ++generator) {
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            residual[axis] -= generators[generator * dimension + axis] * coefficients[generator];
        }
    }
    return std::all_of(residual.begin(), residual.end(),
                       [&](double value) { return std::abs(value) <= tolerance * query_scale; });
}

struct AffineScalar {
    double center = 0.0;
    std::vector<double> linear;
    double remainder = 0.0;

    explicit AffineScalar(std::size_t variables = 0) : linear(variables, 0.0) {}

    double linear_radius() const {
        double result = 0.0;
        for (const double value : linear)
            result += std::abs(value);
        return result;
    }
    double radius() const { return linear_radius() + std::max(0.0, remainder); }
    Interval interval() const {
        const double bound = radius();
        const double magnitude = std::max({1.0, std::abs(center - bound), std::abs(center + bound)});
        const double rounding = 2048.0 * std::numeric_limits<double>::epsilon() *
                                static_cast<double>(linear.size() + 1u) * magnitude;
        return {std::nextafter(center - bound - rounding, -std::numeric_limits<double>::infinity()),
                std::nextafter(center + bound + rounding, std::numeric_limits<double>::infinity())};
    }
};

using AffineMatrix = std::array<AffineScalar, 16>;

AffineScalar constant(double value, std::size_t variables) {
    AffineScalar result(variables);
    result.center = value;
    return result;
}

AffineScalar scaled(const AffineScalar& value, double factor) {
    AffineScalar result(value.linear.size());
    result.center = factor * value.center;
    for (std::size_t index = 0; index < value.linear.size(); ++index)
        result.linear[index] = factor * value.linear[index];
    result.remainder = std::abs(factor) * value.remainder;
    return result;
}

AffineScalar sine(const AffineScalar& argument) {
    AffineScalar result(argument.linear.size());
    result.center = std::sin(argument.center);
    const double derivative = std::cos(argument.center);
    for (std::size_t index = 0; index < argument.linear.size(); ++index)
        result.linear[index] = derivative * argument.linear[index];
    const double radius = argument.radius();
    result.remainder = std::abs(derivative) * argument.remainder + 0.5 * radius * radius;
    result.remainder +=
        256.0 * std::numeric_limits<double>::epsilon() * (1.0 + radius + std::abs(result.center));
    return result;
}

AffineScalar cosine(const AffineScalar& argument) {
    AffineScalar result(argument.linear.size());
    result.center = std::cos(argument.center);
    const double derivative = -std::sin(argument.center);
    for (std::size_t index = 0; index < argument.linear.size(); ++index)
        result.linear[index] = derivative * argument.linear[index];
    const double radius = argument.radius();
    result.remainder = std::abs(derivative) * argument.remainder + 0.5 * radius * radius;
    result.remainder +=
        256.0 * std::numeric_limits<double>::epsilon() * (1.0 + radius + std::abs(result.center));
    return result;
}

AffineScalar multiplied(const AffineScalar& left, const AffineScalar& right) {
    AffineScalar result(left.linear.size());
    result.center = left.center * right.center;
    for (std::size_t index = 0; index < result.linear.size(); ++index) {
        result.linear[index] = left.center * right.linear[index] + left.linear[index] * right.center;
    }
    const double left_linear = left.linear_radius();
    const double right_linear = right.linear_radius();
    result.remainder = left_linear * right_linear + std::abs(left.center) * right.remainder +
                       std::abs(right.center) * left.remainder + left_linear * right.remainder +
                       right_linear * left.remainder + 2.0 * left.remainder * right.remainder;
    const double scale = 1.0 + std::abs(result.center) + result.remainder + left_linear + right_linear;
    result.remainder += 512.0 * std::numeric_limits<double>::epsilon() *
                        static_cast<double>(result.linear.size() + 1u) * scale;
    return result;
}

void add_to(AffineScalar& target, const AffineScalar& value) {
    target.center += value.center;
    target.remainder += value.remainder;
    for (std::size_t index = 0; index < target.linear.size(); ++index)
        target.linear[index] += value.linear[index];
    target.remainder += 128.0 * std::numeric_limits<double>::epsilon() *
                        static_cast<double>(target.linear.size() + 1u) *
                        (1.0 + std::abs(target.center) + target.remainder);
}

AffineMatrix matrix(std::size_t variables) {
    AffineMatrix result;
    for (auto& value : result)
        value = AffineScalar(variables);
    return result;
}

AffineMatrix identity(std::size_t variables) {
    auto result = matrix(variables);
    result[0].center = result[5].center = result[10].center = result[15].center = 1.0;
    return result;
}

AffineMatrix joint_matrix(const DhJoint& joint, const AffineScalar& variable) {
    const std::size_t variables = variable.linear.size();
    auto result = matrix(variables);
    result[15].center = 1.0;
    const double cosine_alpha = std::cos(joint.alpha);
    const double sine_alpha = std::sin(joint.alpha);
    if (joint.type == JointType::Revolute) {
        AffineScalar angle = variable;
        angle.center += joint.theta;
        const auto cos_theta = cosine(angle);
        const auto sin_theta = sine(angle);
        result[0] = cos_theta;
        result[1] = scaled(sin_theta, -1.0);
        result[3] = constant(joint.a, variables);
        result[4] = scaled(sin_theta, cosine_alpha);
        result[5] = scaled(cos_theta, cosine_alpha);
        result[6] = constant(-sine_alpha, variables);
        result[7] = constant(-joint.d * sine_alpha, variables);
        result[8] = scaled(sin_theta, sine_alpha);
        result[9] = scaled(cos_theta, sine_alpha);
        result[10] = constant(cosine_alpha, variables);
        result[11] = constant(joint.d * cosine_alpha, variables);
    } else {
        const double cos_theta = std::cos(joint.theta);
        const double sin_theta = std::sin(joint.theta);
        AffineScalar displacement = variable;
        displacement.center += joint.d;
        result[0] = constant(cos_theta, variables);
        result[1] = constant(-sin_theta, variables);
        result[3] = constant(joint.a, variables);
        result[4] = constant(sin_theta * cosine_alpha, variables);
        result[5] = constant(cos_theta * cosine_alpha, variables);
        result[6] = constant(-sine_alpha, variables);
        result[7] = scaled(displacement, -sine_alpha);
        result[8] = constant(sin_theta * sine_alpha, variables);
        result[9] = constant(cos_theta * sine_alpha, variables);
        result[10] = constant(cosine_alpha, variables);
        result[11] = scaled(displacement, cosine_alpha);
    }
    return result;
}

AffineMatrix multiply(const AffineMatrix& left, const AffineMatrix& right, std::size_t variables) {
    auto output = matrix(variables);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                add_to(output[row * 4 + column],
                       multiplied(left[row * 4 + inner], right[inner * 4 + column]));
            }
        }
    }
    return output;
}

Result<void> validate_region(const SerialRobotModel& robot, const CspaceTaylorRegion& region,
                             const EnvelopeOptions& options) {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status;
    if (!region.valid())
        return Result<void>::failure(StatusCode::InvalidArgument, "Taylor region is invalid");
    if (region.dimension() != robot.dimension()) {
        return Result<void>::failure(StatusCode::DimensionMismatch,
                                     "Taylor region dimension does not match robot");
    }
    if (!std::isfinite(options.obstacle_padding) || options.obstacle_padding < 0.0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "obstacle padding must be finite and non-negative");
    }
    const auto enclosure = region.enclosing_aabb();
    for (std::size_t axis = 0; axis < enclosure.dimension(); ++axis) {
        if (enclosure.axes()[axis].lower < robot.joint_limits()[axis].lower - 1e-12 ||
            enclosure.axes()[axis].upper > robot.joint_limits()[axis].upper + 1e-12) {
            return Result<void>::failure(StatusCode::InvalidArgument, "Taylor region exceeds joint limits",
                                         std::to_string(axis));
        }
    }
    return Result<void>::success();
}

WorkspaceAabb endpoint_box(const AffineMatrix& transform) {
    WorkspaceAabb result;
    constexpr std::array<std::size_t, 3> translation_indices{3, 7, 11};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto interval = transform[translation_indices[axis]].interval();
        result.lower[axis] = interval.lower;
        result.upper[axis] = interval.upper;
    }
    return result;
}

WorkspaceAabb link_box(const WorkspaceAabb& proximal, const WorkspaceAabb& distal, double radius) {
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.lower[axis] = std::nextafter(std::min(proximal.lower[axis], distal.lower[axis]) - radius,
                                            -std::numeric_limits<double>::infinity());
        result.upper[axis] = std::nextafter(std::max(proximal.upper[axis], distal.upper[axis]) + radius,
                                            std::numeric_limits<double>::infinity());
    }
    return result;
}

Result<HigherOrderValidation> validate_envelope(const SceneSnapshot& scene, CspaceAabb enclosure,
                                                Result<LinkEnvelope> envelope_result) {
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    if (!envelope_result)
        return envelope_result.error();
    HigherOrderValidation result;
    result.disposition = ValidationDisposition::CertifiedFree;
    result.conservative_enclosure = std::move(enclosure);
    result.envelope = std::move(envelope_result).value();
    double clearance = std::numeric_limits<double>::infinity();
    for (const auto& link : result.envelope.links) {
        for (const auto& obstacle : scene.obstacles()) {
            if (link.overlaps(obstacle.bounds)) {
                result.disposition = ValidationDisposition::Undetermined;
                result.clearance_lower_bound = 0.0;
                return result;
            }
            clearance = std::min(clearance, link.distance_lower_bound(obstacle.bounds));
        }
    }
    result.clearance_lower_bound = std::isfinite(clearance) ? clearance : 0.0;
    return result;
}

} // namespace

Result<CspaceZonotope> CspaceZonotope::create(Configuration center, std::size_t generator_count,
                                              std::vector<double> generators) {
    CspaceZonotope result;
    result.center_ = std::move(center);
    result.generator_count_ = generator_count;
    result.generators_ = std::move(generators);
    if (!result.valid()) {
        return Result<CspaceZonotope>::failure(
            StatusCode::InvalidArgument, "zonotope requires finite center and generator-major coefficients");
    }
    return result;
}

bool CspaceZonotope::valid() const noexcept {
    return !center_.empty() && generator_count_ <= std::numeric_limits<std::size_t>::max() / center_.size() &&
           generators_.size() == generator_count_ * center_.size() && finite_values(center_) &&
           finite_values(generators_);
}

CspaceAabb CspaceZonotope::enclosing_aabb() const {
    if (!valid())
        return {};
    std::vector<Interval> axes;
    axes.reserve(dimension());
    for (std::size_t axis = 0; axis < dimension(); ++axis) {
        double radius = 0.0;
        for (std::size_t generator = 0; generator < generator_count_; ++generator)
            radius += std::abs(generators_[generator * dimension() + axis]);
        axes.emplace_back(std::nextafter(center_[axis] - radius, -std::numeric_limits<double>::infinity()),
                          std::nextafter(center_[axis] + radius, std::numeric_limits<double>::infinity()));
    }
    return CspaceAabb(std::move(axes));
}

Result<bool> CspaceZonotope::contains(std::span<const double> configuration, double tolerance,
                                      std::size_t maximum_iterations) const {
    if (!valid())
        return Result<bool>::failure(StatusCode::InvalidArgument, "zonotope is invalid");
    auto query = validate_configuration(configuration, dimension(), "zonotope membership query");
    if (!query)
        return query.error();
    if (!enclosing_aabb().contains(configuration, tolerance))
        return false;
    return zonotope_contains(center_, generator_count_, generators_, configuration, tolerance,
                             maximum_iterations);
}

Result<CspaceTaylorRegion> CspaceTaylorRegion::create(Configuration center, std::size_t variable_count,
                                                      std::vector<double> linear,
                                                      Configuration remainder_radii) {
    CspaceTaylorRegion result;
    result.center_ = std::move(center);
    result.variable_count_ = variable_count;
    result.linear_ = std::move(linear);
    result.remainder_radii_ = std::move(remainder_radii);
    if (!result.valid()) {
        return Result<CspaceTaylorRegion>::failure(
            StatusCode::InvalidArgument,
            "Taylor region requires finite linear coefficients and non-negative remainders");
    }
    return result;
}

Result<CspaceTaylorRegion> CspaceTaylorRegion::from_zonotope(const CspaceZonotope& region) {
    if (!region.valid())
        return Result<CspaceTaylorRegion>::failure(StatusCode::InvalidArgument, "zonotope is invalid");
    return create(region.center(), region.generator_count(), region.generators(),
                  Configuration(region.dimension(), 0.0));
}

bool CspaceTaylorRegion::valid() const noexcept {
    return !center_.empty() && remainder_radii_.size() == center_.size() &&
           variable_count_ <= std::numeric_limits<std::size_t>::max() / center_.size() &&
           linear_.size() == variable_count_ * center_.size() && finite_values(center_) &&
           finite_values(linear_) &&
           std::all_of(remainder_radii_.begin(), remainder_radii_.end(),
                       [](double value) { return std::isfinite(value) && value >= 0.0; });
}

CspaceAabb CspaceTaylorRegion::enclosing_aabb() const {
    if (!valid())
        return {};
    std::vector<Interval> axes;
    axes.reserve(dimension());
    for (std::size_t axis = 0; axis < dimension(); ++axis) {
        double radius = remainder_radii_[axis];
        for (std::size_t variable = 0; variable < variable_count_; ++variable)
            radius += std::abs(linear_[variable * dimension() + axis]);
        axes.emplace_back(std::nextafter(center_[axis] - radius, -std::numeric_limits<double>::infinity()),
                          std::nextafter(center_[axis] + radius, std::numeric_limits<double>::infinity()));
    }
    return CspaceAabb(std::move(axes));
}

Result<bool> CspaceTaylorRegion::contains(std::span<const double> configuration, double tolerance,
                                          std::size_t maximum_iterations) const {
    if (!valid())
        return Result<bool>::failure(StatusCode::InvalidArgument, "Taylor region is invalid");
    auto query = validate_configuration(configuration, dimension(), "Taylor membership query");
    if (!query)
        return query.error();
    if (!enclosing_aabb().contains(configuration, tolerance))
        return false;
    std::vector<double> generators = linear_;
    std::size_t generators_count = variable_count_;
    for (std::size_t axis = 0; axis < dimension(); ++axis) {
        if (remainder_radii_[axis] == 0.0)
            continue;
        generators.resize((generators_count + 1) * dimension(), 0.0);
        generators[generators_count * dimension() + axis] = remainder_radii_[axis];
        ++generators_count;
    }
    return zonotope_contains(center_, generators_count, generators, configuration, tolerance,
                             maximum_iterations);
}

Result<LinkEnvelope> compute_ifk_taylor_link_envelope(const SerialRobotModel& robot,
                                                      const CspaceTaylorRegion& region,
                                                      const EnvelopeOptions& options) {
    auto status = validate_region(robot, region, options);
    if (!status)
        return status.error();
    const std::size_t variables = region.variable_count();
    std::vector<AffineScalar> joints;
    joints.reserve(region.dimension());
    for (std::size_t axis = 0; axis < region.dimension(); ++axis) {
        AffineScalar value(variables);
        value.center = region.center()[axis];
        value.remainder = region.remainder_radii()[axis];
        for (std::size_t variable = 0; variable < variables; ++variable)
            value.linear[variable] = region.linear()[variable * region.dimension() + axis];
        joints.push_back(std::move(value));
    }
    std::vector<AffineMatrix> prefix;
    prefix.reserve(robot.dimension() + 1);
    prefix.push_back(identity(variables));
    for (std::size_t index = 0; index < robot.dimension(); ++index) {
        prefix.push_back(
            multiply(prefix.back(), joint_matrix(robot.joints()[index], joints[index]), variables));
    }
    LinkEnvelope result;
    result.links.reserve(robot.link_count());
    for (std::size_t index = 0; index < robot.link_count(); ++index) {
        result.links.push_back(link_box(endpoint_box(prefix[index]), endpoint_box(prefix[index + 1]),
                                        robot.link_radii()[index] + options.obstacle_padding));
    }
    return result;
}

Result<LinkEnvelope> compute_ifk_zonotope_link_envelope(const SerialRobotModel& robot,
                                                        const CspaceZonotope& region,
                                                        const EnvelopeOptions& options) {
    auto taylor = CspaceTaylorRegion::from_zonotope(region);
    if (!taylor)
        return taylor.error();
    return compute_ifk_taylor_link_envelope(robot, taylor.value(), options);
}

Result<HigherOrderValidation> HigherOrderRegionValidator::validate(const SerialRobotModel& robot,
                                                                   const SceneSnapshot& scene,
                                                                   const CspaceZonotope& region) const {
    return validate_envelope(scene, region.enclosing_aabb(),
                             compute_ifk_zonotope_link_envelope(robot, region, options_));
}

Result<HigherOrderValidation> HigherOrderRegionValidator::validate(const SerialRobotModel& robot,
                                                                   const SceneSnapshot& scene,
                                                                   const CspaceTaylorRegion& region) const {
    return validate_envelope(scene, region.enclosing_aabb(),
                             compute_ifk_taylor_link_envelope(robot, region, options_));
}

namespace {

template <class Region>
Result<Certificate> make_higher_order_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                                  const Region& region,
                                                  const HigherOrderRegionValidator& validator,
                                                  const HigherOrderValidation& validation,
                                                  double obstacle_padding, std::string subject) {
    if (validation.disposition != ValidationDisposition::CertifiedFree || !region.valid() ||
        region.dimension() != robot.dimension() || validation.envelope.links.size() != robot.link_count() ||
        !std::all_of(validation.envelope.links.begin(), validation.envelope.links.end(),
                     [](const auto& link) { return link.valid(); }) ||
        !std::isfinite(obstacle_padding) || obstacle_padding < 0.0 ||
        validator.options().obstacle_padding != obstacle_padding) {
        return Result<Certificate>::failure(
            StatusCode::InvalidArgument,
            "only a complete certified higher-order validation can issue a certificate");
    }
    return internal::make_subject_certificate(
        EvidenceLevel::CertifiedRegion, robot.digest(), scene.digest(),
        {validator.algorithm_name(), validator.algorithm_version(), obstacle_padding}, std::move(subject),
        validation.clearance_lower_bound);
}

} // namespace

Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceZonotope& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding) {
    return make_higher_order_certificate(robot, scene, region, validator, validation, obstacle_padding,
                                         internal::zonotope_subject_digest(region));
}

Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceTaylorRegion& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding) {
    return make_higher_order_certificate(robot, scene, region, validator, validation, obstacle_padding,
                                         internal::taylor_region_subject_digest(region));
}

} // namespace rbfsafe
