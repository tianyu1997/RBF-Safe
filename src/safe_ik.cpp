#include <rbfsafe/safe_ik.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rbfsafe {
namespace {

using Vector6 = std::array<double, 6>;
using Matrix6 = std::array<double, 36>;

struct PoseResidual {
    Vector6 vector{};
    double position_norm = 0.0;
    double orientation_norm = 0.0;
};

std::array<double, 4> normalized_quaternion(std::array<double, 4> quaternion) {
    double norm_squared = 0.0;
    for (const double value : quaternion)
        norm_squared += value * value;
    const double inverse_norm = 1.0 / std::sqrt(norm_squared);
    for (double& value : quaternion)
        value *= inverse_norm;
    return quaternion;
}

std::array<double, 4> conjugate(const std::array<double, 4>& quaternion) {
    return {-quaternion[0], -quaternion[1], -quaternion[2], quaternion[3]};
}

std::array<double, 4> multiply(const std::array<double, 4>& left, const std::array<double, 4>& right) {
    return {left[3] * right[0] + left[0] * right[3] + left[1] * right[2] - left[2] * right[1],
            left[3] * right[1] - left[0] * right[2] + left[1] * right[3] + left[2] * right[0],
            left[3] * right[2] + left[0] * right[1] - left[1] * right[0] + left[2] * right[3],
            left[3] * right[3] - left[0] * right[0] - left[1] * right[1] - left[2] * right[2]};
}

std::array<double, 3> rotation_vector(const std::array<double, 4>& from, const std::array<double, 4>& to) {
    auto relative =
        normalized_quaternion(multiply(normalized_quaternion(to), conjugate(normalized_quaternion(from))));
    if (relative[3] < 0.0) {
        for (double& value : relative)
            value = -value;
    }
    const double sine_half =
        std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
    if (sine_half <= 1e-15)
        return {2.0 * relative[0], 2.0 * relative[1], 2.0 * relative[2]};
    const double angle = 2.0 * std::atan2(sine_half, std::clamp(relative[3], 0.0, 1.0));
    const double scale = angle / sine_half;
    return {scale * relative[0], scale * relative[1], scale * relative[2]};
}

PoseResidual residual(const Pose3d& current, const Pose3d& target) {
    PoseResidual result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.vector[axis] = target.position[axis] - current.position[axis];
        result.position_norm += result.vector[axis] * result.vector[axis];
    }
    const auto orientation = rotation_vector(current.orientation, target.orientation);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.vector[axis + 3] = orientation[axis];
        result.orientation_norm += orientation[axis] * orientation[axis];
    }
    result.position_norm = std::sqrt(result.position_norm);
    result.orientation_norm = std::sqrt(result.orientation_norm);
    return result;
}

double weighted_objective(const PoseResidual& value, double orientation_weight) {
    double result = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis)
        result += value.vector[axis] * value.vector[axis];
    for (std::size_t axis = 3; axis < 6; ++axis)
        result += orientation_weight * orientation_weight * value.vector[axis] * value.vector[axis];
    return result;
}

Configuration project_to_box(std::span<const double> configuration, const CspaceAabb& box) {
    Configuration result(configuration.begin(), configuration.end());
    for (std::size_t axis = 0; axis < box.dimension(); ++axis)
        result[axis] = std::clamp(result[axis], box.axes()[axis].lower, box.axes()[axis].upper);
    return result;
}

double squared_distance(std::span<const double> first, std::span<const double> second) {
    double result = 0.0;
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        const double difference = first[axis] - second[axis];
        result += difference * difference;
    }
    return result;
}

double point_box_distance_squared(std::span<const double> point, const CspaceAabb& box) {
    double result = 0.0;
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        double difference = 0.0;
        if (point[axis] < box.axes()[axis].lower)
            difference = box.axes()[axis].lower - point[axis];
        else if (point[axis] > box.axes()[axis].upper)
            difference = point[axis] - box.axes()[axis].upper;
        result += difference * difference;
    }
    return result;
}

bool solve_linear_system(Matrix6 matrix, Vector6 right, Vector6& solution) {
    for (std::size_t pivot = 0; pivot < 6; ++pivot) {
        std::size_t selected = pivot;
        for (std::size_t row = pivot + 1; row < 6; ++row) {
            if (std::abs(matrix[row * 6 + pivot]) > std::abs(matrix[selected * 6 + pivot]))
                selected = row;
        }
        if (std::abs(matrix[selected * 6 + pivot]) <= 1e-18)
            return false;
        if (selected != pivot) {
            for (std::size_t column = pivot; column < 6; ++column)
                std::swap(matrix[pivot * 6 + column], matrix[selected * 6 + column]);
            std::swap(right[pivot], right[selected]);
        }
        const double inverse = 1.0 / matrix[pivot * 6 + pivot];
        for (std::size_t column = pivot; column < 6; ++column)
            matrix[pivot * 6 + column] *= inverse;
        right[pivot] *= inverse;
        for (std::size_t row = 0; row < 6; ++row) {
            if (row == pivot)
                continue;
            const double factor = matrix[row * 6 + pivot];
            for (std::size_t column = pivot; column < 6; ++column)
                matrix[row * 6 + column] -= factor * matrix[pivot * 6 + column];
            right[row] -= factor * right[pivot];
        }
    }
    solution = right;
    return true;
}

struct IterationResult {
    bool converged = false;
    Configuration configuration;
    PoseResidual residual;
};

Result<IterationResult> solve_in_region(const SerialRobotModel& robot, const Pose3d& target,
                                        const CspaceAabb& region, Configuration initial,
                                        const SafeIkOptions& options, SafeIkStats& stats) {
    Configuration configuration = project_to_box(initial, region);
    for (std::size_t iteration = 0; iteration <= options.maximum_iterations; ++iteration) {
        if (options.cancellation.cancelled())
            return Result<IterationResult>::failure(StatusCode::Cancelled, "Safe IK was cancelled");
        auto current_pose = robot.end_effector_pose(configuration);
        ++stats.pose_evaluations;
        if (!current_pose)
            return current_pose.error();
        const PoseResidual current_residual = residual(current_pose.value(), target);
        if (current_residual.position_norm <= options.position_tolerance &&
            current_residual.orientation_norm <= options.orientation_tolerance) {
            return IterationResult{true, std::move(configuration), current_residual};
        }
        if (iteration == options.maximum_iterations)
            return IterationResult{false, std::move(configuration), current_residual};
        ++stats.iterations;

        std::vector<double> jacobian(6 * robot.dimension(), 0.0);
        for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
            const double plus_step =
                std::min(options.finite_difference_step,
                         std::max(0.0, region.axes()[joint].upper - configuration[joint]));
            const double minus_step =
                std::min(options.finite_difference_step,
                         std::max(0.0, configuration[joint] - region.axes()[joint].lower));
            if (plus_step <= 0.0 && minus_step <= 0.0)
                continue;

            Pose3d lower_pose = current_pose.value();
            Pose3d upper_pose = current_pose.value();
            if (minus_step > 0.0) {
                Configuration lower = configuration;
                lower[joint] -= minus_step;
                auto evaluated = robot.end_effector_pose(lower);
                ++stats.pose_evaluations;
                if (!evaluated)
                    return evaluated.error();
                lower_pose = evaluated.value();
            }
            if (plus_step > 0.0) {
                Configuration upper = configuration;
                upper[joint] += plus_step;
                auto evaluated = robot.end_effector_pose(upper);
                ++stats.pose_evaluations;
                if (!evaluated)
                    return evaluated.error();
                upper_pose = evaluated.value();
            }
            const double span = plus_step + minus_step;
            for (std::size_t axis = 0; axis < 3; ++axis)
                jacobian[axis * robot.dimension() + joint] =
                    (upper_pose.position[axis] - lower_pose.position[axis]) / span;
            const auto orientation = rotation_vector(lower_pose.orientation, upper_pose.orientation);
            for (std::size_t axis = 0; axis < 3; ++axis)
                jacobian[(axis + 3) * robot.dimension() + joint] = orientation[axis] / span;
        }

        Matrix6 normal{};
        Vector6 weighted_error = current_residual.vector;
        for (std::size_t row = 3; row < 6; ++row)
            weighted_error[row] *= options.orientation_weight;
        for (std::size_t row = 0; row < 6; ++row) {
            for (std::size_t column = 0; column < 6; ++column) {
                double value = 0.0;
                for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
                    double left = jacobian[row * robot.dimension() + joint];
                    double right = jacobian[column * robot.dimension() + joint];
                    if (row >= 3)
                        left *= options.orientation_weight;
                    if (column >= 3)
                        right *= options.orientation_weight;
                    value += left * right;
                }
                normal[row * 6 + column] = value;
            }
            normal[row * 6 + row] += options.damping * options.damping;
        }
        Vector6 multiplier{};
        if (!solve_linear_system(normal, weighted_error, multiplier))
            return IterationResult{false, std::move(configuration), current_residual};

        Configuration step(robot.dimension(), 0.0);
        double step_norm_squared = 0.0;
        for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
            for (std::size_t row = 0; row < 6; ++row) {
                double derivative = jacobian[row * robot.dimension() + joint];
                if (row >= 3)
                    derivative *= options.orientation_weight;
                step[joint] += derivative * multiplier[row];
            }
            step_norm_squared += step[joint] * step[joint];
        }
        double step_norm = std::sqrt(step_norm_squared);
        if (step_norm <= options.minimum_step_norm)
            return IterationResult{false, std::move(configuration), current_residual};
        if (step_norm > options.maximum_step_norm) {
            const double scale = options.maximum_step_norm / step_norm;
            for (double& value : step)
                value *= scale;
        }

        const double current_objective = weighted_objective(current_residual, options.orientation_weight);
        bool accepted = false;
        for (std::size_t line_search = 0; line_search <= options.maximum_line_search_steps; ++line_search) {
            const double scale = std::ldexp(1.0, -static_cast<int>(line_search));
            Configuration candidate = configuration;
            for (std::size_t joint = 0; joint < candidate.size(); ++joint)
                candidate[joint] = std::clamp(candidate[joint] + scale * step[joint],
                                              region.axes()[joint].lower, region.axes()[joint].upper);
            if (squared_distance(candidate, configuration) <=
                options.minimum_step_norm * options.minimum_step_norm)
                continue;
            auto candidate_pose = robot.end_effector_pose(candidate);
            ++stats.pose_evaluations;
            if (!candidate_pose)
                return candidate_pose.error();
            const auto candidate_residual = residual(candidate_pose.value(), target);
            if (weighted_objective(candidate_residual, options.orientation_weight) < current_objective) {
                configuration = std::move(candidate);
                accepted = true;
                break;
            }
        }
        if (!accepted)
            return IterationResult{false, std::move(configuration), current_residual};
    }
    return Result<IterationResult>::failure(StatusCode::InternalError, "Safe IK iteration escaped its bound");
}

bool valid_options(const SafeIkOptions& options) {
    return std::isfinite(options.position_tolerance) && options.position_tolerance >= 0.0 &&
           std::isfinite(options.orientation_tolerance) && options.orientation_tolerance >= 0.0 &&
           std::isfinite(options.orientation_weight) && options.orientation_weight > 0.0 &&
           std::isfinite(options.damping) && options.damping > 0.0 &&
           std::isfinite(options.finite_difference_step) && options.finite_difference_step > 0.0 &&
           std::isfinite(options.maximum_step_norm) && options.maximum_step_norm > 0.0 &&
           std::isfinite(options.minimum_step_norm) && options.minimum_step_norm >= 0.0 &&
           options.minimum_step_norm < options.maximum_step_norm && options.maximum_iterations <= 10'000 &&
           options.maximum_region_attempts > 0 && options.maximum_region_attempts <= 1'000'000 &&
           options.maximum_line_search_steps <= 64;
}

} // namespace

Result<SafeIkReport> SafeIkSolver::solve(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                         const SafeAtlas& atlas, const Pose3d& target,
                                         std::span<const double> current,
                                         const SafeIkOptions& options) const {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    auto compatibility = atlas.verify_compatible(robot, scene);
    if (!compatibility)
        return compatibility.error();
    auto current_status = validate_configuration(current, robot.dimension(), "Safe IK seed");
    if (!current_status)
        return current_status.error();
    for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
        if (!robot.joint_limits()[joint].contains(current[joint], 1e-12)) {
            return Result<SafeIkReport>::failure(StatusCode::InvalidArgument,
                                                 "Safe IK seed exceeds joint limits", std::to_string(joint));
        }
    }
    if (!target.valid(1e-6))
        return Result<SafeIkReport>::failure(StatusCode::InvalidArgument, "Safe IK target pose is invalid");
    if (!valid_options(options))
        return Result<SafeIkReport>::failure(StatusCode::InvalidArgument, "Safe IK options are invalid");
    if (atlas.regions().empty())
        return SafeIkReport{};

    SafeIkReport report;
    if (options.require_connectivity && !atlas.contains(current)) {
        report.status = SafeIkStatus::SeedNotCertified;
        return report;
    }

    std::vector<std::size_t> region_order(atlas.regions().size());
    std::iota(region_order.begin(), region_order.end(), 0);
    std::sort(region_order.begin(), region_order.end(), [&](std::size_t left, std::size_t right) {
        const double left_distance = point_box_distance_squared(current, atlas.regions()[left].bounds);
        const double right_distance = point_box_distance_squared(current, atlas.regions()[right].bounds);
        if (left_distance != right_distance)
            return left_distance < right_distance;
        return atlas.regions()[left].id < atlas.regions()[right].id;
    });

    std::optional<SafeIkReport> disconnected;
    for (const auto region_index : region_order) {
        const auto& region = atlas.regions()[region_index];
        std::array<Configuration, 2> seeds{project_to_box(current, region.bounds), region.bounds.center()};
        for (std::size_t seed_index = 0; seed_index < seeds.size(); ++seed_index) {
            if (seed_index == 1 &&
                squared_distance(seeds[0], seeds[1]) <= options.minimum_step_norm * options.minimum_step_norm)
                continue;
            if (report.stats.region_attempts == options.maximum_region_attempts)
                break;
            ++report.stats.region_attempts;
            auto solved =
                solve_in_region(robot, target, region.bounds, seeds[seed_index], options, report.stats);
            if (!solved)
                return solved.error();
            if (!solved.value().converged)
                continue;

            SafeIkReport candidate;
            candidate.solution = std::move(solved.value().configuration);
            candidate.region_id = region.id;
            if (region.certificate_index >= atlas.certificates().size()) {
                return Result<SafeIkReport>::failure(StatusCode::InternalError,
                                                     "Safe IK region certificate is missing");
            }
            const auto& region_certificate = atlas.certificates()[region.certificate_index];
            if (region_certificate.level != EvidenceLevel::CertifiedRegion ||
                region_certificate.robot_digest != atlas.robot_digest() ||
                region_certificate.scene_digest != atlas.scene_digest()) {
                return Result<SafeIkReport>::failure(
                    StatusCode::InternalError, "Safe IK region certificate has an invalid level or identity");
            }
            candidate.region_certificate = region_certificate;
            candidate.pose_evidence = EvidenceLevel::PointChecked;
            candidate.position_error = solved.value().residual.position_norm;
            candidate.orientation_error = solved.value().residual.orientation_norm;
            candidate.stats = report.stats;
            auto route = atlas.route(current, candidate.solution);
            if (!route)
                return route.error();
            if (route.value()) {
                candidate.status = SafeIkStatus::SafeConnected;
                candidate.connectivity_route = std::move(route).value();
                return candidate;
            }

            candidate.status = SafeIkStatus::SafeUnconnected;
            ++report.stats.disconnected_solutions;
            candidate.stats = report.stats;
            if (!disconnected)
                disconnected = candidate;
            if (!options.require_connectivity)
                return candidate;
        }
        if (report.stats.region_attempts == options.maximum_region_attempts)
            break;
    }
    if (disconnected) {
        disconnected->stats = report.stats;
        return std::move(*disconnected);
    }
    report.status = SafeIkStatus::NoSolution;
    return report;
}

} // namespace rbfsafe
