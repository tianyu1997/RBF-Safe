#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/result.h>
#include <rbfsafe/scene_delta.h>

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
