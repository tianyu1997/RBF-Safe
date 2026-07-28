# Public API stability policy

RBF-Safe 1.0 froze the initial reviewed public source surface. RBF-Safe 2.0
retained it and added `RBFSafe::policy`; RBF-Safe 3.0 retains both surfaces and
adds `RBFSafe::memory`; RBF-Safe 3.1 additively introduces deterministic memory
identities and `SafetyMemoryStore`; RBF-Safe 3.2 adds `FleetScheduleArchive`
and its version/load-option records; RBF-Safe 3.3 adds `RBFSafe::trust` and
artifact-attestation records/functions; RBF-Safe 3.4 additively extends
`RBFSafe::policy` with calibration profiles and calibrated gating; RBF-Safe
3.5 adds operational drift reports, profile lifecycle records, persistence,
and guarded calibrated gating without removing the 3.4 entry point. RBF-Safe
3.6 adds `RBFSafe::remote`, transport request/response values, service
attestations, verified transfers, and transfer journals. RBF-Safe 3.7 adds
`RBFSafe::identity`, Ed25519 helpers, service public-key/trust-bundle values,
monotonic rotation, and offline public-key verification; two optional public
provenance fields are appended to `VerifiedArtifactTransfer`. RBF-Safe 3.8
additively introduces rotation permission,
`ServiceTrustBundleAuthorization`, `ServiceTrustHistory`, load/rotation
records, signed successor functions, and bundle storage-schema inspection.
The existing key factory gains a trailing defaulted `allow_rotate=false`
argument, preserving prior calls and prior schema-1 behavior. RBF-Safe 3.9
additively introduces `ServiceTrustRotationPolicy`, schema-3
bundle creation, `ServiceTrustBundleAuthorizationSet`, schema-2 history
records, `ServiceTrustCheckpoint`, bounded checkpoint options, signature/
assembly/verification functions, and checkpoint-pinned history-open and
authorization-set publication overloads. Existing schema-2 creation,
single-authorization publication, and expected-head open behavior remain
available. RBF-Safe 3.10 additively introduces `RBFSafe::deployment`,
deterministic profile/runtime/review values, role-aware Ed25519 approval
functions, `ReviewedDeploymentProfile`, schema-1 load options and
persistence, and conformance assessment APIs. Existing evidence meanings and
all prior targets remain unchanged. RBF-Safe 3.11 additively introduces
`RBFSafe::execution`, certified command/session/endpoint/acknowledgement
values, exact-command authorization, and bounded schema-1 persistence.
RBF-Safe 3.12 additively introduces `ExecutionLedger`, ordered
authorization/completion and terminal-event values, expected-head mutations,
current-checkpoint revalidation, and schema-1 ledger persistence. RBF-Safe
3.13 additively introduces `RBFSafe::transparency`, deployment anchors,
independent runtime-observation attestations, signed Merkle checkpoints,
inclusion/prefix-consistency values, bounded schema-1 log persistence, and
audit APIs. RBF-Safe 3.14 additively introduces compact consistency proofs,
`RBFSafe::witness`, independent checkpoint cosignature quorums, authenticated
gossip, split-view audit, and bounded schema-1 gossip archives. RBF-Safe 3.15
additively introduces `RBFSafe::provenance`, explicitly scoped hardware-key
statements and policies, signed external-time chains, freshness reports,
combined replay, and bounded schema-1 bundle persistence. RBF-Safe 4.0 retains
the complete 3.x surface and adds `RBFSafe::occupancy`, timestamped
piecewise-linear trajectories, conservative swept-link records, deterministic
fleet separation reports, replay verification, and bounded schema-1 bundle
persistence. RBF-Safe 4.1 additively introduces `DeploymentFrameBounds`,
`build_robot_trajectory_occupancy_in_frame`, rotated and uncertain
deployment-frame fields, and schema-2 occupancy persistence while preserving
the 4.0 translation-only API and schema-1 reader. RBF-Safe 4.2 additively
introduces `RBFSafe::coordination`, `OccupancyPublication`,
`VerifiedOccupancyPublication`, exact-byte sign/verify functions, successor
validation, schema-1 publication persistence, and a public exact-byte
occupancy-bundle loader. RBF-Safe 4.3 additively introduces
`OccupancyPublicationHistory`, immutable history records, bounded history load
options, exact stored-payload replay, expected-head publication, deterministic
history relation/audit values, and schema-1 history directories. Public
headers under `include/rbfsafe`, installed CMake targets, and names exported
from `rbfsafe.__init__` are tracked by the current
`data/api_surface_v4.sha256` snapshot. Preserved v1, v2, and v3 snapshots
record the historical contracts; `tools/check_api_surface.py` selects the
snapshot for the library's current major version.

## Compatibility promise

Within the 4.x line:

- existing documented C++ declarations, enum values, defaults, target names,
  Python names, argument meanings, and exception categories, including the
  preserved 3.x surface and v4 occupancy API, remain source compatible;
- new overloads, fields with safe defaults, targets, and Python names may be
  added in a minor release;
- a deprecated API remains functional for the rest of 4.x and may be removed
  only in 5.0;
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

The following installed target names are stable in 4.x:

- `RBFSafe::geometry`, `RBFSafe::lect`, `RBFSafe::atlas`;
- `RBFSafe::update`, `RBFSafe::ik`, `RBFSafe::corridor`;
- `RBFSafe::regions`, `RBFSafe::planning`, `RBFSafe::optimization`;
- `RBFSafe::shield`, `RBFSafe::policy`, `RBFSafe::memory`, `RBFSafe::trust`,
  `RBFSafe::remote`, `RBFSafe::identity`, `RBFSafe::deployment`,
  `RBFSafe::execution`, `RBFSafe::transparency`, `RBFSafe::witness`,
  `RBFSafe::provenance`, `RBFSafe::occupancy`, `RBFSafe::coordination`, and aggregate
  `RBFSafe::rbfsafe`; and
- optional `RBFSafe::ompl` when installed with OMPL support.

Every public C++ failure that is part of normal control flow remains a
`Result<T>`. Python continues to map the stable error categories documented in
[the API overview](api.md). New error context text is not itself a stable
machine-readable contract.

## Evidence compatibility

Numeric values and ordering of `EvidenceLevel` are stable throughout 4.x.
Consumers must compare enum values rather than parsing display names. In 4.x,
only an exact closed-window `ExecutionCommandAuthorization` may issue
`RuntimeExecutable`; reviewed profiles, bounded sessions, ledgers, summaries,
transparency anchors/observations/logs/proofs/checkpoints, witness
cosignatures, gossip messages/archives/conflicts, and audit reports remain
`Unknown`. Hardware statements, external-time assertions, provenance bundles,
freshness reports, and combined provenance audits also remain `Unknown`, even
when policy status is `SATISFIED`, `FRESH`, or `ready`.
Continuous occupancies, conflict witnesses, fleet separation reports, and
bundles remain `Unknown`, including
`CertifiedSeparatedUnderSweptEnvelopes`.
Authenticated occupancy publications and verification results also remain
`Unknown`; occupancy publication histories, records, and prefix/fork audits
remain `Unknown` as well. A valid Ed25519 signature or consistent local
history never becomes execution evidence.
The application must still enforce its separately reviewed tracking,
uncertainty, clock, transport, device, and emergency-stop assumptions.

Storage compatibility is governed separately by
[the schema migration policy](schema-migrations.md). A library version bump
never silently changes the meaning of persisted bytes.
