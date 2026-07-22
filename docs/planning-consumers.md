# Certified planning consumers

RBF-Safe 0.8 separates reusable certified planning data from any particular
planner. Include `<rbfsafe/planning.h>` and link `RBFSafe::planning` for the
framework-independent API.

## Certified region sampling

`CertifiedRegionSampler::create(atlas, options)` creates one deterministic
random stream over the Atlas AABB union. `sample()` chooses a region and then
draws uniformly inside it. `sample_near(reference, distance)` returns a
certified point no farther than the requested Euclidean distance or returns a
failure when the ball does not meet the certified union.

Two region-selection policies are available:

- `VolumeWeighted` uses region volume normalized by the Atlas root; and
- `UniformRegions` gives every stored region equal selection weight.

Overlapping regions are not de-overlapped, so overlap may receive extra
probability mass. This changes search bias only. The sampler is a
single-threaded stream; create one instance and seed per worker. Its output is
covered by an existing `CertifiedRegion` but is not a new certificate.

## Certified roadmap

`CertifiedRoadmapBuilder` creates an immutable, in-memory graph from a
`SafeAtlas`:

1. every certified AABB contributes its center;
2. every pair listed as adjacent is checked again for exact, zero-tolerance
   intersection;
3. a midpoint of the exact intersection becomes a portal-witness node; and
4. the two center-to-witness edges are each labelled by the single convex
   region that contains the complete edge.

This construction deliberately does not trust adjacency tolerance to bridge a
gap. Nonintersecting adjacency entries are counted and omitted. Node and edge
budgets plus cooperative cancellation bound the build. The roadmap stores the
Atlas robot and scene digests and provides `verify_compatible` and
`nearest_node`; it has no independent disk schema in 0.8.

The roadmap is a reusable search seed, not a planned path or timing law. A
consumer must still connect its exact start and goal and audit its final path.

## Python

The same high-level API is available in the wheel:

```python
sampler_options = rbfsafe.CertifiedSamplerOptions()
sampler_options.seed = 7
sampler = rbfsafe.CertifiedRegionSampler.create(atlas, sampler_options)
q = sampler.sample()

roadmap = rbfsafe.CertifiedRoadmapBuilder().build(atlas).roadmap
roadmap.verify_compatible(robot, scene)
```

OMPL and MoveIt build on these primitives; see the
[OMPL adapter](ompl-adapter.md) and [MoveIt integration](moveit2.md).
