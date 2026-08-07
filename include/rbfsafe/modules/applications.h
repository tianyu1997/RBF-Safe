#pragma once

#include <rbfsafe/modules/atlas.h>

// Certified corridors.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

using PortalId = std::uint64_t;

class CspaceObb {
  public:
    CspaceObb() = default;

    static Result<CspaceObb> create(Configuration center, std::vector<double> basis,
                                    Configuration half_widths);

    std::size_t dimension() const noexcept { return center_.size(); }
    const Configuration& center() const noexcept { return center_; }
    const std::vector<double>& basis() const noexcept { return basis_; }
    const Configuration& half_widths() const noexcept { return half_widths_; }

    bool valid() const noexcept;
    bool contains(std::span<const double> configuration, double tolerance = 0.0) const noexcept;
    bool contains(const Configuration& configuration, double tolerance = 0.0) const noexcept {
        return contains(std::span<const double>(configuration), tolerance);
    }
    CspaceAabb enclosing_aabb() const;
    double volume() const noexcept;

  private:
    Configuration center_;
    std::vector<double> basis_;
    Configuration half_widths_;
};

class ObbGenerator {
  public:
    static Result<CspaceObb> segment_tube(std::span<const double> first, std::span<const double> second,
                                          double lateral_half_width, double longitudinal_margin = 0.0);
};

struct ObbValidation {
    ValidationDisposition disposition = ValidationDisposition::Undetermined;
    double clearance_lower_bound = 0.0;
    CspaceAabb conservative_enclosure;
    LinkEnvelope envelope;
};

class ObbRegionValidator {
  public:
    explicit ObbRegionValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<ObbValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                   const CspaceObb& region) const;
    std::string algorithm_name() const { return "ifk-aa-link-iaabb-obb-enclosure"; }
    std::string algorithm_version() const { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

struct ObbGrowthOptions {
    double initial_lateral_half_width = 1e-3;
    double maximum_lateral_half_width = 5e-2;
    double longitudinal_margin = 0.0;
    std::size_t maximum_iterations = 12;
    std::size_t maximum_validations = 128;
    double obstacle_padding = 0.0;
    CancellationToken cancellation;
};

struct ObbGrowthResult {
    bool certified = false;
    CspaceObb region;
    ObbValidation validation;
    double achieved_lateral_half_width = 0.0;
    std::size_t validations = 0;
    std::size_t growth_attempts = 0;
};

class ObbGrower {
  public:
    ObbGrower();
    explicit ObbGrower(std::shared_ptr<const ObbRegionValidator> validator);

    Result<ObbGrowthResult> grow(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                 std::span<const double> first, std::span<const double> second,
                                 const ObbGrowthOptions& options = {}) const;

  private:
    std::shared_ptr<const ObbRegionValidator> validator_;
};

struct HipacOptions {
    double minimum_lateral_half_width = 1e-3;
    double maximum_lateral_half_width = 5e-2;
    double longitudinal_margin = 0.0;
    std::size_t growth_iterations = 12;
    std::size_t maximum_subdivision_depth = 8;
    std::size_t maximum_validations = 100'000;
    double portal_tolerance = 1e-12;
    double obstacle_padding = 0.0;
    CancellationToken cancellation;
};

struct HipacBuildStats {
    std::size_t validations = 0;
    std::size_t recursive_splits = 0;
    std::size_t certified_cells = 0;
    std::size_t failed_leaf_segments = 0;
    std::size_t growth_attempts = 0;
    std::size_t portals = 0;
};

struct CertifiedObbRegion {
    RegionId id = 0;
    CspaceObb bounds;
    Certificate certificate;
    ComponentId component = 0;
    std::size_t segment_index = 0;
    double start_fraction = 0.0;
    double end_fraction = 0.0;
    Configuration entry;
    Configuration exit;
};

struct PortalRegion {
    PortalId id = 0;
    RegionId left_region = 0;
    RegionId right_region = 0;
    Configuration witness;
    Certificate certificate;
};

struct CertifiedRoute {
    std::vector<Configuration> waypoints;
    std::vector<RegionId> region_sequence;
    std::vector<PortalId> portal_sequence;
    Certificate certificate;
};

class HipacCorridor {
  public:
    HipacCorridor() = default;

    std::size_t dimension() const noexcept { return dimension_; }
    const std::string& robot_digest() const noexcept { return robot_digest_; }
    const std::string& scene_digest() const noexcept { return scene_digest_; }
    const std::vector<CertifiedObbRegion>& regions() const noexcept { return regions_; }
    const std::vector<PortalRegion>& portals() const noexcept { return portals_; }

    Result<std::vector<RegionId>> regions_at(std::span<const double> configuration) const;
    bool contains(std::span<const double> configuration) const;
    Result<bool> connected(std::span<const double> first, std::span<const double> second) const;
    Result<std::optional<CertifiedRoute>> route(std::span<const double> first,
                                                std::span<const double> second) const;
    Result<void> verify_compatible(const SerialRobotModel& robot, const SceneSnapshot& scene) const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<HipacCorridor> load(const std::filesystem::path& directory);

  private:
    friend class HipacCorridorBuilder;
    friend Result<void> save_corridor_directory(const HipacCorridor&, const std::filesystem::path&,
                                                const SaveOptions&);
    friend Result<HipacCorridor> load_corridor_directory(const std::filesystem::path&);

    std::size_t dimension_ = 0;
    std::string robot_digest_;
    std::string scene_digest_;
    std::vector<CertifiedObbRegion> regions_;
    std::vector<PortalRegion> portals_;
};

enum class HipacBuildStatus : std::uint8_t {
    Certified = 0,
    Partial = 1,
    Invalid = 2,
};

struct HipacBuildReport {
    HipacBuildStatus status = HipacBuildStatus::Invalid;
    double coverage_ratio = 0.0;
    std::size_t waypoint_count = 0;
    std::size_t segment_count = 0;
    std::vector<TrajectoryInterval> uncovered_intervals;
    HipacCorridor corridor;
    HipacBuildStats stats;
};

class HipacCorridorBuilder {
  public:
    HipacCorridorBuilder();
    explicit HipacCorridorBuilder(std::shared_ptr<const ObbRegionValidator> validator);

    Result<HipacBuildReport> build(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                   std::span<const Configuration> path,
                                   const HipacOptions& options = {}) const;

  private:
    std::shared_ptr<const ObbRegionValidator> validator_;
};

} // namespace rbfsafe

// Higher-order region representations.

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

// A C-space zonotope q = center + sum(generator_k * xi_k), xi_k in [-1, 1].
// Generators use generator-major row order: generators[k * dimension + axis].
class CspaceZonotope {
  public:
    CspaceZonotope() = default;

    static Result<CspaceZonotope> create(Configuration center, std::size_t generator_count,
                                         std::vector<double> generators);

    std::size_t dimension() const noexcept { return center_.size(); }
    std::size_t generator_count() const noexcept { return generator_count_; }
    const Configuration& center() const noexcept { return center_; }
    const std::vector<double>& generators() const noexcept { return generators_; }

    bool valid() const noexcept;
    CspaceAabb enclosing_aabb() const;
    Result<bool> contains(std::span<const double> configuration, double tolerance = 1e-10,
                          std::size_t maximum_iterations = 512) const;

  private:
    Configuration center_;
    std::size_t generator_count_ = 0;
    std::vector<double> generators_;
};

// First-order Taylor region with an independent interval remainder per joint:
// q = center + sum(linear_k * xi_k) + remainder, |remainder_i| <= radius_i.
class CspaceTaylorRegion {
  public:
    CspaceTaylorRegion() = default;

    static Result<CspaceTaylorRegion> create(Configuration center, std::size_t variable_count,
                                             std::vector<double> linear, Configuration remainder_radii);
    static Result<CspaceTaylorRegion> from_zonotope(const CspaceZonotope& region);

    std::size_t dimension() const noexcept { return center_.size(); }
    std::size_t variable_count() const noexcept { return variable_count_; }
    const Configuration& center() const noexcept { return center_; }
    const std::vector<double>& linear() const noexcept { return linear_; }
    const Configuration& remainder_radii() const noexcept { return remainder_radii_; }

    bool valid() const noexcept;
    CspaceAabb enclosing_aabb() const;
    Result<bool> contains(std::span<const double> configuration, double tolerance = 1e-10,
                          std::size_t maximum_iterations = 512) const;

  private:
    Configuration center_;
    std::size_t variable_count_ = 0;
    std::vector<double> linear_;
    Configuration remainder_radii_;
};

struct HigherOrderValidation {
    ValidationDisposition disposition = ValidationDisposition::Undetermined;
    double clearance_lower_bound = 0.0;
    CspaceAabb conservative_enclosure;
    LinkEnvelope envelope;
};

// Correlation-preserving first-order Taylor FK. Shared generator variables are
// retained through trigonometric linearization and matrix products; nonlinear
// and floating-point residuals are accumulated conservatively.
Result<LinkEnvelope> compute_ifk_taylor_link_envelope(const SerialRobotModel& robot,
                                                      const CspaceTaylorRegion& region,
                                                      const EnvelopeOptions& options = {});
Result<LinkEnvelope> compute_ifk_zonotope_link_envelope(const SerialRobotModel& robot,
                                                        const CspaceZonotope& region,
                                                        const EnvelopeOptions& options = {});

class HigherOrderRegionValidator {
  public:
    explicit HigherOrderRegionValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<HigherOrderValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                           const CspaceZonotope& region) const;
    Result<HigherOrderValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                           const CspaceTaylorRegion& region) const;
    std::string algorithm_name() const { return "ifk-taylor1-link-iaabb"; }
    std::string algorithm_version() const { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceZonotope& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding = 0.0);
Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceTaylorRegion& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding = 0.0);

} // namespace rbfsafe

// Unified region database.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rbfsafe {

class RegionDatabase;
Result<void> save_region_database_directory(const RegionDatabase& database,
                                            const std::filesystem::path& directory,
                                            const SaveOptions& options);
Result<RegionDatabase> load_region_database_directory(const std::filesystem::path& directory);

enum class RegionType : std::uint8_t {
    Aabb = 0,
    Obb = 1,
    Portal = 2,
    TrajectoryTube = 3,
    Zonotope = 4,
    Taylor = 5,
};

struct PortalIntersectionOptions {
    std::size_t maximum_iterations = 4096;
    double feasibility_tolerance = 1e-12;
    CancellationToken cancellation;
};

// Convex intersection of two OBB cells in half-space form. Normals are stored
// row-major and constraints mean dot(normal_i, q) <= offset_i.
class CspacePortal {
  public:
    CspacePortal() = default;

    static Result<std::optional<CspacePortal>> intersect(const CspaceObb& first, const CspaceObb& second,
                                                         const PortalIntersectionOptions& options = {});
    static Result<CspacePortal> create(std::vector<double> normals, std::vector<double> offsets,
                                       Configuration witness, CspaceAabb enclosing_aabb);

    std::size_t dimension() const noexcept { return witness_.size(); }
    std::size_t constraint_count() const noexcept { return offsets_.size(); }
    const std::vector<double>& normals() const noexcept { return normals_; }
    const std::vector<double>& offsets() const noexcept { return offsets_; }
    const Configuration& witness() const noexcept { return witness_; }
    const CspaceAabb& enclosing_aabb() const noexcept { return enclosing_aabb_; }

    bool valid() const noexcept;
    bool contains(std::span<const double> configuration, double tolerance = 0.0) const noexcept;

  private:
    friend class RegionDatabase;
    friend Result<void> save_region_database_directory(const RegionDatabase&, const std::filesystem::path&,
                                                       const SaveOptions&);
    friend Result<RegionDatabase> load_region_database_directory(const std::filesystem::path&);

    std::vector<double> normals_;
    std::vector<double> offsets_;
    Configuration witness_;
    CspaceAabb enclosing_aabb_;
};

struct PortalGeometry {
    RegionId left_region = 0;
    RegionId right_region = 0;
    CspacePortal intersection;

    bool valid() const noexcept {
        return left_region != 0 && right_region != 0 && left_region != right_region && intersection.valid();
    }
};

struct TrajectoryTubeGeometry {
    std::vector<RegionId> cell_ids;
    std::vector<RegionId> portal_ids;
    std::vector<Configuration> centerline;

    bool valid(std::size_t dimension) const noexcept;
};

using RegionGeometry = std::variant<CspaceAabb, CspaceObb, PortalGeometry, TrajectoryTubeGeometry,
                                    CspaceZonotope, CspaceTaylorRegion>;

RegionType region_type(const RegionGeometry& geometry) noexcept;
std::string region_type_name(RegionType type);

struct RegionRecord {
    RegionId id = 0;
    RegionGeometry geometry;
    std::size_t certificate_index = 0;
    ComponentId component = 0;
    LinkEnvelope dependency;
    std::string source;
};

struct CertifiedRegionInput {
    RegionGeometry geometry;
    Certificate certificate;
    LinkEnvelope dependency;
    std::string source;
};

struct RegionQueryOptions {
    bool include_portals = false;
    bool include_trajectory_tubes = false;
    double tolerance = 1e-12;
};

struct PortalDiscoveryOptions {
    std::size_t maximum_candidate_pairs = 1'000'000;
    std::size_t maximum_portals = 250'000;
    std::size_t maximum_iterations = 4096;
    double feasibility_tolerance = 1e-12;
    CancellationToken cancellation;
};

struct PortalDiscoveryStats {
    std::size_t candidate_pairs = 0;
    std::size_t aabb_rejections = 0;
    std::size_t feasibility_tests = 0;
    std::size_t portals_created = 0;
};

struct ObbAtlasBuildOptions {
    double initial_half_width = 1e-3;
    double maximum_half_width = 5e-2;
    double bridge_longitudinal_margin = 0.0;
    std::size_t nearest_bridge_neighbors = 2;
    std::size_t growth_iterations = 12;
    std::size_t maximum_samples = 100'000;
    std::size_t maximum_pair_evaluations = 1'000'000;
    std::size_t maximum_validations = 1'000'000;
    double obstacle_padding = 0.0;
    PortalDiscoveryOptions portal;
    CancellationToken cancellation;
};

struct ObbAtlasBuildStats {
    std::size_t unique_samples = 0;
    std::size_t point_regions = 0;
    std::size_t bridge_regions = 0;
    std::size_t rejected_candidates = 0;
    std::size_t validations = 0;
    std::size_t growth_attempts = 0;
    PortalDiscoveryStats portal;
};

class RegionDatabase {
  public:
    RegionDatabase() = default;

    std::size_t dimension() const noexcept { return dimension_; }
    const std::string& robot_digest() const noexcept { return robot_digest_; }
    const std::string& scene_digest() const noexcept { return scene_digest_; }
    const std::string& scene_version() const noexcept { return scene_version_; }
    const std::vector<RegionRecord>& records() const noexcept { return records_; }
    const std::vector<Certificate>& certificates() const noexcept { return certificates_; }
    const std::vector<std::vector<std::size_t>>& adjacency() const noexcept { return adjacency_; }

    Result<std::optional<RegionRecord>> region(RegionId id) const;
    Result<std::optional<Certificate>> certificate(const std::string& certificate_id) const;
    Result<std::vector<RegionRecord>> regions_at(std::span<const double> configuration,
                                                 const RegionQueryOptions& options = {}) const;
    bool contains(std::span<const double> configuration, const RegionQueryOptions& options = {}) const;
    Result<std::optional<RegionRecord>> nearest_region(std::span<const double> configuration,
                                                       const RegionQueryOptions& options = {}) const;
    Result<bool> connected(std::span<const double> first, std::span<const double> second) const;
    Result<void> verify_compatible(const SerialRobotModel& robot, const SceneSnapshot& scene) const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<RegionDatabase> load(const std::filesystem::path& directory);
    static Result<RegionDatabase> from_atlas(const SafeAtlas& atlas, std::string scene_version = {},
                                             const PortalDiscoveryOptions& options = {});
    static Result<RegionDatabase> from_corridor(const HipacCorridor& corridor, std::string scene_version,
                                                const PortalDiscoveryOptions& options = {});
    static Result<RegionDatabase> create(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                         std::vector<CertifiedRegionInput> regions,
                                         const PortalDiscoveryOptions& options = {});

  private:
    friend class ObbAtlasBuilder;
    friend Result<void> save_region_database_directory(const RegionDatabase&, const std::filesystem::path&,
                                                       const SaveOptions&);
    friend Result<RegionDatabase> load_region_database_directory(const std::filesystem::path&);

    std::size_t dimension_ = 0;
    std::string robot_digest_;
    std::string scene_digest_;
    std::string scene_version_;
    std::vector<RegionRecord> records_;
    std::vector<Certificate> certificates_;
    std::vector<std::vector<std::size_t>> adjacency_;
};

struct ObbAtlasBuildResult {
    RegionDatabase database;
    ObbAtlasBuildStats stats;
};

class ObbAtlasBuilder {
  public:
    Result<ObbAtlasBuildResult> build(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                      std::vector<Configuration> samples,
                                      const ObbAtlasBuildOptions& options = {}) const;
};

} // namespace rbfsafe

// Optimization adapters.

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

// Safe inverse kinematics.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rbfsafe {

enum class SafeIkStatus : std::uint8_t {
    SafeConnected = 0,
    SafeUnconnected = 1,
    SeedNotCertified = 2,
    NoSolution = 3,
};

struct SafeIkOptions {
    double position_tolerance = 1e-4;
    double orientation_tolerance = 1e-3;
    double orientation_weight = 0.25;
    double damping = 1e-3;
    double finite_difference_step = 1e-6;
    double maximum_step_norm = 0.25;
    double minimum_step_norm = 1e-12;
    std::size_t maximum_iterations = 128;
    std::size_t maximum_region_attempts = 256;
    std::size_t maximum_line_search_steps = 8;
    bool require_connectivity = true;
    CancellationToken cancellation;
};

struct SafeIkStats {
    std::size_t region_attempts = 0;
    std::size_t iterations = 0;
    std::size_t pose_evaluations = 0;
    std::size_t disconnected_solutions = 0;
};

struct SafeIkReport {
    SafeIkStatus status = SafeIkStatus::NoSolution;
    Configuration solution;
    RegionId region_id = 0;
    std::optional<Certificate> region_certificate;
    std::optional<AtlasRoute> connectivity_route;
    EvidenceLevel pose_evidence = EvidenceLevel::Unknown;
    double position_error = 0.0;
    double orientation_error = 0.0;
    SafeIkStats stats;
};

class SafeIkSolver {
  public:
    Result<SafeIkReport> solve(const SerialRobotModel& robot, const SceneSnapshot& scene,
                               const SafeAtlas& atlas, const Pose3d& target, std::span<const double> current,
                               const SafeIkOptions& options = {}) const;
};

} // namespace rbfsafe

// Certified planning utilities.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class CertifiedSamplingPolicy : std::uint8_t {
    UniformRegions = 0,
    VolumeWeighted = 1,
};

struct CertifiedSamplerOptions {
    CertifiedSamplingPolicy policy = CertifiedSamplingPolicy::VolumeWeighted;
    std::uint64_t seed = 42;
    std::size_t maximum_attempts = 64;
};

struct CertifiedSamplerStats {
    std::uint64_t samples_requested = 0;
    std::uint64_t samples_returned = 0;
    std::uint64_t near_samples_requested = 0;
    std::uint64_t rejected_attempts = 0;
};

// Deterministic, single-stream sampler over the certified AABB union. The
// class is intentionally not thread-safe; independent consumers should create
// independent streams with explicit seeds.
class CertifiedRegionSampler {
  public:
    CertifiedRegionSampler() = default;

    static Result<CertifiedRegionSampler> create(std::shared_ptr<const SafeAtlas> atlas,
                                                 const CertifiedSamplerOptions& options = {});

    bool valid() const noexcept { return static_cast<bool>(atlas_); }
    Result<Configuration> sample();
    Result<Configuration> sample_near(std::span<const double> reference, double maximum_distance);
    const CertifiedSamplerStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

  private:
    std::size_t choose_region(std::span<const std::size_t> candidates);
    Configuration sample_box(const CspaceAabb& box);

    std::shared_ptr<const SafeAtlas> atlas_;
    CertifiedSamplerOptions options_;
    std::vector<double> weights_;
    std::mt19937_64 engine_;
    CertifiedSamplerStats stats_;
};

using RoadmapNodeId = std::uint64_t;

enum class RoadmapNodeKind : std::uint8_t {
    RegionCenter = 0,
    PortalWitness = 1,
};

struct CertifiedRoadmapNode {
    RoadmapNodeId id = 0;
    RoadmapNodeKind kind = RoadmapNodeKind::RegionCenter;
    Configuration configuration;
    std::vector<RegionId> support_regions;
};

struct CertifiedRoadmapEdge {
    RoadmapNodeId first = 0;
    RoadmapNodeId second = 0;
    RegionId covering_region = 0;
};

struct CertifiedRoadmapOptions {
    std::size_t maximum_nodes = 1'000'000;
    std::size_t maximum_edges = 2'000'000;
    CancellationToken cancellation;
};

struct CertifiedRoadmapStats {
    std::size_t region_nodes = 0;
    std::size_t portal_nodes = 0;
    std::size_t edges = 0;
    std::size_t nonintersecting_adjacencies = 0;
};

class CertifiedRoadmap {
  public:
    CertifiedRoadmap() = default;

    std::size_t dimension() const noexcept { return dimension_; }
    const std::string& robot_digest() const noexcept { return robot_digest_; }
    const std::string& scene_digest() const noexcept { return scene_digest_; }
    const std::vector<CertifiedRoadmapNode>& nodes() const noexcept { return nodes_; }
    const std::vector<CertifiedRoadmapEdge>& edges() const noexcept { return edges_; }
    const std::vector<std::vector<std::size_t>>& adjacency() const noexcept { return adjacency_; }

    bool valid() const noexcept;
    Result<std::optional<CertifiedRoadmapNode>> nearest_node(std::span<const double> configuration) const;
    Result<void> verify_compatible(const SerialRobotModel& robot, const SceneSnapshot& scene) const;

  private:
    friend class CertifiedRoadmapBuilder;

    std::size_t dimension_ = 0;
    std::string robot_digest_;
    std::string scene_digest_;
    std::vector<CertifiedRoadmapNode> nodes_;
    std::vector<CertifiedRoadmapEdge> edges_;
    std::vector<std::vector<std::size_t>> adjacency_;
};

struct CertifiedRoadmapBuildResult {
    CertifiedRoadmap roadmap;
    CertifiedRoadmapStats stats;
};

class CertifiedRoadmapBuilder {
  public:
    Result<CertifiedRoadmapBuildResult> build(const SafeAtlas& atlas,
                                              const CertifiedRoadmapOptions& options = {}) const;
};

} // namespace rbfsafe

// Dynamic Atlas updates.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rbfsafe {

struct AtlasUpdateOptions {
    std::size_t maximum_repair_depth = 16;
    std::size_t maximum_repair_nodes = 250'000;
    std::size_t maximum_validations = 1'000'000;
    double minimum_normalized_width = 1e-3;
    double adjacency_tolerance = 1e-12;
    double obstacle_padding = 0.0;
    bool repair_invalidated_regions = true;
    CancellationToken cancellation;
};

struct AtlasUpdateStats {
    std::size_t regions_examined = 0;
    std::size_t certificates_inherited = 0;
    std::size_t regions_revalidated = 0;
    std::size_t regions_invalidated = 0;
    std::size_t repair_nodes_visited = 0;
    std::size_t repaired_regions = 0;
    std::size_t unresolved_repair_nodes = 0;
    std::size_t validations = 0;
};

struct AtlasUpdateResult {
    SafeAtlas atlas;
    SceneDelta delta;
    AtlasUpdateStats stats;
    std::vector<RegionId> retained_region_ids;
    std::vector<RegionId> invalidated_region_ids;
    std::vector<RegionId> repaired_region_ids;
};

class AtlasUpdater {
  public:
    AtlasUpdater();
    explicit AtlasUpdater(std::shared_ptr<const RegionValidator> validator);

    Result<AtlasUpdateResult> update(const SerialRobotModel& robot, const SceneSnapshot& previous_scene,
                                     const SceneSnapshot& next_scene, const SafeAtlas& previous_atlas,
                                     std::vector<Configuration> repair_samples = {},
                                     const AtlasUpdateOptions& options = {}) const;

  private:
    std::shared_ptr<const RegionValidator> validator_;
};

class AtlasVersionStore {
  public:
    static Result<AtlasVersionStore> create(const std::filesystem::path& directory,
                                            const SafeAtlas& initial_atlas);
    static Result<AtlasVersionStore> open(const std::filesystem::path& directory);

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& current_version_id() const noexcept { return current_version_id_; }
    const std::vector<AtlasVersionInfo>& versions() const noexcept { return versions_; }

    Result<SafeAtlas> load_current() const;
    Result<SafeAtlas> load_version(const std::string& version_id) const;
    Result<void> publish(const SafeAtlas& atlas);
    Result<void> rollback(const std::string& version_id);

  private:
    std::filesystem::path directory_;
    std::string current_version_id_;
    std::vector<AtlasVersionInfo> versions_;
};

} // namespace rbfsafe

// Runtime action shield.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rbfsafe {

enum class ShieldActionType : std::uint8_t {
    JointDelta = 0,
    EndEffectorPose = 1,
    Trajectory = 2,
};

enum class ShieldOutcome : std::uint8_t {
    Accept = 0,
    Repair = 1,
    Reject = 2,
};

enum class ShieldReason : std::uint8_t {
    Certified = 0,
    JointTargetRepaired = 1,
    SafeIkRoute = 2,
    TrajectoryRepaired = 3,
    CurrentStateNotCertified = 4,
    TargetNotCertified = 5,
    RepairDisabled = 6,
    RepairLimitExceeded = 7,
    NoSafeIkSolution = 8,
    Disconnected = 9,
};

struct JointDeltaAction {
    Configuration delta;
};

struct EndEffectorAction {
    Pose3d target;
};

struct TrajectoryAction {
    std::vector<Configuration> waypoints;
};

using ShieldAction = std::variant<JointDeltaAction, EndEffectorAction, TrajectoryAction>;

struct ShieldOptions {
    bool allow_repair = true;
    double maximum_waypoint_repair_distance = 0.25;
    double maximum_total_repair_distance = 1.0;
    std::size_t maximum_input_waypoints = 10'000;
    std::size_t maximum_output_waypoints = 100'000;
    std::size_t maximum_repair_region_tests = 10'000'000;
    TrajectoryAuditOptions audit;
    SafeIkOptions safe_ik;
    CancellationToken cancellation;
};

struct ShieldDecision {
    ShieldActionType action_type = ShieldActionType::JointDelta;
    ShieldOutcome outcome = ShieldOutcome::Reject;
    ShieldReason reason = ShieldReason::TargetNotCertified;
    std::string id;
    std::string action_digest;
    std::string robot_digest;
    std::string scene_digest;
    Configuration requested_target;
    std::vector<Configuration> output_trajectory;
    std::optional<TrajectoryAuditReport> audit;
    std::optional<Certificate> connectivity_certificate;
    double repair_distance = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct ShieldBatchOptions {
    std::size_t maximum_actions = 256;
    ShieldOptions action;
    CancellationToken cancellation;
};

struct ShieldBatchReport {
    std::vector<ShieldDecision> decisions;
    std::optional<std::size_t> selected_index;
};

struct ShieldTelemetrySnapshot {
    std::uint64_t total_actions = 0;
    std::uint64_t accepted_actions = 0;
    std::uint64_t repaired_actions = 0;
    std::uint64_t rejected_actions = 0;
    std::uint64_t joint_actions = 0;
    std::uint64_t end_effector_actions = 0;
    std::uint64_t trajectory_actions = 0;
    std::uint64_t repair_attempts = 0;
    std::uint64_t successful_repairs = 0;
    std::uint64_t input_waypoints = 0;
    std::uint64_t output_waypoints = 0;
    std::uint64_t batches = 0;
};

class RuntimeShield {
  public:
    Result<ShieldDecision> check_action(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                        const SafeAtlas& atlas, std::span<const double> current,
                                        const ShieldAction& action, const ShieldOptions& options = {});

    Result<ShieldBatchReport> check_actions(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const SafeAtlas& atlas, std::span<const double> current,
                                            std::span<const ShieldAction> actions,
                                            const ShieldBatchOptions& options = {});

    ShieldTelemetrySnapshot telemetry() const;
    void reset_telemetry();

  private:
    void record(const ShieldAction& action, const ShieldDecision& decision, bool repair_attempted);

    mutable std::mutex telemetry_mutex_;
    ShieldTelemetrySnapshot telemetry_;
};

enum class MonitorState : std::uint8_t {
    Inactive = 0,
    OnCertifiedPlan = 1,
    CertifiedDeviation = 2,
    UncertifiedState = 3,
};

struct RuntimeMonitorOptions {
    double tracking_tolerance = 0.05;
    std::size_t maximum_plan_waypoints = 100'000;
};

struct MonitorObservation {
    MonitorState state = MonitorState::Inactive;
    std::string decision_id;
    double timestamp = 0.0;
    double distance_to_plan = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct RuntimeMonitorStats {
    std::uint64_t observations = 0;
    std::uint64_t on_plan = 0;
    std::uint64_t certified_deviations = 0;
    std::uint64_t uncertified_states = 0;
};

class RuntimeShieldMonitor {
  public:
    explicit RuntimeShieldMonitor(std::shared_ptr<const SafeAtlas> atlas, RuntimeMonitorOptions options = {});

    Result<void> arm(const ShieldDecision& decision);
    void disarm();
    Result<MonitorObservation> observe(std::span<const double> configuration, double timestamp);
    RuntimeMonitorStats stats() const;

  private:
    std::shared_ptr<const SafeAtlas> atlas_;
    RuntimeMonitorOptions options_;
    mutable std::mutex mutex_;
    std::optional<ShieldDecision> active_;
    std::optional<double> last_timestamp_;
    RuntimeMonitorStats stats_;
};

std::string shield_action_type_name(ShieldActionType type);
std::string shield_outcome_name(ShieldOutcome outcome);
std::string shield_reason_name(ShieldReason reason);
std::string monitor_state_name(MonitorState state);
std::string shield_action_digest(const ShieldAction& action);

} // namespace rbfsafe

// Learning-policy safety gate.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class PolicySelectionMode : std::uint8_t {
    InputOrder = 0,
    HighestConfidence = 1,
    LowestUncertainty = 2,
};

enum class PolicyGateReason : std::uint8_t {
    ShieldAccepted = 0,
    ShieldRepaired = 1,
    ConfidenceBelowMinimum = 2,
    StateUncertaintyExceeded = 3,
    ActionUncertaintyExceeded = 4,
    ObservationTooOld = 5,
    InferenceLatencyExceeded = 6,
    ShieldRejected = 7,
};

enum class PolicyFeedbackLabel : std::uint8_t {
    SelectedAccepted = 0,
    SelectedRepaired = 1,
    EligibleNotSelected = 2,
    PolicyRejected = 3,
    ShieldRejected = 4,
};

struct PolicyProposalMetadata {
    std::string policy_id;
    std::string task_id;
    std::string episode_id;
    std::uint64_t sequence = 0;
    double confidence = 1.0;
    double state_uncertainty = 0.0;
    double action_uncertainty = 0.0;
    double observation_age_seconds = 0.0;
    double inference_latency_seconds = 0.0;
};

struct PolicyProposal {
    ShieldAction action;
    PolicyProposalMetadata metadata;
};

struct PolicyGateOptions {
    double minimum_confidence = 0.0;
    double maximum_state_uncertainty = std::numeric_limits<double>::max();
    double maximum_action_uncertainty = std::numeric_limits<double>::max();
    double maximum_observation_age_seconds = std::numeric_limits<double>::max();
    double maximum_inference_latency_seconds = std::numeric_limits<double>::max();
    std::size_t maximum_proposals = 256;
    PolicySelectionMode selection_mode = PolicySelectionMode::InputOrder;
    ShieldOptions shield;
};

struct PolicyGateDecision {
    std::string id;
    std::string proposal_id;
    PolicyProposalMetadata metadata;
    bool policy_eligible = false;
    bool selected = false;
    PolicyGateReason reason = PolicyGateReason::ConfidenceBelowMinimum;
    std::optional<ShieldDecision> shield_decision;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct PolicyFeedbackRecord {
    std::string id;
    std::string proposal_id;
    std::string policy_decision_id;
    std::string shield_decision_id;
    std::string robot_digest;
    std::string scene_digest;
    PolicyProposalMetadata metadata;
    PolicyFeedbackLabel label = PolicyFeedbackLabel::PolicyRejected;
    PolicyGateReason reason = PolicyGateReason::ConfidenceBelowMinimum;
    ShieldActionType action_type = ShieldActionType::JointDelta;
    Configuration requested_target;
    Configuration output_target;
    double repair_distance = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct PolicyBatchReport {
    std::vector<PolicyGateDecision> decisions;
    std::vector<PolicyFeedbackRecord> feedback;
    std::optional<std::size_t> selected_index;
};

struct PolicyTelemetrySnapshot {
    std::uint64_t batches = 0;
    std::uint64_t proposals = 0;
    std::uint64_t policy_rejections = 0;
    std::uint64_t shield_checks = 0;
    std::uint64_t shield_accepts = 0;
    std::uint64_t shield_repairs = 0;
    std::uint64_t shield_rejections = 0;
    std::uint64_t selected_accepts = 0;
    std::uint64_t selected_repairs = 0;
};

class LearningPolicySafetyGate {
  public:
    Result<PolicyBatchReport> check_proposals(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                              const SafeAtlas& atlas, std::span<const double> current,
                                              std::span<const PolicyProposal> proposals,
                                              const PolicyGateOptions& options = {});

    PolicyTelemetrySnapshot telemetry() const;
    void reset_telemetry();

  private:
    RuntimeShield shield_;
    mutable std::mutex telemetry_mutex_;
    PolicyTelemetrySnapshot telemetry_;
};

struct PolicyFeedbackQuery {
    std::string policy_id;
    std::string task_id;
    std::string episode_id;
    std::optional<PolicyFeedbackLabel> label;
    std::size_t maximum_results = 100'000;
};

struct PolicyFeedbackSummary {
    std::uint64_t records = 0;
    std::uint64_t selected_accepted = 0;
    std::uint64_t selected_repaired = 0;
    std::uint64_t eligible_not_selected = 0;
    std::uint64_t policy_rejected = 0;
    std::uint64_t shield_rejected = 0;
};

struct PolicyFeedbackLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 536'870'912ULL;
};

class PolicyFeedbackDatabase {
  public:
    PolicyFeedbackDatabase() = default;

    static Result<PolicyFeedbackDatabase> create(std::vector<PolicyFeedbackRecord> records);

    const std::vector<PolicyFeedbackRecord>& records() const noexcept { return records_; }
    Result<void> append(std::span<const PolicyFeedbackRecord> records,
                        std::size_t maximum_records = 1'000'000);
    Result<std::vector<PolicyFeedbackRecord>> query(const PolicyFeedbackQuery& query = {}) const;
    PolicyFeedbackSummary summary() const noexcept;
    bool valid() const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<PolicyFeedbackDatabase> load(const std::filesystem::path& directory,
                                               const PolicyFeedbackLoadOptions& options = {});

  private:
    std::vector<PolicyFeedbackRecord> records_;
};

std::string policy_selection_mode_name(PolicySelectionMode mode);
std::string policy_gate_reason_name(PolicyGateReason reason);
std::string policy_feedback_label_name(PolicyFeedbackLabel label);

} // namespace rbfsafe

// Policy calibration and lifecycle monitoring.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

struct PolicyCalibrationBinInput {
    double lower_confidence = 0.0;
    double upper_confidence = 1.0;
    double mean_confidence = 0.5;
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
};

struct PolicyCalibrationProfileInput {
    std::string policy_id;
    std::string policy_model_digest;
    std::string scope_id;
    std::string task_id;
    std::string dataset_digest;
    std::string method;
    std::string method_version;
    std::string outcome_definition;
    std::string state_uncertainty_unit;
    std::string action_uncertainty_unit;
    std::vector<PolicyCalibrationBinInput> bins;
};

struct PolicyCalibrationBin {
    double lower_confidence = 0.0;
    double upper_confidence = 1.0;
    double mean_confidence = 0.5;
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
    double observed_success_rate = 0.0;
    double lower_confidence_bound_95 = 0.0;
    double absolute_calibration_error = 0.0;
};

struct PolicyCalibrationLookup {
    std::string profile_id;
    std::size_t bin_index = 0;
    double raw_confidence = 0.0;
    double calibrated_confidence = 0.0;
    double conservative_confidence = 0.0;
    std::uint64_t samples = 0;
};

struct PolicyCalibrationLoadOptions {
    std::size_t maximum_bins = 4'096;
    std::uintmax_t maximum_payload_bytes = 1'048'576ULL;
};

class PolicyCalibrationProfile {
  public:
    PolicyCalibrationProfile() = default;

    static Result<PolicyCalibrationProfile> create(PolicyCalibrationProfileInput input);

    const std::string& id() const noexcept { return id_; }
    const std::string& policy_id() const noexcept { return policy_id_; }
    const std::string& policy_model_digest() const noexcept { return policy_model_digest_; }
    const std::string& scope_id() const noexcept { return scope_id_; }
    const std::string& task_id() const noexcept { return task_id_; }
    const std::string& dataset_digest() const noexcept { return dataset_digest_; }
    const std::string& method() const noexcept { return method_; }
    const std::string& method_version() const noexcept { return method_version_; }
    const std::string& outcome_definition() const noexcept { return outcome_definition_; }
    const std::string& state_uncertainty_unit() const noexcept { return state_uncertainty_unit_; }
    const std::string& action_uncertainty_unit() const noexcept { return action_uncertainty_unit_; }
    const std::vector<PolicyCalibrationBin>& bins() const noexcept { return bins_; }
    std::uint64_t sample_count() const noexcept { return sample_count_; }
    double expected_calibration_error() const noexcept { return expected_calibration_error_; }
    double maximum_calibration_error() const noexcept { return maximum_calibration_error_; }

    bool valid() const;
    Result<PolicyCalibrationLookup> lookup(double raw_confidence) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<PolicyCalibrationProfile> load(const std::filesystem::path& path,
                                                 const PolicyCalibrationLoadOptions& options = {});

  private:
    std::string id_;
    std::string policy_id_;
    std::string policy_model_digest_;
    std::string scope_id_;
    std::string task_id_;
    std::string dataset_digest_;
    std::string method_;
    std::string method_version_;
    std::string outcome_definition_;
    std::string state_uncertainty_unit_;
    std::string action_uncertainty_unit_;
    std::vector<PolicyCalibrationBin> bins_;
    std::uint64_t sample_count_ = 0;
    double expected_calibration_error_ = 0.0;
    double maximum_calibration_error_ = 0.0;
};

enum class PolicyCalibrationDriftStatus : std::uint8_t {
    InsufficientData = 0,
    Stable = 1,
    DriftDetected = 2,
};

enum class PolicyCalibrationDriftReason : std::uint8_t {
    InsufficientTotalSamples = 0,
    InsufficientBinSamples = 1,
    ConfidenceDistributionShift = 2,
    ExpectedCalibrationErrorExceeded = 3,
    OverallSuccessRateDropExceeded = 4,
    BinSuccessRateDropExceeded = 5,
};

struct PolicyCalibrationWindowBinInput {
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
};

struct PolicyCalibrationWindowInput {
    std::string window_id;
    std::uint64_t sequence = 0;
    std::string source_digest;
    std::vector<PolicyCalibrationWindowBinInput> bins;
};

struct PolicyCalibrationDriftOptions {
    std::uint64_t minimum_total_samples = 1'000;
    std::uint64_t minimum_bin_samples = 30;
    double maximum_total_variation_distance = 0.1;
    double maximum_expected_calibration_error = 0.1;
    double maximum_overall_success_rate_drop = 0.1;
    double maximum_bin_success_rate_drop = 0.2;
};

struct PolicyCalibrationWindowBin {
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
    double baseline_fraction = 0.0;
    double observed_fraction = 0.0;
    double baseline_success_rate = 0.0;
    bool outcome_rate_available = false;
    double observed_success_rate = 0.0;
    double success_rate_drop = 0.0;
    double absolute_calibration_error = 0.0;
};

struct PolicyCalibrationDriftReport {
    std::string id;
    std::string profile_id;
    std::string window_id;
    std::uint64_t window_sequence = 0;
    std::string source_digest;
    PolicyCalibrationDriftOptions options;
    PolicyCalibrationDriftStatus status = PolicyCalibrationDriftStatus::InsufficientData;
    std::vector<PolicyCalibrationDriftReason> reasons;
    std::uint64_t sample_count = 0;
    double total_variation_distance = 0.0;
    double baseline_success_rate = 0.0;
    double observed_success_rate = 0.0;
    double overall_success_rate_drop = 0.0;
    double expected_calibration_error = 0.0;
    double maximum_calibration_error = 0.0;
    double maximum_bin_success_rate_drop = 0.0;
    std::vector<PolicyCalibrationWindowBin> bins;
};

Result<PolicyCalibrationDriftReport>
assess_policy_calibration_drift(const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
                                const PolicyCalibrationDriftOptions& options = {});

enum class PolicyCalibrationLifecycleState : std::uint8_t {
    PendingReview = 0,
    Active = 1,
    Quarantined = 2,
    Retired = 3,
};

enum class PolicyCalibrationLifecycleEventType : std::uint8_t {
    Registered = 0,
    DriftAssessed = 1,
    StateTransition = 2,
};

struct PolicyCalibrationLifecycleEvent {
    std::string id;
    std::string parent_id;
    std::uint64_t sequence = 0;
    PolicyCalibrationLifecycleEventType type = PolicyCalibrationLifecycleEventType::Registered;
    PolicyCalibrationLifecycleState previous_state = PolicyCalibrationLifecycleState::PendingReview;
    PolicyCalibrationLifecycleState current_state = PolicyCalibrationLifecycleState::PendingReview;
    std::string report_id;
    std::string detail;
};

struct PolicyCalibrationLifecycleSummary {
    std::uint64_t assessments = 0;
    std::uint64_t stable = 0;
    std::uint64_t insufficient_data = 0;
    std::uint64_t drift_detected = 0;
    std::uint64_t transitions = 0;
};

struct PolicyCalibrationLifecycleLoadOptions {
    std::size_t maximum_reports = 100'000;
    std::size_t maximum_events = 1'000'000;
    std::size_t maximum_total_bins = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class PolicyCalibrationLifecycle;
Result<void> save_policy_calibration_lifecycle(const PolicyCalibrationLifecycle& lifecycle,
                                               const PolicyCalibrationProfile& profile,
                                               const std::filesystem::path& path, const SaveOptions& options);
Result<PolicyCalibrationLifecycle>
load_policy_calibration_lifecycle(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                                  const PolicyCalibrationLifecycleLoadOptions& options);

class PolicyCalibrationLifecycle {
  public:
    PolicyCalibrationLifecycle() = default;

    static Result<PolicyCalibrationLifecycle> create(const PolicyCalibrationProfile& profile);

    const std::string& profile_id() const noexcept { return profile_id_; }
    PolicyCalibrationLifecycleState state() const noexcept { return state_; }
    std::uint64_t generation() const noexcept { return generation_; }
    const std::string& current_event_id() const noexcept { return current_event_id_; }
    const std::string& latest_report_id() const noexcept { return latest_report_id_; }
    const std::vector<PolicyCalibrationDriftReport>& reports() const noexcept { return reports_; }
    const std::vector<PolicyCalibrationLifecycleEvent>& events() const noexcept { return events_; }

    Result<PolicyCalibrationDriftReport>
    assess(const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
           std::string_view expected_current_event_id, const PolicyCalibrationDriftOptions& options = {},
           std::size_t maximum_reports = 100'000, std::size_t maximum_events = 1'000'000);
    Result<PolicyCalibrationLifecycleEvent> transition(const PolicyCalibrationProfile& profile,
                                                       std::string_view expected_current_event_id,
                                                       PolicyCalibrationLifecycleState target_state,
                                                       std::string detail,
                                                       std::size_t maximum_events = 1'000'000);
    Result<PolicyCalibrationDriftReport> latest_report() const;
    Result<PolicyCalibrationDriftReport> report(std::string_view report_id) const;
    PolicyCalibrationLifecycleSummary summary() const noexcept;
    bool deployment_ready() const noexcept;
    bool valid(const PolicyCalibrationProfile& profile) const;

    Result<void> save(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                      const SaveOptions& options = {}) const;
    static Result<PolicyCalibrationLifecycle> load(const std::filesystem::path& path,
                                                   const PolicyCalibrationProfile& profile,
                                                   const PolicyCalibrationLifecycleLoadOptions& options = {});

  private:
    friend Result<void> save_policy_calibration_lifecycle(const PolicyCalibrationLifecycle&,
                                                          const PolicyCalibrationProfile&,
                                                          const std::filesystem::path&, const SaveOptions&);
    friend Result<PolicyCalibrationLifecycle>
    load_policy_calibration_lifecycle(const std::filesystem::path&, const PolicyCalibrationProfile&,
                                      const PolicyCalibrationLifecycleLoadOptions&);

    std::string profile_id_;
    PolicyCalibrationLifecycleState state_ = PolicyCalibrationLifecycleState::PendingReview;
    std::uint64_t generation_ = 0;
    std::string current_event_id_;
    std::string latest_report_id_;
    std::vector<PolicyCalibrationDriftReport> reports_;
    std::vector<PolicyCalibrationLifecycleEvent> events_;
};

struct CalibratedPolicyGateOptions {
    std::uint64_t minimum_total_samples = 1'000;
    std::uint64_t minimum_bin_samples = 30;
    double maximum_expected_calibration_error = 0.1;
    double maximum_bin_calibration_error = 0.2;
    PolicyGateOptions policy;
};

struct CalibratedPolicyApplication {
    std::string id;
    std::string profile_id;
    PolicyProposalMetadata raw_metadata;
    PolicyProposalMetadata effective_metadata;
    std::size_t bin_index = 0;
    std::uint64_t bin_samples = 0;
    double calibrated_confidence = 0.0;
    double conservative_confidence = 0.0;
};

struct CalibratedPolicyBatchReport {
    std::string profile_id;
    std::string lifecycle_event_id;
    std::vector<CalibratedPolicyApplication> applications;
    PolicyBatchReport policy_report;
};

class CalibratedPolicySafetyGate {
  public:
    Result<CalibratedPolicyBatchReport>
    check_proposals(const PolicyCalibrationProfile& profile, std::string_view expected_scope_id,
                    std::string_view expected_policy_model_digest, const SerialRobotModel& robot,
                    const SceneSnapshot& scene, const SafeAtlas& atlas, std::span<const double> current,
                    std::span<const PolicyProposal> proposals,
                    const CalibratedPolicyGateOptions& options = {});

    Result<CalibratedPolicyBatchReport> check_proposals_guarded(
        const PolicyCalibrationProfile& profile, const PolicyCalibrationLifecycle& lifecycle,
        std::string_view expected_lifecycle_event_id, std::string_view expected_scope_id,
        std::string_view expected_policy_model_digest, const SerialRobotModel& robot,
        const SceneSnapshot& scene, const SafeAtlas& atlas, std::span<const double> current,
        std::span<const PolicyProposal> proposals, const CalibratedPolicyGateOptions& options = {});

    PolicyTelemetrySnapshot telemetry() const { return gate_.telemetry(); }
    void reset_telemetry() { gate_.reset_telemetry(); }

  private:
    LearningPolicySafetyGate gate_;
};

std::string policy_calibration_drift_status_name(PolicyCalibrationDriftStatus status);
std::string policy_calibration_drift_reason_name(PolicyCalibrationDriftReason reason);
std::string policy_calibration_lifecycle_state_name(PolicyCalibrationLifecycleState state);
std::string policy_calibration_lifecycle_event_type_name(PolicyCalibrationLifecycleEventType type);

} // namespace rbfsafe
