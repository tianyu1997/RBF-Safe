# Public headers

Use the grouped entry points in `rbfsafe/modules/` when learning the library or
when an application consumes a whole layer:

```text
modules/core.h         Result, common value types, version
modules/envelope.h     workspace envelope value types
modules/geometry.h     robot model, kinematics, validators, certificates
modules/lect.h         LECT partition trees
modules/atlas.h        Atlas construction, scene deltas, trajectory auditing
modules/applications.h planning, IK, optimization, shield, policy
modules/assurance.h    memory, trust, deployment, execution, coordination
modules/ompl.h         optional OMPL adapter; requires the OMPL component
```

RBF-Safe 5.0 intentionally removed the former fine-grained headers directly
under `rbfsafe/`. Include one module header, or use `rbfsafe/rbfsafe.h` as the
all-in-one entry point. The optional OMPL header is not included by the
all-in-one entry point because it requires external OMPL headers.

The corresponding link targets are `RBFSafe::core`, `RBFSafe::envelope`,
`RBFSafe::geometry`, `RBFSafe::lect`, `RBFSafe::atlas`,
`RBFSafe::applications`, and `RBFSafe::assurance`. The last two are interface
aggregates; individual implementation targets remain available.

## Migration from 4.x

Replace old includes by ownership:

```text
result, types, version                         -> modules/core.h
workspace_envelope                            -> modules/envelope.h
model, geometry, certificate                  -> modules/geometry.h
lect                                           -> modules/lect.h
atlas, scene_delta, trajectory                -> modules/atlas.h
planning, IK, regions, shield, policy         -> modules/applications.h
occupancy, memory, trust, execution, identity -> modules/assurance.h
ompl                                           -> modules/ompl.h
```
