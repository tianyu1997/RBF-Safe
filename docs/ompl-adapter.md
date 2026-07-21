# OMPL adapter

RBF-Safe v0.3 provides an optional C++ adapter for OMPL real-vector planning.
It is planner-agnostic: RBF-Safe supplies validity and sampling primitives but
does not wrap, fork, or reimplement RRT, RRT*, PRM, BIT*, or other planners.

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

The installed state-space sampler selects a certified region and samples
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

## Options and statistics

`OmplAdapterOptions` provides the sampling policy, seed, bounded sampling
attempts, and maximum Atlas region tests per motion. Both resource limits must
be positive.

`OmplAdapter::stats()` returns atomic snapshots of state and motion queries,
certified results, requested and certified samples, sampling fallbacks, and
audit failures. `reset_stats()` clears the counters. Statistics are diagnostic
metadata and do not change certificate evidence.

## Limits

- Only bounded `RealVectorStateSpace` is supported; SO(2), compound, control,
  constrained, and task-space state spaces are outside v0.3.
- Joint wraparound is not inferred; real-vector interpolation must match the
  linear configuration convention used by the Atlas and trajectory auditor.
- Region sampling guides existing OMPL planners but does not provide a planner,
  roadmap cache, objective, path simplifier, or MoveIt plugin.
- The robot, scene, self-collision, static-world, and execution limitations in
  the safety model remain unchanged.
