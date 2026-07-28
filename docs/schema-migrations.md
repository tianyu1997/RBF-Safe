# Schema support and migrations

Library SemVer and storage schema numbers are independent. RBF-Safe 4.2 reads
every standalone format released by 0.x and never interprets a legacy
RapidBoxForest cache as RBF-Safe data.

| Format | Read | Write | Migration in 4.2 |
|---|---:|---:|---|
| Robot JSON | 1 | 1 | None required |
| Scene JSON | 1 | 1 | None required |
| Standalone LECT | 1 | 1 | None required |
| Atlas | 1, 2 | 2 for new/update; preserve 1 on plain save | Schema 1 to 2 by full regional revalidation through `AtlasUpdater` |
| HiPaC corridor | 1 | 1 | None required |
| Region database | 1 | 1 | None required |
| Atlas version store | 1 | 1 | Contained Atlas versions retain their own schema |
| Policy feedback | 1 | 1 | New independent format; no legacy format is interpreted |
| Safety memory | 1 | 1 | New independent format; locators do not import referenced payloads |
| Safety-memory revision store | 1 | 1 | Immutable wrapper; contained memories retain schema 1 |
| Fleet-schedule archive | 1 | 1 | Independent version history; no report-only legacy bytes are inferred |
| Artifact attestation | 1 | 1 | Independent sidecar; existing payloads require a new keyed attestation |
| Policy calibration profile | 1 | 1 | Independent empirical record; no uncalibrated metadata is upgraded implicitly |
| Policy calibration lifecycle | 1 | 1 | Independent monitoring history; no profile or feedback is treated as operational review |
| Artifact transfer journal | 1, 2 | 2 | Schema 1 loads unchanged; public-key provenance cannot be inferred |
| Service trust bundle | 1, 2, 3 | 2 from `create`; 3 from explicit quorum creation; preserve schema on rotation/save | Schemas 1/2 never gain quorum policy implicitly |
| Service trust history | 1, 2 | Root schema 2 writes history 1; root schema 3 writes history 2 | Single-signature histories never gain authorization sets implicitly |
| Service trust checkpoint | 1 | 1 | New signed anchor; no head or root is inferred from older artifacts |
| Reviewed deployment profile | 1 | 1 | New signed governance artifact; no deployment ID or review text is upgraded implicitly |
| Bounded execution session | 1 | 1 | New exact-session artifact; no profile, trajectory, acknowledgement, clock, or execution authority is inferred |
| Execution ledger | 1 | 1 | New append-only exact-session history; no command progress, revocation, completion, or current checkpoint is inferred |
| Transparency log | 1 | 1 | New append-only deployment/observation history; no global freshness, physical observation, prior publication, or execution evidence is inferred |
| Transparency gossip archive | 1 | 1 | Independent witnessed-checkpoint exchange history; no peer discovery, global freshness, hardware provenance, time, or execution authority is inferred |
| Verifiable provenance bundle | 1 | 1 | Explicit new hardware-statement/time-source artifact; no vendor evidence, trusted adapter, clock, historical assertion, or execution authority is inferred |
| Continuous fleet occupancy bundle | 1, 2 | 1 from legacy translation builder; 2 from bounded-frame builder/mixed input | Schema 1 remains exact fixed-translation evidence; rotation or uncertainty is never inferred |
| Authenticated occupancy publication | 1 | 1 | Independent signed statement; an unsigned occupancy bundle is never upgraded implicitly |

Unknown schemas fail with `IncompatibleFormat`; malformed known schemas fail
with `CorruptData` or `ResourceLimit`. There is no implicit downgrade.

## Atlas schema 1 to 2

Schema 1 lacks exact regional subject digests, link-envelope dependencies,
repair domains, transition documents, and version lineage. Those fields
cannot be invented from old bytes. Migration therefore requires the exact
robot and old scene plus a distinct target `SceneSnapshot`, and performs full
regional validation:

```python
legacy = rbfsafe.SafeAtlas.load("atlas-v1")
robot = rbfsafe.SerialRobotModel.from_json("robot.json")
old_scene = rbfsafe.SceneSnapshot.from_json("scene-v1.json")
new_scene = rbfsafe.SceneSnapshot(
    old_scene.obstacles,
    "scene-v1-schema2-migration",
)
migrated = rbfsafe.AtlasUpdater().update(robot, old_scene, new_scene, legacy)
assert migrated.atlas.storage_schema == 2
migrated.atlas.save("atlas-v2")
```

Changing only the scene version makes the identity transition explicit while
keeping obstacle geometry identical. Because schema-1 certificates cannot be
inherited, `certificates_inherited` is zero and every retained region appears
in `regions_revalidated`. The fixed `data/atlas_schema1` artifact is loaded,
byte-preserved, migrated this way, and validated on Linux and Windows CI.

## 3.x format policy

- Every new schema receives a separate specification, bounded reader, fixed
  cross-platform fixture, corruption tests, and explicit migration or
  incompatibility behavior before release.
- Readers for schemas supported by 3.15 remain available throughout 4.x.
- Writers publish atomically and never overwrite by default.
- Migration is always explicit and writes a new destination; input artifacts
  remain untouched.
- Certificate evidence is revalidated whenever an older schema lacks fields
  needed to justify inheritance.

Schema removal or reinterpretation requires a major library release. A future
writer may introduce a new schema in 3.x only while preserving these reader
and migration guarantees.

Safety-memory schema 1 is described separately in
[Safety memory format](safety-memory-format.md). Its lifecycle history is
replayed during load; migration cannot synthesize missing registration,
transition, invalidation, or reuse events. There is no implicit conversion
from Atlas version stores, policy feedback, or RapidBoxForest caches.

Safety-memory-store schema 1 is specified in
[Transactional safety memory](safety-memory-store.md). It adds immutable
revision metadata around unchanged memory directories. Importing an existing
memory is explicit: create a new store with that validated memory as revision
zero. The source directory remains untouched.

Fleet-schedule-archive schema 1 is specified in
[Versioned fleet schedules](fleet-schedule-archive.md). A v3.0 in-memory report
has no implicit migration because it did not carry a durable parent chain or
whole-memory identity. Recreate and publish the schedule from its exact fleet,
memory revision, and reservations into a new archive destination.

Artifact-attestation schema 1 is specified in
[Authenticated artifact attestations](artifact-attestation.md). No migration
can invent authentication for an older payload. A trusted service must read
the immutable bytes, select an external key/key ID, and publish a new sidecar;
the original payload and its format remain unchanged.

Policy-calibration-profile schema 1 is specified in
[Policy calibration profiles](policy-calibration.md). Existing policy
feedback cannot be converted implicitly because it does not declare held-out
bin boundaries, outcome semantics, exact model/data identity, or evaluation
method. Build a new profile from independently reviewed calibration data;
existing feedback bytes remain unchanged.

Policy-calibration-lifecycle schema 1 is specified in
[Policy calibration drift and lifecycle](policy-calibration-lifecycle.md).
A profile alone has no operational history, and policy-feedback schema 1 does
not bind reviewed aggregation windows, thresholds, lifecycle parents, or
manual transitions. Create a new pending lifecycle for an exact validated
profile, assess retained operational aggregates, and explicitly review any
activation; no prior format is upgraded implicitly.

Artifact-transfer-journal schemas 1 and 2 are specified in
[Artifact transfer journal](artifact-transfer-journal-format.md). Existing
memory locators or artifact-attestation sidecars cannot be upgraded
implicitly: they do not prove that a particular remote request received a
particular response. Re-run the transfer through the v3.6 verification
contract and append the resulting metadata to a new journal. Source artifacts,
memories, and attestations remain unchanged.

The 3.7 reader loads schema 1 directly and preserves every historical transfer
identity. Saving a loaded or new journal writes schema 2. No migration can
invent `verification_key_id` or `trust_bundle_id` for a legacy HMAC record;
those fields remain empty. To obtain public-key provenance, repeat the
exchange through an Ed25519 request and offline verification against an
explicitly authorized trust bundle, then append the new verified transfer.

Service-trust-bundle schemas 1, 2, and 3 are specified in
[Service trust bundles](service-trust-bundle-format.md). There is no implicit
migration from an HMAC key ID: symmetric key possession does not identify an
Ed25519 public key or authorize a trust root. Generate and govern a new
public/private key pair, create a caller-pinned root bundle through an
authenticated out-of-band process, and activate it deliberately.

The 3.8 reader preserves schema-1 bundle IDs and assigns every loaded legacy
key `allow_rotate=false`. New bundles and successors write schema 2, whose
identity binds the new rotation permission. No operation/state heuristic may
upgrade a schema-1 key into a rotation authority. Deployments adopting signed
rotation must create and authenticate a distinct schema-2 root and pin it
deliberately.

Service-trust-history schema 1 is specified in
[Service trust histories](service-trust-history-format.md). It is a new
directory format containing complete schema-2 bundles and signed rotation
records. There is no implicit import of standalone bundles or parent-linked
but unsigned schema-1 rotations. A caller creates a history only from an exact
out-of-band-pinned schema-2 root and retains each accepted head outside the
history rollback domain.

Service-trust-bundle schema 3 and service-trust-history schema 2 preserve the
older readers and IDs. A deployment adopts quorum rotation only by creating
and authenticating a distinct schema-3 root with an explicit immutable policy.
Schema-3 successors cannot change that policy or rotate into another schema.
Schema-2 histories always contain canonical authorization sets; a
single-signature schema-1 record is never rewritten or counted as a quorum.

Service-trust-checkpoint schema 1 is specified in
[Service trust checkpoints](service-trust-checkpoint-format.md). No migration
can infer the latest accepted head or authenticate a root. A deployment must
replay its exact pinned history, collect the configured current-head quorum,
persist a new checkpoint, and distribute both its root and checkpoint IDs
through an authenticated channel. Existing history bytes remain untouched.

Reviewed-deployment-profile schema 1 is specified in
[Reviewed deployment profiles](deployment-profile-format.md). No earlier
format contains the exact controller/platform/runtime identities, runtime
limits, required review roles, checkpoint binding, and Ed25519 approval set.
Create and approve a new profile against an explicitly pinned checkpoint;
calibration lifecycle text, safety-memory deployment IDs, and trust bundles
remain unchanged.

Bounded-execution-session schema 1 is specified in
[Bounded execution sessions](bounded-execution-session-format.md). No earlier
artifact binds the exact certified command sequence, reviewed profile,
controller/monitor endpoints, execution-review quorum, signed acknowledgements,
and closed monotonic session window. Create and approve a new session; no
profile, route, runtime snapshot, or historical planner output is upgraded
implicitly.

Execution-ledger schema 1 is specified in
[Revocation-aware execution ledger](execution-ledger-format.md). A bounded
session alone has no durable command order, completion history, or current
checkpoint record. Create a new ledger for that exact validated session and
append only caller-observed events. No migration invents historical dispatch,
completion, cancellation, expiration, revocation, checkpoint freshness, or
physical execution evidence.

Transparency-log schema 1 is specified in
[Deployment and runtime transparency](transparency-log-format.md). Neither a
reviewed profile nor an execution ledger contains a public log identity,
publication history, independent observation-source quorum, Merkle checkpoint,
or externally retained newest-head anchor. Create a new log with an explicitly
pinned identity and publish only newly verified anchors/observations. No
migration invents historical publication, observation authenticity,
checkpoint freshness, network consensus, or physical execution.

Verifiable-provenance-bundle schema 1 is specified in
[Verifiable provenance and external time](verifiable-provenance-format.md).
No trust bundle, service key, deployment profile, execution ledger, or
transparency record contains vendor evidence digests, adapter pins,
hardware-usage scopes, signed external-time chains, or a caller-defined
freshness policy. Create and sign a new bundle from explicitly reviewed
inputs. No existing timestamp or monotonic observation is upgraded
implicitly, and no migration can invent physical key custody or trustworthy
time.

Continuous-fleet-occupancy-bundle schemas 1 and 2 are specified in
[Continuous fleet occupancy](continuous-fleet-occupancy-format.md). A v3
fleet reservation contains one caller-declared workspace AABB, not the exact
joint trajectory, robot model, per-link swept envelopes, timeline/frame
contract, or subdivision parameters. No migration can infer those values.
Build a new occupancy from reviewed trajectory/model inputs, replay it against
the exact model, and deliberately integrate its non-authorizing report into
the deployment's scheduling policy.

Schema 1 has exact fixed-translation semantics and remains readable and
robot-replayable. Schema 2 adds explicit nominal rotation plus translation
and angular uncertainty. A schema-1 record is not silently upgraded because
no file can establish missing calibration uncertainty. To publish schema 2,
obtain a reviewed `DeploymentFrameBounds` input and rebuild the occupancy
from the exact robot and trajectory. Mixed schema-1/schema-2 inputs are
permitted only inside a schema-2 bundle and remain distinguishable by each
record's storage schema and algorithm version.

Authenticated-occupancy-publication schema 1 is specified in
[Authenticated occupancy publication](authenticated-occupancy-publication-format.md).
It is a new statement over an existing exact occupancy file. There is no
implicit conversion: create a publication only with an explicitly trusted
publication key, reviewed stream/publisher identity, closed tick window, and
retained parent. A checksum, occupancy digest, or unsigned bundle cannot
invent publisher authentication, trust, freshness, or a current stream head.
