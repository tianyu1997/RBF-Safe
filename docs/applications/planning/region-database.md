# Unified region and certificate database

RBF-Safe v0.7 adds `RBFSafe::regions`, a database layer for certified region
geometries that do not all fit the rectangular `SafeAtlas` schema. It does not
replace `SafeAtlas` or `HipacCorridor`: both remain focused, independently
versioned producers that can be imported into the common query model.

## Record types

`RegionRecord` stores a deterministic nonzero ID, one geometry, a certificate
index, a connected-component label, an optional per-link workspace envelope,
and a provenance string. `RegionType` distinguishes:

- `Aabb`: a certified `CspaceAabb`, normally imported from `SafeAtlas`;
- `Obb`: a certified oriented convex cell;
- `Portal`: the complete half-space intersection of two AABB/OBB cells, with
  a verified shared witness;
- `TrajectoryTube`: an ordered cell/portal chain and its centerline;
- `Zonotope`: correlated affine generators with shared variables; and
- `Taylor`: shared first-order variables plus independent interval remainder.

The database owns a separate certificate array. Every record points to one
certificate, and `certificate(id)` performs deterministic certificate lookup.
Primary geometries require `CertifiedRegion`; Portal and TrajectoryTube
records require `CertifiedConnectivity`.

## OBB Atlas construction

`ObbAtlasBuilder::build(robot, scene, samples, options)`:

1. validates, sorts, and deduplicates samples;
2. grows a point-centered OBB around every sample;
3. tries deterministic segment tubes to the configured nearest neighbors;
4. rejects every candidate whose conservative enclosure cannot be certified;
5. deduplicates cells by exact geometry subject digest;
6. tests arbitrary AABB/OBB cell pairs for a nonempty convex intersection;
7. issues subject-bound Portal certificates and labels components.

Growth and discovery have independent hard limits for sample count, pair
evaluations, validations, iterations, and Portal count. Cancellation is
cooperative. Repeating a build with identical inputs and options produces the
same record order, IDs, certificates, and graph.

The current OBB validator certifies the OBB's C-space AABB enclosure. This is
conservative but may reject a safe oriented cell. The builder never promotes
sampling evidence.

## Arbitrary Portal discovery

Portal candidates first pass exact AABB-enclosure overlap pruning. Surviving
pairs are converted to half-space constraints and solved by deterministic
cyclic projection. A Portal is emitted only after the returned witness passes
both parent-cell membership checks. Failure to find a witness within the
budget is an absence of connectivity evidence, not proof of disconnection.

Unlike the v0.4 corridor format, discovery is not limited to consecutive cells
along the source path. `RegionDatabase::from_atlas` and `from_corridor` both
run the same arbitrary-pair discovery. Higher-order cell intersections are not
yet promoted to Portal certificates in v0.7.

## Correlation-preserving regions

`CspaceZonotope` represents

```text
q = center + sum(generator[k] * xi[k]),  xi[k] in [-1, 1].
```

`CspaceTaylorRegion` adds an independent interval remainder per joint. The
experimental `HigherOrderRegionValidator` carries shared variables through DH
trigonometric linearization and matrix products, while accumulating nonlinear
and floating-point residuals conservatively. This can prove a tighter envelope
than independently intervalizing every joint when correlations cancel.

Use `make_higher_order_region_certificate` only with the exact successful
validation result. `RegionDatabase::create` then checks the robot, scene,
geometry subject, certificate identity, joint-limit enclosure, and complete
workspace dependency before accepting the record.

## Queries

- `regions_at(q, options)` returns matching records sorted by ID.
- `contains(q, options)` is a membership convenience predicate.
- `nearest_region(q, options)` uses conservative AABB distance with stable ID
  tie-breaking.
- `connected(q1, q2)` compares certified component labels of primary records.
- `region(id)` and `certificate(id)` return optional records.
- `verify_compatible(robot, scene)` requires exact SHA-256 identity equality.

Portal and TrajectoryTube records are excluded from membership queries by
default. Set `RegionQueryOptions::include_portals` or
`include_trajectory_tubes` explicitly. A tube query means membership in any
certified cell in the recorded chain; it is not a time-parametrized swept-volume
claim.

## Example

```cpp
auto result = rbfsafe::ObbAtlasBuilder{}.build(
    robot, scene, {{-0.5, -0.2}, {0.0, 0.0}, {0.5, 0.2}});
if (!result)
    return result.error();

auto& database = result.value().database;
database.save("region-database");
bool certified = database.contains(rbfsafe::Configuration{0.0, 0.0});
```

The Python API mirrors these high-level builders, queries, persistence
operations, higher-order validators, and options.
