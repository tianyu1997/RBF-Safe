#pragma once

#include <rbfsafe/certificate.h>
#include <rbfsafe/lect.h>
#include <rbfsafe/model.h>
#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rbfsafe {

namespace detail {
class RegionQueryIndex;
}

class CancellationToken {
  public:
    CancellationToken() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const noexcept { cancelled_->store(true, std::memory_order_relaxed); }
    bool cancelled() const noexcept { return cancelled_->load(std::memory_order_relaxed); }

  private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

struct BuildOptions {
    std::size_t maximum_depth = 24;
    std::size_t maximum_nodes = 1'000'000;
    double minimum_normalized_width = 1e-3;
    double adjacency_tolerance = 1e-12;
    double obstacle_padding = 0.0;
    int threads = 1;
    CancellationToken cancellation;
};

struct BuildStats {
    std::size_t input_samples = 0;
    std::size_t unique_samples = 0;
    std::size_t nodes_visited = 0;
    std::size_t certified_nodes = 0;
    std::size_t unresolved_nodes = 0;
    std::size_t merged_regions = 0;
};

struct SafeRegion {
    RegionId id = 0;
    CspaceAabb bounds;
    std::size_t certificate_index = 0;
    ComponentId component = 0;
    LectNodeKey source_node;
};

struct SaveOptions {
    bool overwrite = false;
};

struct AtlasRoute {
    std::vector<Configuration> waypoints;
    std::vector<RegionId> region_sequence;
    Certificate certificate;
};

class SafeAtlas {
  public:
    SafeAtlas() = default;

    std::size_t dimension() const noexcept { return dimension_; }
    const std::string& robot_digest() const noexcept { return robot_digest_; }
    const std::string& scene_digest() const noexcept { return scene_digest_; }
    const std::vector<SafeRegion>& regions() const noexcept { return regions_; }
    const std::vector<Certificate>& certificates() const noexcept { return certificates_; }
    const std::vector<std::vector<std::size_t>>& adjacency() const noexcept { return adjacency_; }
    const LectSnapshot& lect() const noexcept { return lect_; }

    Result<std::vector<SafeRegion>> regions_at(std::span<const double> configuration) const;
    Result<std::vector<SafeRegion>> regions_at(const Configuration& configuration) const {
        return regions_at(std::span<const double>(configuration));
    }
    bool contains(std::span<const double> configuration) const;
    bool contains(const Configuration& configuration) const {
        return contains(std::span<const double>(configuration));
    }
    Result<std::optional<SafeRegion>> nearest_region(std::span<const double> configuration) const;
    Result<bool> connected(std::span<const double> first, std::span<const double> second) const;
    Result<std::optional<AtlasRoute>> route(std::span<const double> first,
                                            std::span<const double> second) const;
    Result<void> verify_compatible(const SerialRobotModel& robot, const SceneSnapshot& scene) const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<SafeAtlas> load(const std::filesystem::path& directory);

  private:
    friend class AtlasBuilder;
    friend Result<void> save_atlas_directory(const SafeAtlas&, const std::filesystem::path&,
                                             const SaveOptions&);
    friend Result<SafeAtlas> load_atlas_directory(const std::filesystem::path&);

    void rebuild_query_index();

    std::size_t dimension_ = 0;
    std::string robot_digest_;
    std::string scene_digest_;
    LectSnapshot lect_;
    std::vector<SafeRegion> regions_;
    std::vector<Certificate> certificates_;
    std::vector<std::vector<std::size_t>> adjacency_;
    std::shared_ptr<const detail::RegionQueryIndex> query_index_;
};

struct AtlasBuildResult {
    SafeAtlas atlas;
    BuildStats stats;
};

class AtlasBuilder {
  public:
    AtlasBuilder();
    explicit AtlasBuilder(std::shared_ptr<const RegionValidator> validator);

    Result<AtlasBuildResult> build(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                   std::vector<Configuration> samples,
                                   const BuildOptions& options = {}) const;

  private:
    std::shared_ptr<const RegionValidator> validator_;
};

} // namespace rbfsafe
