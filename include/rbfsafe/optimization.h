#pragma once

#include <rbfsafe/region_database.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class OptimizationBackend : std::uint8_t {
    TrajOpt = 0,
    Chomp = 1,
    Stomp = 2,
    Mpc = 3,
};

struct RegionConstraintResidual {
    bool satisfied = false;
    double maximum_violation = 0.0;
    double squared_penalty = 0.0;
    Configuration configuration_gradient;
    std::vector<double> auxiliary_gradient;
};

struct ConstraintProjectionOptions {
    std::size_t maximum_iterations = 2048;
    double tolerance = 1e-10;
    CancellationToken cancellation;
};

struct ConstraintProjection {
    bool converged = false;
    Configuration configuration;
    std::vector<double> auxiliary;
    double maximum_violation = 0.0;
    std::size_t iterations = 0;
};

// Dense row-major linear description over x = [q, z]. Inequalities use
// A*x <= b and equalities use E*x = f. Auxiliary variables have explicit box
// bounds. AABB/OBB/Portal constraints need no z; zonotope and Taylor regions
// use generator variables in [-1, 1].
class LinearRegionConstraint {
  public:
    RegionId region_id = 0;
    RegionType region_type = RegionType::Aabb;
    std::string certificate_id;
    std::size_t configuration_dimension = 0;
    std::size_t auxiliary_dimension = 0;
    std::vector<double> inequality_matrix;
    std::vector<double> inequality_upper;
    std::vector<double> equality_matrix;
    std::vector<double> equality_value;
    std::vector<Interval> auxiliary_bounds;

    std::size_t variable_dimension() const noexcept { return configuration_dimension + auxiliary_dimension; }
    std::size_t inequality_count() const noexcept { return inequality_upper.size(); }
    std::size_t equality_count() const noexcept { return equality_value.size(); }
    bool valid() const noexcept;

    Result<RegionConstraintResidual> evaluate(std::span<const double> configuration,
                                              std::span<const double> auxiliary = {},
                                              double tolerance = 1e-10) const;
    Result<ConstraintProjection> project(std::span<const double> configuration,
                                         const ConstraintProjectionOptions& options = {}) const;
};

Result<LinearRegionConstraint> compile_region_constraint(const RegionDatabase& database, RegionId region_id);

enum class TrajectoryAssignmentStatus : std::uint8_t {
    Complete = 0,
    Partial = 1,
    Invalid = 2,
};

struct TrajectoryAssignmentOptions {
    std::size_t maximum_waypoints = 1'000'000;
    std::size_t maximum_region_tests = 10'000'000;
    CancellationToken cancellation;
};

struct TrajectoryRegionAssignment {
    TrajectoryAssignmentStatus status = TrajectoryAssignmentStatus::Invalid;
    std::vector<RegionId> region_ids;
    std::size_t assigned_waypoints = 0;
    std::size_t first_unassigned_waypoint = 0;
    std::size_t region_tests = 0;
};

Result<TrajectoryRegionAssignment> assign_trajectory_regions(const RegionDatabase& database,
                                                             std::span<const Configuration> trajectory,
                                                             const TrajectoryAssignmentOptions& options = {});

struct TrajectoryConstraintProgram {
    OptimizationBackend backend = OptimizationBackend::TrajOpt;
    std::size_t configuration_dimension = 0;
    std::vector<RegionId> region_ids;
    std::vector<LinearRegionConstraint> stages;

    bool valid() const noexcept;
};

struct ProgramEvaluation {
    bool satisfied = false;
    double maximum_violation = 0.0;
    double squared_penalty = 0.0;
    std::vector<RegionConstraintResidual> stages;
};

Result<TrajectoryConstraintProgram> compile_trajectory_constraints(const RegionDatabase& database,
                                                                   std::span<const RegionId> region_ids,
                                                                   OptimizationBackend backend);
Result<ProgramEvaluation> evaluate_trajectory_constraints(const TrajectoryConstraintProgram& program,
                                                          std::span<const Configuration> trajectory,
                                                          std::span<const std::vector<double>> auxiliary = {},
                                                          double tolerance = 1e-10);
Result<std::vector<ConstraintProjection>>
project_trajectory_constraints(const TrajectoryConstraintProgram& program,
                               std::span<const Configuration> trajectory,
                               const ConstraintProjectionOptions& options = {});

class TrajOptRegionAdapter {
  public:
    Result<TrajectoryConstraintProgram> compile(const RegionDatabase& database,
                                                std::span<const RegionId> region_ids) const;
};

class ChompRegionAdapter {
  public:
    Result<TrajectoryConstraintProgram> compile(const RegionDatabase& database,
                                                std::span<const RegionId> region_ids) const;
};

class StompRegionAdapter {
  public:
    Result<TrajectoryConstraintProgram> compile(const RegionDatabase& database,
                                                std::span<const RegionId> region_ids) const;
};

class MpcRegionAdapter {
  public:
    Result<TrajectoryConstraintProgram> compile(const RegionDatabase& database,
                                                std::span<const RegionId> region_ids) const;
};

} // namespace rbfsafe
