#include <rbfsafe/optimization.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace rbfsafe {
namespace {

bool finite_values(std::span<const double> values) {
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
}

void append_halfspace(LinearRegionConstraint& output, std::span<const double> normal, double upper) {
    output.inequality_matrix.insert(output.inequality_matrix.end(), normal.begin(), normal.end());
    output.inequality_matrix.insert(output.inequality_matrix.end(), output.auxiliary_dimension, 0.0);
    output.inequality_upper.push_back(upper);
}

Result<LinearRegionConstraint> direct_constraint(const RegionRecord& record, const Certificate& certificate,
                                                 std::size_t dimension) {
    LinearRegionConstraint output;
    output.region_id = record.id;
    output.region_type = region_type(record.geometry);
    output.certificate_id = certificate.id;
    output.configuration_dimension = dimension;
    Configuration normal(dimension, 0.0);

    if (const auto* box = std::get_if<CspaceAabb>(&record.geometry)) {
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            std::fill(normal.begin(), normal.end(), 0.0);
            normal[axis] = 1.0;
            append_halfspace(output, normal, box->axes()[axis].upper);
            normal[axis] = -1.0;
            append_halfspace(output, normal, -box->axes()[axis].lower);
        }
        return output;
    }
    if (const auto* box = std::get_if<CspaceObb>(&record.geometry)) {
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            const auto row = std::span<const double>(box->basis()).subspan(axis * dimension, dimension);
            double center_offset = 0.0;
            for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate)
                center_offset += row[coordinate] * box->center()[coordinate];
            append_halfspace(output, row, box->half_widths()[axis] + center_offset);
            for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate)
                normal[coordinate] = -row[coordinate];
            append_halfspace(output, normal, box->half_widths()[axis] - center_offset);
        }
        return output;
    }
    if (const auto* portal = std::get_if<PortalGeometry>(&record.geometry)) {
        for (std::size_t row = 0; row < portal->intersection.constraint_count(); ++row) {
            const auto normal_row =
                std::span<const double>(portal->intersection.normals()).subspan(row * dimension, dimension);
            append_halfspace(output, normal_row, portal->intersection.offsets()[row]);
        }
        return output;
    }
    return Result<LinearRegionConstraint>::failure(
        StatusCode::InvalidArgument, "region is not a direct half-space geometry", "optimization");
}

Result<LinearRegionConstraint> generator_constraint(const RegionRecord& record,
                                                    const Certificate& certificate, std::size_t dimension) {
    LinearRegionConstraint output;
    output.region_id = record.id;
    output.region_type = region_type(record.geometry);
    output.certificate_id = certificate.id;
    output.configuration_dimension = dimension;
    Configuration center;
    std::vector<double> generators;

    if (const auto* zonotope = std::get_if<CspaceZonotope>(&record.geometry)) {
        center = zonotope->center();
        generators = zonotope->generators();
        output.auxiliary_dimension = zonotope->generator_count();
    } else if (const auto* taylor = std::get_if<CspaceTaylorRegion>(&record.geometry)) {
        center = taylor->center();
        generators = taylor->linear();
        output.auxiliary_dimension = taylor->variable_count();
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            if (taylor->remainder_radii()[axis] == 0.0)
                continue;
            generators.resize((output.auxiliary_dimension + 1) * dimension, 0.0);
            generators[output.auxiliary_dimension * dimension + axis] = taylor->remainder_radii()[axis];
            ++output.auxiliary_dimension;
        }
    } else {
        return Result<LinearRegionConstraint>::failure(StatusCode::InvalidArgument,
                                                       "region is not a generator geometry", "optimization");
    }

    output.auxiliary_bounds.assign(output.auxiliary_dimension, {-1.0, 1.0});
    const std::size_t variables = output.variable_dimension();
    output.equality_matrix.assign(dimension * variables, 0.0);
    output.equality_value = center;
    for (std::size_t axis = 0; axis < dimension; ++axis) {
        output.equality_matrix[axis * variables + axis] = 1.0;
        for (std::size_t generator = 0; generator < output.auxiliary_dimension; ++generator) {
            output.equality_matrix[axis * variables + dimension + generator] =
                -generators[generator * dimension + axis];
        }
    }
    return output;
}

Result<void> validate_projection_options(const ConstraintProjectionOptions& options) {
    if (options.maximum_iterations == 0 || options.maximum_iterations > 10'000'000 ||
        !std::isfinite(options.tolerance) || options.tolerance < 0.0 || options.tolerance > 1e-3) {
        return Result<void>::failure(StatusCode::InvalidArgument, "invalid convex projection options",
                                     "optimization");
    }
    return Result<void>::success();
}

double row_dot(std::span<const double> matrix, std::size_t row, std::size_t columns,
               std::span<const double> variables) {
    double value = 0.0;
    for (std::size_t column = 0; column < columns; ++column)
        value += matrix[row * columns + column] * variables[column];
    return value;
}

double row_norm_squared(std::span<const double> matrix, std::size_t row, std::size_t columns) {
    double value = 0.0;
    for (std::size_t column = 0; column < columns; ++column) {
        const double entry = matrix[row * columns + column];
        value += entry * entry;
    }
    return value;
}

Result<const RegionRecord*> find_record(const RegionDatabase& database, RegionId id) {
    const auto found = std::find_if(database.records().begin(), database.records().end(),
                                    [id](const RegionRecord& record) { return record.id == id; });
    if (found == database.records().end() || found->id != id) {
        return Result<const RegionRecord*>::failure(StatusCode::InvalidArgument, "region ID does not exist",
                                                    "optimization");
    }
    return &*found;
}

Result<bool> primary_record_contains(const RegionRecord& record, std::span<const double> configuration) {
    if (const auto* box = std::get_if<CspaceAabb>(&record.geometry))
        return box->contains(configuration, 1e-12);
    if (const auto* box = std::get_if<CspaceObb>(&record.geometry))
        return box->contains(configuration, 1e-12);
    if (const auto* zonotope = std::get_if<CspaceZonotope>(&record.geometry))
        return zonotope->contains(configuration, 1e-10);
    if (const auto* taylor = std::get_if<CspaceTaylorRegion>(&record.geometry))
        return taylor->contains(configuration, 1e-10);
    return false;
}

} // namespace

bool LinearRegionConstraint::valid() const noexcept {
    const std::size_t variables = variable_dimension();
    if (region_id == 0 || certificate_id.empty() || configuration_dimension == 0 ||
        auxiliary_bounds.size() != auxiliary_dimension ||
        inequality_matrix.size() != inequality_count() * variables ||
        equality_matrix.size() != equality_count() * variables || !finite_values(inequality_matrix) ||
        !finite_values(inequality_upper) || !finite_values(equality_matrix) || !finite_values(equality_value))
        return false;
    return std::all_of(auxiliary_bounds.begin(), auxiliary_bounds.end(),
                       [](const Interval& bounds) { return bounds.valid(); });
}

Result<RegionConstraintResidual> LinearRegionConstraint::evaluate(std::span<const double> configuration,
                                                                  std::span<const double> auxiliary,
                                                                  double tolerance) const {
    if (!valid()) {
        return Result<RegionConstraintResidual>::failure(
            StatusCode::InvalidArgument, "linear region constraint is invalid", "optimization");
    }
    auto configuration_status =
        validate_configuration(configuration, configuration_dimension, "optimization configuration");
    if (!configuration_status)
        return configuration_status.error();
    if (auxiliary.size() != auxiliary_dimension) {
        return Result<RegionConstraintResidual>::failure(
            StatusCode::DimensionMismatch, "optimization auxiliary dimension differs", "optimization");
    }
    if (!finite_values(auxiliary) || !std::isfinite(tolerance) || tolerance < 0.0) {
        return Result<RegionConstraintResidual>::failure(
            StatusCode::InvalidArgument, "constraint evaluation input is invalid", "optimization");
    }

    std::vector<double> variables(configuration.begin(), configuration.end());
    variables.insert(variables.end(), auxiliary.begin(), auxiliary.end());
    RegionConstraintResidual output;
    output.configuration_gradient.assign(configuration_dimension, 0.0);
    output.auxiliary_gradient.assign(auxiliary_dimension, 0.0);
    auto accumulate_gradient = [&](std::span<const double> matrix, std::size_t row, double scale) {
        for (std::size_t column = 0; column < configuration_dimension; ++column)
            output.configuration_gradient[column] += scale * matrix[row * variable_dimension() + column];
        for (std::size_t column = 0; column < auxiliary_dimension; ++column) {
            output.auxiliary_gradient[column] +=
                scale * matrix[row * variable_dimension() + configuration_dimension + column];
        }
    };

    for (std::size_t row = 0; row < inequality_count(); ++row) {
        const double residual =
            row_dot(inequality_matrix, row, variable_dimension(), variables) - inequality_upper[row];
        output.maximum_violation = std::max(output.maximum_violation, residual);
        if (residual > 0.0) {
            output.squared_penalty += residual * residual;
            accumulate_gradient(inequality_matrix, row, 2.0 * residual);
        }
    }
    for (std::size_t row = 0; row < equality_count(); ++row) {
        const double residual =
            row_dot(equality_matrix, row, variable_dimension(), variables) - equality_value[row];
        output.maximum_violation = std::max(output.maximum_violation, std::abs(residual));
        output.squared_penalty += residual * residual;
        accumulate_gradient(equality_matrix, row, 2.0 * residual);
    }
    for (std::size_t index = 0; index < auxiliary_dimension; ++index) {
        double residual = 0.0;
        double direction = 0.0;
        if (auxiliary[index] < auxiliary_bounds[index].lower) {
            residual = auxiliary_bounds[index].lower - auxiliary[index];
            direction = -1.0;
        } else if (auxiliary[index] > auxiliary_bounds[index].upper) {
            residual = auxiliary[index] - auxiliary_bounds[index].upper;
            direction = 1.0;
        }
        output.maximum_violation = std::max(output.maximum_violation, residual);
        output.squared_penalty += residual * residual;
        output.auxiliary_gradient[index] += 2.0 * residual * direction;
    }
    output.satisfied = output.maximum_violation <= tolerance;
    return output;
}

Result<ConstraintProjection>
LinearRegionConstraint::project(std::span<const double> configuration,
                                const ConstraintProjectionOptions& options) const {
    if (!valid()) {
        return Result<ConstraintProjection>::failure(StatusCode::InvalidArgument,
                                                     "linear region constraint is invalid", "optimization");
    }
    auto configuration_status =
        validate_configuration(configuration, configuration_dimension, "optimization projection input");
    if (!configuration_status)
        return configuration_status.error();
    auto option_status = validate_projection_options(options);
    if (!option_status)
        return option_status.error();

    std::vector<double> variables(configuration.begin(), configuration.end());
    variables.resize(variable_dimension(), 0.0);
    ConstraintProjection output;
    for (std::size_t iteration = 0; iteration < options.maximum_iterations; ++iteration) {
        if (options.cancellation.cancelled()) {
            return Result<ConstraintProjection>::failure(StatusCode::Cancelled,
                                                         "convex projection was cancelled", "optimization");
        }
        for (std::size_t row = 0; row < inequality_count(); ++row) {
            const double residual =
                row_dot(inequality_matrix, row, variable_dimension(), variables) - inequality_upper[row];
            if (residual <= 0.0)
                continue;
            const double norm = row_norm_squared(inequality_matrix, row, variable_dimension());
            if (!(norm > 0.0)) {
                return Result<ConstraintProjection>::failure(StatusCode::CorruptData,
                                                             "inequality has a zero normal", "optimization");
            }
            const double scale = residual / norm;
            for (std::size_t column = 0; column < variable_dimension(); ++column)
                variables[column] -= scale * inequality_matrix[row * variable_dimension() + column];
        }
        for (std::size_t row = 0; row < equality_count(); ++row) {
            const double residual =
                row_dot(equality_matrix, row, variable_dimension(), variables) - equality_value[row];
            const double norm = row_norm_squared(equality_matrix, row, variable_dimension());
            if (!(norm > 0.0)) {
                return Result<ConstraintProjection>::failure(StatusCode::CorruptData,
                                                             "equality has a zero normal", "optimization");
            }
            const double scale = residual / norm;
            for (std::size_t column = 0; column < variable_dimension(); ++column)
                variables[column] -= scale * equality_matrix[row * variable_dimension() + column];
        }
        for (std::size_t index = 0; index < auxiliary_dimension; ++index) {
            variables[configuration_dimension + index] =
                std::clamp(variables[configuration_dimension + index], auxiliary_bounds[index].lower,
                           auxiliary_bounds[index].upper);
        }
        const auto projected_configuration =
            std::span<const double>(variables).first(configuration_dimension);
        const auto projected_auxiliary = std::span<const double>(variables).subspan(configuration_dimension);
        auto residual = evaluate(projected_configuration, projected_auxiliary, options.tolerance);
        if (!residual)
            return residual.error();
        output.iterations = iteration + 1;
        output.maximum_violation = residual.value().maximum_violation;
        if (residual.value().satisfied) {
            output.converged = true;
            break;
        }
    }
    output.configuration.assign(variables.begin(),
                                variables.begin() + static_cast<std::ptrdiff_t>(configuration_dimension));
    output.auxiliary.assign(variables.begin() + static_cast<std::ptrdiff_t>(configuration_dimension),
                            variables.end());
    return output;
}

Result<LinearRegionConstraint> compile_region_constraint(const RegionDatabase& database, RegionId region_id) {
    if (database.dimension() == 0) {
        return Result<LinearRegionConstraint>::failure(StatusCode::InvalidArgument,
                                                       "region database is empty", "optimization");
    }
    auto record_result = find_record(database, region_id);
    if (!record_result)
        return record_result.error();
    const auto& record = *record_result.value();
    const auto type = region_type(record.geometry);
    if (type == RegionType::TrajectoryTube) {
        return Result<LinearRegionConstraint>::failure(
            StatusCode::InvalidArgument,
            "trajectory-tube records must be expanded into their referenced convex cells", "optimization");
    }
    if (record.certificate_index >= database.certificates().size()) {
        return Result<LinearRegionConstraint>::failure(
            StatusCode::CorruptData, "region certificate index is out of range", "optimization");
    }
    const auto& certificate = database.certificates()[record.certificate_index];
    const bool connectivity = type == RegionType::Portal;
    const auto expected_level =
        connectivity ? EvidenceLevel::CertifiedConnectivity : EvidenceLevel::CertifiedRegion;
    if (certificate.level != expected_level || certificate.robot_digest != database.robot_digest() ||
        certificate.scene_digest != database.scene_digest() || certificate.id.empty()) {
        return Result<LinearRegionConstraint>::failure(StatusCode::CorruptData,
                                                       "region certificate is inconsistent", "optimization");
    }
    if (type == RegionType::Aabb || type == RegionType::Obb || type == RegionType::Portal)
        return direct_constraint(record, certificate, database.dimension());
    if (type == RegionType::Zonotope || type == RegionType::Taylor)
        return generator_constraint(record, certificate, database.dimension());
    return Result<LinearRegionConstraint>::failure(StatusCode::InvalidArgument,
                                                   "region geometry is unsupported", "optimization");
}

Result<TrajectoryRegionAssignment> assign_trajectory_regions(const RegionDatabase& database,
                                                             std::span<const Configuration> trajectory,
                                                             const TrajectoryAssignmentOptions& options) {
    if (database.dimension() == 0 || trajectory.empty() || options.maximum_waypoints == 0 ||
        options.maximum_region_tests == 0) {
        return Result<TrajectoryRegionAssignment>::failure(
            StatusCode::InvalidArgument, "invalid trajectory assignment input", "optimization");
    }
    if (trajectory.size() > options.maximum_waypoints) {
        return Result<TrajectoryRegionAssignment>::failure(
            StatusCode::ResourceLimit, "trajectory exceeds waypoint assignment budget", "optimization");
    }
    TrajectoryRegionAssignment output;
    output.region_ids.reserve(trajectory.size());
    for (std::size_t waypoint = 0; waypoint < trajectory.size(); ++waypoint) {
        if (options.cancellation.cancelled()) {
            return Result<TrajectoryRegionAssignment>::failure(
                StatusCode::Cancelled, "trajectory region assignment was cancelled", "optimization");
        }
        auto status = validate_configuration(trajectory[waypoint], database.dimension(),
                                             "trajectory assignment waypoint");
        if (!status)
            return status.error();
        RegionId selected = 0;
        for (const auto& record : database.records()) {
            const auto type = region_type(record.geometry);
            if (type == RegionType::Portal || type == RegionType::TrajectoryTube)
                continue;
            if (output.region_tests == options.maximum_region_tests) {
                return Result<TrajectoryRegionAssignment>::failure(
                    StatusCode::ResourceLimit, "trajectory assignment reached region-test budget",
                    "optimization");
            }
            ++output.region_tests;
            auto contains = primary_record_contains(record, trajectory[waypoint]);
            if (!contains)
                return contains.error();
            if (contains.value() && (selected == 0 || record.id < selected))
                selected = record.id;
        }
        if (selected == 0) {
            output.first_unassigned_waypoint = waypoint;
            output.status =
                waypoint == 0 ? TrajectoryAssignmentStatus::Invalid : TrajectoryAssignmentStatus::Partial;
            return output;
        }
        output.region_ids.push_back(selected);
        ++output.assigned_waypoints;
    }
    output.status = TrajectoryAssignmentStatus::Complete;
    output.first_unassigned_waypoint = trajectory.size();
    return output;
}

bool TrajectoryConstraintProgram::valid() const noexcept {
    if (configuration_dimension == 0 || stages.empty() || stages.size() != region_ids.size())
        return false;
    for (std::size_t index = 0; index < stages.size(); ++index) {
        if (!stages[index].valid() || stages[index].configuration_dimension != configuration_dimension ||
            stages[index].region_id != region_ids[index])
            return false;
    }
    return true;
}

Result<TrajectoryConstraintProgram> compile_trajectory_constraints(const RegionDatabase& database,
                                                                   std::span<const RegionId> region_ids,
                                                                   OptimizationBackend backend) {
    if (database.dimension() == 0 || region_ids.empty()) {
        return Result<TrajectoryConstraintProgram>::failure(
            StatusCode::InvalidArgument, "trajectory constraint input is empty", "optimization");
    }
    if (backend != OptimizationBackend::TrajOpt && backend != OptimizationBackend::Chomp &&
        backend != OptimizationBackend::Stomp && backend != OptimizationBackend::Mpc) {
        return Result<TrajectoryConstraintProgram>::failure(
            StatusCode::InvalidArgument, "optimization backend is unsupported", "optimization");
    }
    TrajectoryConstraintProgram output;
    output.backend = backend;
    output.configuration_dimension = database.dimension();
    output.region_ids.assign(region_ids.begin(), region_ids.end());
    output.stages.reserve(region_ids.size());
    for (const auto id : region_ids) {
        auto constraint = compile_region_constraint(database, id);
        if (!constraint)
            return constraint.error();
        output.stages.push_back(std::move(constraint).value());
    }
    return output;
}

Result<ProgramEvaluation> evaluate_trajectory_constraints(const TrajectoryConstraintProgram& program,
                                                          std::span<const Configuration> trajectory,
                                                          std::span<const std::vector<double>> auxiliary,
                                                          double tolerance) {
    if (!program.valid() || trajectory.size() != program.stages.size() ||
        (!auxiliary.empty() && auxiliary.size() != program.stages.size())) {
        return Result<ProgramEvaluation>::failure(
            StatusCode::DimensionMismatch, "trajectory program input dimensions differ", "optimization");
    }
    ProgramEvaluation output;
    output.satisfied = true;
    output.stages.reserve(program.stages.size());
    for (std::size_t index = 0; index < program.stages.size(); ++index) {
        std::span<const double> stage_auxiliary;
        if (!auxiliary.empty())
            stage_auxiliary = auxiliary[index];
        else if (program.stages[index].auxiliary_dimension != 0) {
            return Result<ProgramEvaluation>::failure(
                StatusCode::DimensionMismatch, "lifted trajectory constraints require auxiliary values",
                "optimization");
        }
        auto residual = program.stages[index].evaluate(trajectory[index], stage_auxiliary, tolerance);
        if (!residual)
            return residual.error();
        output.satisfied = output.satisfied && residual.value().satisfied;
        output.maximum_violation = std::max(output.maximum_violation, residual.value().maximum_violation);
        output.squared_penalty += residual.value().squared_penalty;
        output.stages.push_back(std::move(residual).value());
    }
    return output;
}

Result<std::vector<ConstraintProjection>>
project_trajectory_constraints(const TrajectoryConstraintProgram& program,
                               std::span<const Configuration> trajectory,
                               const ConstraintProjectionOptions& options) {
    if (!program.valid() || trajectory.size() != program.stages.size()) {
        return Result<std::vector<ConstraintProjection>>::failure(
            StatusCode::DimensionMismatch, "trajectory program input dimensions differ", "optimization");
    }
    std::vector<ConstraintProjection> output;
    output.reserve(program.stages.size());
    for (std::size_t index = 0; index < program.stages.size(); ++index) {
        auto projection = program.stages[index].project(trajectory[index], options);
        if (!projection)
            return projection.error();
        output.push_back(std::move(projection).value());
    }
    return output;
}

Result<TrajectoryConstraintProgram>
TrajOptRegionAdapter::compile(const RegionDatabase& database, std::span<const RegionId> region_ids) const {
    return compile_trajectory_constraints(database, region_ids, OptimizationBackend::TrajOpt);
}

Result<TrajectoryConstraintProgram> ChompRegionAdapter::compile(const RegionDatabase& database,
                                                                std::span<const RegionId> region_ids) const {
    return compile_trajectory_constraints(database, region_ids, OptimizationBackend::Chomp);
}

Result<TrajectoryConstraintProgram> StompRegionAdapter::compile(const RegionDatabase& database,
                                                                std::span<const RegionId> region_ids) const {
    return compile_trajectory_constraints(database, region_ids, OptimizationBackend::Stomp);
}

Result<TrajectoryConstraintProgram> MpcRegionAdapter::compile(const RegionDatabase& database,
                                                              std::span<const RegionId> region_ids) const {
    return compile_trajectory_constraints(database, region_ids, OptimizationBackend::Mpc);
}

} // namespace rbfsafe
