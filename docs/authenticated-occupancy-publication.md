# Authenticated occupancy publication

RBF-Safe 4.2 adds the independent `RBFSafe::coordination` target for
publishing an existing continuous-fleet occupancy bundle as an authenticated,
caller-pinned statement. The layer does not recompute the occupancy: it reads
one regular file once, validates the exact bytes as a
`ContinuousFleetOccupancyBundle`, binds their SHA-256 digest and length, and
signs the resulting publication identity with an Ed25519 key authorized to
publish by an exact `ServiceTrustBundle`.

The resulting `OccupancyPublication` binds:

- the exact serialized occupancy bytes and decoded occupancy-bundle ID;
- timeline and workspace-frame IDs from that bundle;
- one publisher service and public-key ID;
- one exact trust-bundle ID;
- a caller-defined stream ID, positive monotonic publisher sequence, and
  parent publication ID;
- a closed logical-tick validity window covered by every deployment
  trajectory in the bundle; and
- the Ed25519 algorithm and signature.

The publication and `VerifiedOccupancyPublication` deliberately return
`EvidenceLevel::Unknown` and `authorizes_execution() == false`. Authentication
proves which trusted software key signed which bytes under explicit caller
pins. It does not prove obstacle freedom, self-collision freedom, clock
synchronization, physical robot state, network delivery, consensus, or
controller enforcement.

## Signing and verification

Include `<rbfsafe/coordination.h>` and link `RBFSafe::coordination`, or use the
aggregate target.

```cpp
auto publication = sign_continuous_fleet_occupancy_publication(
    "occupancy.json",
    trust_bundle,
    "cell-a-occupancy-v1",
    "fleet-coordinator",
    publisher_key.id,
    secret_key,
    1,
    "",
    100,
    140);
if (!publication)
    return publication.error();

auto verified = verify_continuous_fleet_occupancy_publication(
    "occupancy.json",
    publication.value(),
    trust_bundle,
    "cell-a-occupancy-v1",
    "fleet-coordinator",
    trust_bundle.id(),
    "",
    120);
```

Verification is fail-closed. The caller must supply the expected stream,
publisher service, exact trust-bundle ID, retained parent ID, and evaluation
tick. Verification then:

1. validates the publication and trust-bundle structure;
2. compares every caller pin;
3. checks the closed validity window;
4. selects the exact active, publish-authorized key at the publisher sequence;
5. verifies the Ed25519 signature;
6. reads the occupancy file once as bounded exact bytes without following a
   symbolic link;
7. decodes and fully validates the bundle from those same bytes; and
8. checks byte length/digest, bundle/timeline/frame identities, and complete
   validity-window coverage.

Signing performs the same exact-byte and trust-key checks and verifies the
new signature against the public key before returning it. Secret keys are
caller-owned and are never persisted. The Python binding keeps a temporary
secret copy only for the native call and clears that copy when it leaves
scope; applications still need a real key manager.

## Stream succession and rollback resistance

A root publication has sequence 1 and an empty parent. Later publications
must have sequence `previous + 1`, name the exact previous publication ID as
their parent, and retain the stream, publisher service, timeline, and
workspace frame. `verify_occupancy_publication_successor(previous, successor)`
checks only this structural relationship.

Applications must first authenticate each publication independently, retain
the accepted publication ID outside the untrusted payload, and supply it as
`expected_parent_publication_id` during verification. This prevents accepting
an old or forked statement relative to that retained head. The helper is not
a persistent log, fork-consensus protocol, peer discovery service, or global
rollback oracle. A publisher may use a rotated trust bundle for a successor,
but the verifier must explicitly pin that successor's exact bundle ID.

Ticks belong to the bundle's logical timeline. RBF-Safe does not read a wall
clock or assert that two machines agree on the tick. The caller is responsible
for deriving the evaluation tick from an appropriate trusted mechanism.

## Python

```python
publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
    "occupancy.json",
    trust_bundle,
    "cell-a-occupancy-v1",
    "fleet-coordinator",
    publisher_key.id,
    secret_key,
    1,
    "",
    100,
    140,
)
publication.save("publication.json")

verified = rbfsafe.verify_continuous_fleet_occupancy_publication(
    "occupancy.json",
    publication,
    trust_bundle,
    "cell-a-occupancy-v1",
    "fleet-coordinator",
    trust_bundle.id,
    "",
    120,
)
assert not verified.authorizes_execution
```

`examples/occupancy_publication_quickstart.cpp` and
`examples/occupancy_publication_quickstart.py` create a deterministic
demonstration key only so the example and fixture are reproducible. Never use
that seed in production.

## Inspection

Python entry point:

```bash
rbfsafe-inspect publication.json \
  --occupancy-payload occupancy.json \
  --occupancy-trust-bundle trust-bundle.json \
  --expected-occupancy-stream fixture-cell-occupancy-stream-v1 \
  --expected-occupancy-publisher fixture-occupancy-publisher \
  --expected-occupancy-trust-bundle 89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d \
  --expected-occupancy-parent - \
  --occupancy-evaluation-tick 16
```

Native entry point:

```bash
rbfsafe-inspect publication.json occupancy.json trust-bundle.json \
  fixture-cell-occupancy-stream-v1 fixture-occupancy-publisher \
  89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d \
  - 16
```

Both tools report successful signature and payload verification only after
performing the full caller-pinned replay. A dash denotes the empty parent of
a root publication.

## Explicit exclusions

RBF-Safe 4.2 does not provide a network transport, distributed lock,
publisher election, Byzantine consensus, multicast, clock synchronization,
online key discovery, revocation feed, occupancy archive, moving-obstacle
model, trajectory execution, or hardware interlock. It also does not combine
authenticated occupancy with scene certificates or upgrade fleet-separation
status. Those concerns remain separate and must fail closed at the deployment
boundary.

The storage contract is specified in
[Authenticated occupancy publication format](authenticated-occupancy-publication-format.md).
RBF-Safe 4.3 adds an optional immutable,
[expected-head guarded publication history](occupancy-publication-history.md)
without changing the schema-1 standalone publication bytes.
