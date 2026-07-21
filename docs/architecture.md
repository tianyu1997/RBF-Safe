# Architecture

RBF-Safe uses one-way dependencies so that geometry and partitioning remain
usable without the Atlas layer:

```text
RBFSafe::geometry
       ↑
RBFSafe::lect
       ↑
RBFSafe::atlas ← RBFSafe::rbfsafe (aggregate target)
```

## Modules

### Geometry

Owns standard-library value types, modified-DH forward kinematics, robot and
scene canonical identity, affine-arithmetic endpoint propagation, conservative
LinkIAABB construction, and the default `RegionValidator`.

### LECT

Owns a deterministic binary partition over a C-space AABB. Split keys are bit
paths from the root, so public identity is independent of memory allocation or
array position. `LectTree` is mutable; `LectSnapshot` is read-only.

### Atlas

Owns certificates, seed-guided construction, safe regions, revalidated merges,
adjacency, connected components, an immutable region query BVH, schema-v1
persistence, and continuous trajectory coverage auditing.

### Python and tools

pybind11 mirrors stable high-level operations and maps error categories to
Python exceptions. The C++ and Python `rbfsafe-inspect` tools load through the
same validating reader used by the library.

### Optional OMPL adapter

`RBFSafe::ompl` depends on `RBFSafe::atlas` and OMPL, but the dependency does
not flow back into the core targets or Python wheels. It owns the conversion
between `RealVectorStateSpace` states and configurations, strict Atlas-backed
state validity, continuous edge validation through `TrajectoryAuditor`, and
certified-region sampling.

The trajectory auditor remains in the Atlas layer: it consumes immutable
certified regions and produces a report without depending on robot geometry,
planners, or storage internals.

## Construction flow

1. Validate and canonically identify the robot and scene.
2. Sort and deduplicate seed configurations.
3. Create a LECT rooted at the complete joint-limit domain.
4. Certify the root or split unresolved seeded nodes along the normalized
   longest axis, validating both children.
5. Stop at a certificate, maximum depth, minimum normalized width,
   cancellation, or node budget.
6. Revalidate the rectangular union before merging adjacent regions.
7. Build deterministic overlap/touch adjacency and connected components.
8. Bind certificates and stable region IDs to the resulting Atlas.

## Design constraints

- Public headers do not expose Eigen, JSON, pybind11, or binary storage types.
- Samples influence exploration only and cannot raise evidence level.
- All expected failures return `Result<T>` in C++.
- No C++ struct is written directly to disk.
- The current validator is deliberately singular; adding another backend
  requires an explicit certificate and compatibility design.
