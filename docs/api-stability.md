# Public API stability policy

RBF-Safe 1.0 froze the initial reviewed public source surface. RBF-Safe 2.0
retained it and added `RBFSafe::policy`; RBF-Safe 3.0 retains both surfaces and
adds `RBFSafe::memory`; RBF-Safe 3.1 additively introduces deterministic memory
identities and `SafetyMemoryStore`; RBF-Safe 3.2 adds `FleetScheduleArchive`
and its version/load-option records; RBF-Safe 3.3 adds `RBFSafe::trust` and
artifact-attestation records/functions. Public headers under `include/rbfsafe`,
installed CMake targets, and names exported from `rbfsafe.__init__` are tracked by the current
`data/api_surface_v3.sha256` snapshot. Preserved v1 and v2 snapshots record the
historical contracts; `tools/check_api_surface.py` selects the snapshot for the
library's current major version.

## Compatibility promise

Within the 3.x line:

- existing documented C++ declarations, enum values, defaults, target names,
  Python names, argument meanings, and exception categories remain source
  compatible;
- new overloads, fields with safe defaults, targets, and Python names may be
  added in a minor release;
- a deprecated API remains functional for the rest of 3.x and may be removed
  only in 4.0;
- defect fixes may reject inputs that were always invalid, corrupt, identity
  mismatched, or unsupported; and
- safety fixes may conservatively turn a former certificate outcome into an
  undetermined/rejected outcome, but never silently weaken validation.

The snapshot is a review gate rather than a substitute for compatibility
tests. Any intentional additive change updates it in the same pull request and
documents the addition. A removal or incompatible signature change requires a
major-version proposal.

## C++ ABI

RBF-Safe promises C++ source compatibility, not a universal binary ABI across
compilers, standard libraries, build types, sanitizers, or runtime-library
choices. Downstream C++ consumers should rebuild against each RBF-Safe update.
Official Python wheels bundle the extension built for their exact Python and
platform tag and are tested as complete artifacts.

## Stable CMake targets

The following installed target names are stable in 3.x:

- `RBFSafe::geometry`, `RBFSafe::lect`, `RBFSafe::atlas`;
- `RBFSafe::update`, `RBFSafe::ik`, `RBFSafe::corridor`;
- `RBFSafe::regions`, `RBFSafe::planning`, `RBFSafe::optimization`;
- `RBFSafe::shield`, `RBFSafe::policy`, `RBFSafe::memory`, and aggregate
- `RBFSafe::shield`, `RBFSafe::policy`, `RBFSafe::memory`, `RBFSafe::trust`,
  and aggregate `RBFSafe::rbfsafe`; and
- optional `RBFSafe::ompl` when installed with OMPL support.

Every public C++ failure that is part of normal control flow remains a
`Result<T>`. Python continues to map the stable error categories documented in
[the API overview](api.md). New error context text is not itself a stable
machine-readable contract.

## Evidence compatibility

Numeric values and ordering of `EvidenceLevel` are stable throughout 3.x.
Consumers must compare enum values rather than parsing display names. No 3.x
component may issue `RuntimeExecutable` without a separately reviewed
deployment-profile contract that models timing, tracking, uncertainty, and
hardware assumptions.

Storage compatibility is governed separately by
[the schema migration policy](schema-migrations.md). A library version bump
never silently changes the meaning of persisted bytes.
