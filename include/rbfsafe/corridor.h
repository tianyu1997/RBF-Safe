#pragma once

#include <rbfsafe/certificate.h>
#include <rbfsafe/geometry.h>
#include <rbfsafe/trajectory.h>
#include <rbfsafe/types.h>

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
