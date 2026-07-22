#pragma once

#include <rbfsafe/atlas.h>

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
