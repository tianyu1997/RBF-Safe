# Release procedure

## 1. Prepare

1. Move user-visible entries from `Unreleased` in `CHANGELOG.md` into the new
   version and date.
2. Confirm matching versions in `CMakeLists.txt`, `pyproject.toml`,
   `include/rbfsafe/version.h`, and `CITATION.cff`.
3. Review API, safety, input, storage, and compatibility documentation.
4. Confirm provenance for all migrated or derived code and fixtures.
5. Run `python tools/check_api_surface.py --root .`; any snapshot update must
   have an explicit compatibility review.

## 2. Validate

Run the CI-equivalent matrix from a clean checkout:

- Ubuntu 22.04/24.04 with GCC and Clang;
- ASan and UBSan with Clang;
- Windows with MSVC;
- Python 3.10-3.12 installed-wheel tests on Linux and Windows;
- independent downstream CMake `find_package(RBFSafe)` consumption;
- clang-format and warnings-as-errors.

Inspect the source tree for build products, absolute paths, paper assets,
legacy caches, generated Atlas directories, and old-library dependencies.
Run the C++ and Python quickstarts, save/load/query/update an Atlas,
save/load/query a generalized region database, and verify the fixed Atlas
schema-v1 compatibility fixture and schema-v2 payload hashes. Build the
certified roadmap and optimization quickstarts, exercise all four supported
OMPL planner kinds when that component is enabled, and run the MoveIt request,
response, IK, and constraint-sampler plugin tests in a sourced Jazzy workspace.
Run the runtime-shield quickstart and tests for all three action types,
accept/repair/reject outcomes, deterministic IDs, proposal selection,
telemetry, cancellation, and monitor classifications.
Run both policy-safety quickstarts and test every metadata rejection reason,
selection mode, duplicate handling, aligned feedback label, deterministic ID,
query filter, load limit, checksum failure, and native/Python inspection path.
Run both fleet-schedule archive quickstarts; verify expected-head and
idempotent publication, historical replay, every aggregate load limit,
checksum/semantic corruption rejection, fixed fixture identities, and both
inspection paths.
Run both artifact-attestation quickstarts and the RFC 4231 vector; verify
wrong-key/service/payload/lifecycle rejection, payload and metadata budgets,
cancellation, atomic overwrite behavior, fixed fixture identities, and
metadata-only versus authenticated CLI output. Confirm no secret key appears
in an installed artifact or committed non-fixture file.
Run both remote-artifact quickstarts; verify exact-byte digest requirements,
current-memory/generation/state checks, request/response replay rejection,
wrong and missing keys, explicit unauthenticated mode, byte/cancellation
limits, expected journal head, corruption/schema/load-budget failures, fixed
fixture identities, and native/Python journal inspection. Confirm that no
transport path, key, payload, or authentication tag is persisted in the
compact journal.
Run both public-identity quickstarts and the RFC 8032 vector; verify malformed
signature, wrong service/key, pending/retired/revoked state, operation/window,
rotation-parent, rollback/reactivation, resource, corruption, and unknown-
schema rejection. Verify the fixed bundle and schema-2 journal IDs, native/
Python inspection, caller-pinning warnings, legacy schema-1 reading, and
unchanged HMAC transfer identities. Confirm no seed/private key is installed,
persisted, printed, or committed outside deterministic test/example source.
Create a schema-2 rotation-capable root, sign an exact successor, publish it
to a trust history, and replay it with independently retained root/head IDs.
Verify wrong/stale head, wrong root, missing/altered records, altered
authorization, indirect files, writer contention, aggregate resource limits,
cancellation, legacy-root rejection, fixed history identities, and both
inspection paths. Demonstrate whole-directory rollback rejection with the
newer external head and document that rolling the external head back as well
is outside local detection.
Create a schema-3 two-signature distinct-service root, assemble a canonical
successor authorization set, publish/replay schema-2 history, sign its head
with the current quorum, and verify a standalone schema-1 checkpoint against
independently retained root/checkpoint IDs. Verify insufficient, duplicate,
same-service, wrong-secret, stale-anchor, tamper, ordering, signature-count,
and byte-limit failures; fixed fixture identities; cross-language byte
equality; and both CLI verification directions. Document that rolling back
the history and every external anchor together remains outside local
detection.
Run both reviewed-deployment-profile quickstarts against deterministic
reviewer/governance keys. Verify exact root/checkpoint/head binding,
publication permission, minimum/distinct-service quorum, every required
review role, wrong-secret and duplicate-signer rejection, bounded loading,
unknown schema, truncation and tamper rejection, fixed fixture identities,
cross-language byte equality, and native/Python inspection. Exercise every
runtime identity/timing/monitor/transport/artifact violation and confirm all
assessments remain `Unknown` with `runtime_executable=false`.
Run both calibrated-policy quickstarts and inspect the fixed profile at raw
confidence `0.9`; verify derived statistics are recomputed, conservative
confidence never exceeds raw confidence, and output remains explicitly below
runtime-executable evidence.
Run both calibration-lifecycle quickstarts and inspect the fixed lifecycle
with its exact profile. Verify stable, insufficient, and drift outcomes;
automatic quarantine; reviewed recovery; parent and expected-head rejection;
bounded load failures; fixed report/head IDs; and guarded-gate failure closed.
Build `rbfsafe-release-benchmark`, run its 128-iteration smoke gate and
8192-iteration soak gate, and confirm zero false-safe point checks and an exact
match with the committed cross-platform logical digest. Inspect timing and
memory only as release diagnostics.

## 3. Package

1. Build source and wheel distributions with `python -m build`.
2. Inspect wheel contents; it must contain only the Python package, extension,
   metadata, RBF-Safe license, third-party notice, and Monocypher license.
3. Install each wheel into a clean environment and run `pip check` plus the
   Python test suite.
4. Install the CMake package to an empty prefix and rebuild `tests/consumer`.

## 4. Publish

1. Merge or commit the exact tested tree on `main`.
2. Create an annotated `vX.Y.Z` tag on that commit and push it.
3. Create a GitHub release from the changelog, attach supported wheels and the
   source archive, and identify the Atlas schema version.
4. Verify release downloads in clean Linux and Windows environments.

Atlas, corridor, region-database, version-store, policy-feedback,
safety-memory, safety-memory-store, attestation, artifact-transfer-journal,
service-trust-bundle, service-trust-history, service-trust-checkpoint,
reviewed-deployment-profile, calibration-profile, or
calibration-lifecycle schema
changes require an independent schema number,
fixed-format fixtures, and a documented reader or explicit incompatibility
error. A library version change must never silently reinterpret an existing
storage schema.
