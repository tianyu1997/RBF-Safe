# Trust-rotating occupancy publication histories

RBF-Safe 4.5 adds `RotatingOccupancyPublicationHistory`, an independent
history type for one authenticated occupancy stream whose publication key
policy can change through an authorized `ServiceTrustHistory`.

The type composes two already public protocols:

- the service-trust root, signed single- or quorum-authorized bundle
  successors, and optional signed head checkpoints; and
- exact-byte occupancy publications, monotonic publication succession, stored
  payload replay, and deterministic prefix/fork audit.

It does not change `OccupancyPublicationHistory` schema 1. A fixed-trust
history remains fixed, loads unchanged, and is never upgraded implicitly.

## Creation and caller anchors

Creation requires:

- one valid root `OccupancyPublication` and its exact payload bytes;
- one complete, valid source `ServiceTrustHistory`;
- caller-pinned stream and publisher service IDs;
- caller-pinned trust root and current trust head IDs; and
- the caller-pinned root publication ID.

The root publication must have sequence 1, no parent, and use the source trust
history's current bundle. Creation replays the publication under that exact
bundle and copies the complete authorized trust history into the new
directory. The source directory is not referenced afterwards.

Every later open requires the same stream, publisher, trust root, publication
root, and publication head. Trust freshness is anchored in exactly one of two
ways:

1. a caller-retained expected trust-head bundle ID; or
2. a complete `ServiceTrustCheckpoint` plus its caller-retained checkpoint
   ID.

The checkpoint overload verifies the checkpoint against the embedded trust
history after complete history replay. A valid old checkpoint remains old;
applications must retain the newest accepted head or checkpoint outside the
history's rollback domain.

## Authorized trust rotation

`rotate_trust(successor, authorization, expected_trust_head)` accepts the
single-signature policy used by service-trust schemas 1 and 2.

`rotate_trust(successor, authorization_set, expected_trust_head)` accepts the
canonical quorum policy used by service-trust schema 3. The existing trust
implementation verifies:

- exact parent and successor bundle identities;
- monotonic bundle sequence;
- active rotation-authorized signer keys;
- unique signer keys;
- the configured minimum signature count; and
- distinct signer services when required by policy.

The operation acquires the outer history's cross-process writer lock, reopens
both chains under the caller's expected heads, and appends the authorized
successor to the embedded trust history. Private keys and signing seeds are
never stored.

The embedded trust append is its own commit point. If a process stops after
that commit but before the in-memory outer object is refreshed, the directory
already contains the successor. Reopen with the retained successor bundle ID
or its signed checkpoint; retrying with the stale predecessor ID fails.

## Publication under rotating trust

`publish(publication, payload, expected_publication_head,
expected_trust_head)` requires both caller-retained heads. The publication
must:

- extend the exact current publication;
- use the exact current trust bundle, not merely any historical bundle;
- retain stream, publisher, timeline, and workspace-frame identities;
- carry the next positive publisher sequence; and
- authenticate the exact supplied bytes under an active
  publication-authorized Ed25519 key.

Loading resolves every publication's recorded `trust_bundle_id` to the
corresponding historical bundle and verifies it there. Trust-bundle positions
across the publication chain must be nondecreasing. Thus old publications
remain independently verifiable after a key is retired, while a writer cannot
switch back to an older trust snapshot.

One trust bundle may authenticate several consecutive publications. Several
authorized trust rotations may also occur between publications. Only a
publication append requires the current bundle.

## Lookup, replay, and dual-chain audit

`publication`, `current_publication`, and `verify(publication_id,
evaluation_tick)` expose bounded historical lookup. Verification uses the
publication's exact stored payload and the exact historical bundle it names.

`audit_rotating_occupancy_publication_histories(first, second)` compares:

- the embedded authorized trust histories; and
- the immutable occupancy publication records.

It reports `Identical`, `FirstExtendsSecond`, `SecondExtendsFirst`, or
`Forked` independently for each chain, with separate common-prefix counts and
IDs. `fork_detected` is true when either chain forks. Both inputs must share
the pinned stream, publisher, trust root, and publication root. The audit
compares only the supplied views; it does not discover peers, select a
canonical branch, or establish global freshness.

## Resource and filesystem behavior

`RotatingOccupancyPublicationHistoryLoadOptions` bounds publication count,
manifest/record/publication bytes, aggregate payload bytes, the complete
embedded trust replay, and continuous-occupancy payload decoding. Either
nested cancellation token cancels creation, loading, verification, rotation,
or publication at bounded checkpoints.

Creation is non-overwriting and publishes a fully replayed sibling temporary
directory. Appends use a cross-process `.writer-lock`; immutable payload and
publication files are written before the record commit. Exact orphan files
from an interrupted append may be reused, while conflicting or indirect files
are rejected. History, trust-history, record, publication, and payload
symlinks are not accepted.

## Inspection

Python entry point:

```bash
rbfsafe-inspect rotating-history \
  --expected-occupancy-stream STREAM \
  --expected-occupancy-publisher PUBLISHER \
  --expected-occupancy-trust-root TRUST_ROOT \
  --expected-occupancy-trust-head TRUST_HEAD \
  --expected-occupancy-root PUBLICATION_ROOT \
  --expected-occupancy-head PUBLICATION_HEAD \
  --occupancy-evaluation-tick 31
```

Replace `--expected-occupancy-trust-head` with `--trust-checkpoint FILE` and
`--expected-trust-checkpoint CHECKPOINT_ID` to use the signed-checkpoint open
contract. A comparison additionally requires
`--compare-occupancy-history`, `--expected-compare-occupancy-trust-head`, and
`--expected-compare-occupancy-head`.

Native positional inspection uses:

```bash
rbfsafe-inspect rotating-history STREAM PUBLISHER TRUST_ROOT TRUST_HEAD \
  PUBLICATION_ROOT PUBLICATION_HEAD 31
```

For a checkpoint anchor, replace `TRUST_HEAD` with two arguments:
`CHECKPOINT_FILE CHECKPOINT_ID`. Append
`COMPARISON_DIRECTORY COMPARISON_TRUST_HEAD COMPARISON_PUBLICATION_HEAD` to
audit two views.

## Safety boundary

Successful signature verification proves that an authorized key signed the
exact retained occupancy bytes under the replayed trust policy. It does not
prove:

- that the retained trust or publication head is globally newest;
- that a remote peer saw the same chain;
- that clocks, perception, prediction, robot calibration, or deployment
  frames are trustworthy;
- that swept bounds describe physical motion; or
- that a controller executed, or may execute, any command.

All histories, records, verifications, and audits remain `Unknown` evidence
and return `authorizes_execution == false`.

See the [schema-1 directory format](rotating-occupancy-publication-history-format.md),
the [service trust-history format](service-trust-history-format.md), and the
[authenticated occupancy publication contract](authenticated-occupancy-publication.md).
