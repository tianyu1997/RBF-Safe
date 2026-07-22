# OMPL adapter

RBF-Safe provides an optional C++ adapter for OMPL real-vector planning. The
low-level v0.3 interface remains planner-agnostic. Version 0.8 adds a bounded
high-level helper that configures the upstream OMPL implementations of RRT,
RRT*, PRM, and BIT*, then independently audits the returned path. RBF-Safe does
not fork or reimplement those planners.

## Certified-only semantics

The adapter maps evidence conservatively:

| Atlas result | OMPL result |
|---|---|
| configuration belongs to a certified region | valid state |
| configuration is not covered | invalid state |
| complete linear edge is covered by the certified union | valid motion |
| edge has any uncovered parameter interval | invalid motion |
| audit error or resource-limit failure | invalid motion and incremented failure counter |

An OMPL `false` therefore means "not certified by this Atlas", not "collision
proved". v0.3 deliberately has no fallback collision checker. Planning is
restricted to the certified union and is not probabilistically complete in the
robot's full free space.

Motion validation calls the continuous v0.2 `TrajectoryAuditor` for each edge.
It does not depend on OMPL's discrete state-validity resolution. The
`lastValid` overload reports the certified boundary before the first uncovered
interval.

## Build and use

Build with an installed OMPL package:

```bash
cmake -S . -B build -DRBFSAFE_BUILD_OMPL=ON
cmake --build build --config Release
```

Installed consumers request the optional component and link its target:

```cmake
find_package(RBFSafe CONFIG REQUIRED COMPONENTS ompl)
target_link_libraries(my_planner PRIVATE RBFSafe::ompl)
```

Install the adapter before setting up the OMPL planning context:

```cpp
#include <rbfsafe/ompl.h>

auto atlas = std::make_shared<rbfsafe::SafeAtlas>(
    rbfsafe::SafeAtlas::load("atlas").value());
auto space = rbfsafe::make_ompl_state_space(*atlas).value();
auto si = std::make_shared<ompl::base::SpaceInformation>(space);

rbfsafe::OmplAdapterOptions options;
options.seed = 42;
auto adapter = rbfsafe::OmplAdapter::install(si, atlas, options);
if (!adapter) {
    // Inspect adapter.error().
}
si->setup();
```

The supplied space must be `RealVectorStateSpace`, match the Atlas dimension,
and use the exact Atlas root bounds. `make_ompl_state_space` establishes those
invariants automatically. The adapter retains a shared immutable Atlas, so it
is safe for OMPL planners that perform concurrent const validity queries.

## Certified-region sampler

With `OmplSamplingMode::AtlasGuided` (the default), the installed state-space
sampler selects a certified region and samples
uniformly inside its AABB. The default selection weight is the region's volume
normalized by the Atlas root; `UniformRegions` is available when equal region
frequency is preferred. Overlapping regions are not de-overlapped, so overlap
can receive additional probability mass without affecting validity.

Near samples are restricted to the requested OMPL distance when a certified
point exists. Gaussian sampling uses bounded rejection followed by a certified
near sample. If no certified candidate can be produced, the sampler returns
the near state as an ordinary proposal; the installed validity checker still
rejects it when necessary, and `sampling_fallbacks` records the event.

`seed` makes sampler streams reproducible for a single-threaded planner with a
stable OMPL version and allocation order. Parallel planner scheduling is not a
cross-run determinism guarantee.

Set `sampling_mode` to `OmplDefault` to retain OMPL's ordinary state-space
sampler while keeping RBF-Safe state and continuous motion validation. This
supports a controlled OMPL+RBF versus RBF-guided comparison. Default samples
outside the certified union are rejected by the validity checker.

## RRT, RRT*, PRM, and BIT* helper

`OmplPlanner::solve(atlas, start, goal, options)` validates dimensions and
coverage, installs the adapter, constructs the selected upstream planner, runs
it under a wall-time/cancellation condition, extracts `PathGeometric`, and
runs a complete `TrajectoryAuditor` pass. Normal outcomes are explicit:

- `CertifiedExactSolution`: exact OMPL goal solution and fully certified path;
- `ApproximateSolution`: certified path to an approximate OMPL goal only;
- `Timeout`: no solution was returned within the bound;
- `InvalidStart` or `InvalidGoal`: endpoint lacks Atlas coverage; and
- `UncertifiedSolution`: a returned path failed the independent audit.

Resource or API failures remain `Result<T>` errors. `maximum_solution_states`
bounds extraction. Statistics report time, planner-data vertices/edges, path
length/state count, exactness, seed-roadmap size, and adapter counters.

For PRM, `seed_prm_from_atlas=true` imports a `CertifiedRoadmap` through
OMPL's public `PlannerData` constructor. Every node and edge is rechecked by
the installed Atlas validators during import. RRT and RRT* accept the helper's
optional `range`; the version-neutral helper intentionally leaves PRM and BIT*
range tuning to native OMPL configuration.

```cpp
rbfsafe::OmplPlannerOptions options;
options.planner = rbfsafe::OmplPlannerKind::BitStar;
options.maximum_planning_time = 2.0;
auto result = rbfsafe::OmplPlanner{}.solve(atlas, start, goal, options);
if (result && result.value().status ==
                  rbfsafe::OmplPlanStatus::CertifiedExactSolution) {
    use(result.value().path);
}
```

## Options and statistics

`OmplAdapterOptions` provides the guided/default sampling mode, region policy,
seed, bounded sampling attempts, and maximum Atlas region tests per motion.
Both resource limits must be positive.

`OmplAdapter::stats()` returns atomic snapshots of state and motion queries,
certified results, requested and certified samples, sampling fallbacks, and
audit failures. `reset_stats()` clears the counters. Statistics are diagnostic
metadata and do not change certificate evidence.

## Limits

- Only bounded `RealVectorStateSpace` is supported; SO(2), compound, control,
  constrained, and task-space state spaces are outside the current adapter.
- Joint wraparound is not inferred; real-vector interpolation must match the
  linear configuration convention used by the Atlas and trajectory auditor.
- The helper selects upstream planners; it does not change their completeness,
  optimality, objectives, path simplification, or termination semantics.
- `CertifiedRoadmap` is an in-memory seed, not a persistent OMPL roadmap cache.
- The robot, scene, self-collision, static-world, and execution limitations in
  the safety model remain unchanged.
