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
6. Run `python tools/check_project_scope.py --root .`; every requirement must
   retain public-API, behavioral-test, and documentation evidence, and the
   checked matrix must contain exactly one matching marker.
7. Run `python tools/check_chinese_docs.py --root .`; every top-level English
   document must retain a same-name Simplified Chinese mirror and valid local
   links.

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
Run the bounded-execution-session quickstart and fixed fixture through C++,
Python, and CLI readers. Reverify the exact Atlas trajectory/route,
profile/checkpoint/head, reviewer quorum, controller acknowledgement, armed
monitor observation, freshness, and time arithmetic. Reject wrong
configuration/index/time, disarmed or nonconformant observations, wrong keys
or anchors, duplicate/insufficient approvals, overflow, load budgets,
symlinks, unknown schema, truncation, and tampering. Confirm the session
remains `Unknown` and non-authorizing and only an exact closed-window command
returns non-open-ended `RuntimeExecutable`.
Run the execution-ledger C++/Python examples and fixed fixture through both
inspectors. Verify expected-head exclusion, strict authorization/completion
order, duplicate/skip rejection, independently signed controller outcomes,
late-completion expiration, cancellation while awaiting completion, every
exact dependency-revocation kind, automatic current-checkpoint reviewer-key
revocation, checkpoint rollback/fork rejection, observation-time monotonicity,
offline replay, abandoned temporary tolerance, symlink rejection, unknown
schema, truncation, tamper, count/byte/signature limits, and cancellation.
Confirm ledger/summary/audit evidence remains `Unknown`; only the returned
exact command decision may contain non-open-ended `RuntimeExecutable`.
Run the transparency C++/Python quickstart and fixed fixture through both
inspectors. Verify the exact deployment/trust/head anchor, outstanding-command
and closed-window observation binding, current active publication-capable
source keys, distinct-service quorum, controller-source exclusion, canonical
ordering, signed checkpoint chain, both inclusion paths, prefix consistency,
expected-head rejection, caller-pinned identity/checkpoint reopen, writer
contention, abandoned temporary tolerance, symlink rejection, unknown schema,
truncation, tamper, count/byte/attestation limits, and cancellation. Confirm
all anchors, observations, logs, proofs, checkpoints, and audits remain
non-authorizing `Unknown`.
Run witnessed transparency through C++, Python, both inspectors, and the fixed
gossip fixture. Verify compact-proof frontier/subtree bounds, wrong-root and
wrong-size rejection, witness quorum/service distinctness/log-signer
exclusion, canonical ordering, inactive/non-publication key rejection,
sender sequence/parent chains, same-size equivocation, invalid-proof conflicts,
incomplete proof paths, proof-DAG reachability, expected archive head, writer
contention, bundle/log pin mismatch, symlink and unexpected-entry rejection,
unknown schema, truncation, tamper, byte/count/witness/proof/pair/graph limits,
and cancellation. Confirm all outputs remain non-authorizing `Unknown`.
Run provenance replay through C++, Python, both inspectors, and the fixed
schema-1 fixture. Verify exact subject/trust binding, contiguous hardware and
per-source time chains, adapter/authority/vendor/scope pins, distinct-service
quorums, uncertainty limits, fresh/stale/future/inconsistent/incomplete
status, unsigned overflow boundaries, wrong-source/key/clock rejection,
unknown schema, checksum/tamper/truncation/symlink rejection, overwrite
protection, count/byte/policy limits, and cancellation. Confirm
`SATISFIED`, `FRESH`, and combined `ready` remain non-authorizing `Unknown`.
Run the continuous fleet occupancy C++/Python quickstarts and fixed schema-1
and schema-2 fixtures through both inspectors. Verify deterministic
subdivision, sampled IFK-AA envelope containment, fixed-translation legacy
replay, right-handed rotation validation, outward-rounded interval
transforms, translation/angular uncertainty expansion, exact robot replay,
mixed-schema handling, duplicate deployment and timeline/frame rejection,
overlap and separation-margin witnesses, single-input rejection,
cancellation, all work and load limits, unknown schema, truncation,
checksum/tamper/symlink rejection, overwrite protection, cross-platform byte
equality, and fixed bundle/report identities. Confirm every occupancy,
conflict, report, bundle, and successful separation status remains
non-authorizing `Unknown`.
Run the authenticated occupancy publication C++/Python quickstarts and fixed
schema-1 fixture through both inspectors. Verify one-read exact-byte digest,
length, decoded bundle/timeline/frame binding, full trajectory coverage of
the closed validity window, active publish-key policy, wrong-secret and
signature rejection, explicit stream/publisher/trust/parent/tick pins,
root/successor sequence rules, wrong parent, expired tick, payload
substitution, schema/checksum/tamper/truncation/symlink rejection, byte limits,
cancellation, overwrite protection, and fixed cross-platform identities.
Confirm publications and verification results remain non-authorizing
`Unknown`.
Run the occupancy publication history C++/Python quickstarts and fixed
schema-1 directory through both inspectors. Verify exact
stream/publisher/trust/root/head pins; complete signature, digest, decoded
occupancy, window, sequence, and parent replay; expected-head append/open;
writer contention; committed-record crash semantics; identical, forward-
extension, reverse-extension, and fork relations; corruption, symlink,
unknown-schema, byte/count limits, and cancellation. Confirm full-directory
rollback requires an externally retained head and that every history/audit
result remains non-authorizing `Unknown`.
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
calibration-lifecycle, bounded-execution-session, execution-ledger, or
transparency-log, transparency-gossip-archive, or
verifiable-provenance-bundle, continuous-fleet-occupancy-bundle, or
authenticated-occupancy-publication, or occupancy-publication-history schema
changes require an independent schema number,
fixed-format fixtures, and a documented reader or explicit incompatibility
error. A library version change must never silently reinterpret an existing
storage schema.
