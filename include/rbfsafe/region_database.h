#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/corridor.h>
#include <rbfsafe/higher_order.h>

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
