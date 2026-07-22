#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/planning.h>
#include <rbfsafe/result.h>
#include <rbfsafe/trajectory.h>

#include <ompl/base/Planner.h>
#include <ompl/base/SpaceInformation.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class RegionSamplingPolicy : std::uint8_t {
    UniformRegions = 0,
    VolumeWeighted = 1,
};

enum class OmplSamplingMode : std::uint8_t {
    OmplDefault = 0,
    AtlasGuided = 1,
};

struct OmplAdapterOptions {
    OmplSamplingMode sampling_mode = OmplSamplingMode::AtlasGuided;
    RegionSamplingPolicy sampling_policy = RegionSamplingPolicy::VolumeWeighted;
    std::uint64_t seed = 42;
    std::size_t maximum_sampling_attempts = 64;
    std::size_t maximum_region_tests = 10'000'000;
};

struct OmplAdapterStats {
    std::uint64_t state_queries = 0;
    std::uint64_t certified_states = 0;
    std::uint64_t motion_queries = 0;
    std::uint64_t certified_motions = 0;
    std::uint64_t samples_requested = 0;
    std::uint64_t certified_samples = 0;
    std::uint64_t sampling_fallbacks = 0;
    std::uint64_t audit_failures = 0;
};

namespace detail {
struct OmplAdapterState;
}

class OmplAdapter {
  public:
    OmplAdapter() = default;

    static Result<OmplAdapter> install(const ompl::base::SpaceInformationPtr& space_information,
                                       std::shared_ptr<const SafeAtlas> atlas,
                                       const OmplAdapterOptions& options = {});

    bool valid() const noexcept { return static_cast<bool>(state_); }
    OmplAdapterStats stats() const noexcept;
    void reset_stats() const noexcept;

  private:
    explicit OmplAdapter(std::shared_ptr<detail::OmplAdapterState> state) : state_(std::move(state)) {}

    std::shared_ptr<detail::OmplAdapterState> state_;
};

Result<std::shared_ptr<ompl::base::RealVectorStateSpace>> make_ompl_state_space(const SafeAtlas& atlas);

enum class OmplPlannerKind : std::uint8_t {
    Rrt = 0,
    RrtStar = 1,
    Prm = 2,
    BitStar = 3,
};

enum class OmplPlanStatus : std::uint8_t {
    CertifiedExactSolution = 0,
    ApproximateSolution = 1,
    Timeout = 2,
    InvalidStart = 3,
    InvalidGoal = 4,
    UncertifiedSolution = 5,
};

struct OmplPlannerOptions {
    OmplPlannerKind planner = OmplPlannerKind::Rrt;
    double maximum_planning_time = 1.0;
    double goal_tolerance = 1e-6;
    double range = 0.0;
    bool seed_prm_from_atlas = true;
    std::size_t maximum_solution_states = 1'000'000;
    CertifiedRoadmapOptions roadmap;
    OmplAdapterOptions adapter;
    CancellationToken cancellation;
};

struct OmplPlanningStats {
    double planning_time = 0.0;
    std::size_t planner_vertices = 0;
    std::size_t planner_edges = 0;
    std::size_t solution_states = 0;
    double solution_length = 0.0;
    bool exact_solution = false;
    std::size_t seeded_roadmap_nodes = 0;
    std::size_t seeded_roadmap_edges = 0;
    OmplAdapterStats adapter;
};

struct OmplPlanResult {
    OmplPlanStatus status = OmplPlanStatus::Timeout;
    std::vector<Configuration> path;
    std::optional<TrajectoryAuditReport> audit;
    OmplPlanningStats stats;
};

// The optional roadmap is accepted only by PRM. Callers supplying one must
// verify its robot/scene identity before invoking the factory; every imported
// node and edge is also rechecked through the installed SpaceInformation.
Result<ompl::base::PlannerPtr> make_ompl_planner(const ompl::base::SpaceInformationPtr& space_information,
                                                 OmplPlannerKind kind, double range = 0.0,
                                                 const CertifiedRoadmap* roadmap = nullptr);

class OmplPlanner {
  public:
    Result<OmplPlanResult> solve(std::shared_ptr<const SafeAtlas> atlas, std::span<const double> start,
                                 std::span<const double> goal, const OmplPlannerOptions& options = {}) const;
};

} // namespace rbfsafe
