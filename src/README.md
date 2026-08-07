# Source layout

Implementation files use the same top-level layers as the public module
headers. Small CMake targets remain visible one level below their aggregate:

```text
core/                       RBFSafe::core
envelope/                   RBFSafe::envelope
geometry/                   RBFSafe::geometry
lect/                       RBFSafe::lect
atlas/                      RBFSafe::atlas
applications/
  corridor, regions         certified cells and region database
  ik, planning, optimization
  update, shield, policy    dynamic and runtime consumers
assurance/
  occupancy, coordination   multi-robot occupancy and agreement
  memory, trust, remote     reusable and authenticated artifacts
  identity, deployment      public trust and reviewed deployment
  execution, transparency   authorization and audit trail
  witness, provenance       independent observation and provenance
internal/                   private implementation headers
```

`internal/` contains private headers shared by implementation targets. These
headers are not installed API and must not be included by downstream users.
The grouping does not merge implementation libraries: each nested directory
still maps to its existing fine-grained CMake target. `RBFSafe::applications`
and `RBFSafe::assurance` remain interface aggregates over those targets.

The foundational dependency graph is:

```text
core ─┬─> envelope ─> geometry ─┐
      └─> lect ─────────────────┴─> atlas
```
