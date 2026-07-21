# OBB corridors, portals, and HiPaC

RBF-Safe v0.4 adds a planner-independent certified corridor layer. It turns a
candidate piecewise-linear configuration-space path into a sequence of convex
OBB cells, connects consecutive cells with witness portals, and can recover a
deterministic route through the certified union.

## OBB representation

`CspaceObb` contains a center, a row-major orthonormal basis, and one
non-negative half-width per basis axis. For dimension `d`, basis entries
`basis[i*d + j]` are coordinate `j` of local axis `i`. Public APIs use only
standard containers; Eigen is not part of the ABI.

`ObbGenerator::segment_tube(first, second, lateral, margin)` chooses the first
axis along the segment and completes a deterministic orthonormal basis using
modified Gram-Schmidt against canonical axes. The longitudinal half-width is
half the segment length plus `margin`; all other half-widths are `lateral`.

## Conservative certification

`ObbRegionValidator` computes the axis-aligned C-space enclosure of the OBB and
passes that enclosure to `IFK-AA + LinkIAABB`. An OBB is certified only if the
larger enclosure is certified. Therefore every OBB point is covered by the
proof, but orientation correlations are not yet exploited by the validator.

This design is intentionally conservative. A rejected OBB is `Undetermined`,
not collision-proven. OBBs whose conservative enclosure exceeds a joint limit
are rejected even when the exact rotated OBB might fit.

`ObbGrower::grow` exposes the bounded lateral-growth operation independently
of HiPaC. It first validates the requested initial tube, tests the configured
cap, and then performs bounded binary refinement when the cap is unresolved.
`ObbGrowthResult::certified` is false when even the initial tube is
undetermined; validation and cancellation budgets never turn that result into
a certificate.

## HiPaC construction

`HipacCorridorBuilder::build(robot, scene, path, options)` performs these
deterministic steps for every input segment:

1. Build a segment OBB at `minimum_lateral_half_width`.
2. If certification fails, bisect the segment recursively up to
   `maximum_subdivision_depth`.
3. If certification succeeds, grow the lateral width toward
   `maximum_lateral_half_width` with bounded binary search.
4. Record every failed leaf as an explicit `TrajectoryInterval`.
5. Join consecutive cells only when they share a waypoint contained in both
   OBBs. That configuration becomes a witness `PortalRegion`.
6. Assign connected components and issue subject-bound region and connectivity
   certificates.

| `HipacOptions` field | Default | Meaning |
|---|---:|---|
| `minimum_lateral_half_width` | `1e-3` | Required starting tube radius |
| `maximum_lateral_half_width` | `5e-2` | Growth cap |
| `longitudinal_margin` | `0` | Extra extent beyond segment endpoints |
| `growth_iterations` | `12` | Binary growth iterations after the cap test |
| `maximum_subdivision_depth` | `8` | Recursive segment split limit |
| `maximum_validations` | `100,000` | Hard OBB-validation budget |
| `portal_tolerance` | `1e-12` | Shared-witness comparison tolerance |
| `obstacle_padding` | `0` | Extra link radius used by certification |
| `cancellation` | fresh token | Cooperative cancellation source |

The report status is `CERTIFIED` when every input segment is covered,
`PARTIAL` when certified cells and uncovered intervals both exist, and
`INVALID` when no cell is certified. `INVALID` still means "not certified",
not "collision proved".

## Portal and route guarantees

A v0.4 portal is a zero-volume witness portal, not a maximal intersection of
two arbitrary OBBs. The witness belongs to both adjacent certified convex
cells. `HipacCorridor::route(q1, q2)` uses a deterministic shortest-hop search
and returns:

- the query endpoints and portal witnesses as waypoints;
- the traversed region and portal IDs; and
- a `CertifiedConnectivity` certificate bound to the complete route subject.

Every returned line segment lies inside one certified convex OBB. The route is
therefore geometrically certified for the bound robot and scene snapshot, but
it has no timing, dynamics, controller, self-collision, or execution claim.

## C++ example

```cpp
#include <rbfsafe/corridor.h>

std::vector<rbfsafe::Configuration> candidate{
    {-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
auto report = rbfsafe::HipacCorridorBuilder{}.build(robot, scene, candidate);
if (report && report.value().status == rbfsafe::HipacBuildStatus::Certified) {
    auto route = report.value().corridor.route(candidate.front(), candidate.back());
}
```

Link `RBFSafe::corridor` or the aggregate `RBFSafe::rbfsafe` target.

Both installed `rbfsafe-inspect` implementations auto-detect a corridor
manifest and can report metadata or run a membership query.

## Python example

```python
report = rbfsafe.HipacCorridorBuilder().build(robot, scene, candidate)
if report.status == rbfsafe.HipacBuildStatus.CERTIFIED:
    route = report.corridor.route(candidate[0], candidate[-1])
    report.corridor.save("corridor")
```

## v0.4 limits

- Portal discovery is limited to consecutive cells produced from the input
  path; arbitrary OBB intersection and graph augmentation are not attempted.
- The validator certifies an AABB enclosure and does not use a correlated
  zonotope or Taylor-model proof.
- HiPaC certifies and covers an existing path; it is not a sampling planner,
  optimizer, shortcut method, or collision-check fallback.
- Partial corridors may contain multiple disconnected components. Route
  queries never bridge an uncovered gap.
