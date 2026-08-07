# Occupancy publication histories

RBF-Safe 4.3 adds a bounded local history for authenticated continuous-fleet
occupancy publications. A history preserves exact occupancy bytes, their
Ed25519-signed publication envelopes, and an immutable record chain under one
caller-pinned stream, publisher service, trust bundle, timeline, and workspace
frame.

The history solves three local problems:

1. replay every accepted publication from stored bytes;
2. reject stale writers with an expected-head check; and
3. compare two observed histories and report whether one extends the other or
   they fork.

It is not a transport, replicated consensus service, trusted timestamp,
globally fresh stream head, or execution permit.

## Trust model

Creating or opening a history requires all of these caller pins:

- stream ID;
- publisher service ID;
- exact trust-bundle ID;
- root publication ID; and
- expected current publication ID when opening.

The history stores the exact public trust bundle and only accepts publications
that use that same bundle. Publish-key changes are possible only when both keys
already belong to the pinned bundle and satisfy its publish/sequence policy.
Trust-bundle rotation starts a separate v4.3 history. Combining service-trust
rotation with an occupancy stream is intentionally deferred.

Every stored publication is independently verified at its own
`valid_from_tick`. This checks its signature, exact payload SHA-256 and length,
decoded occupancy-bundle identity, timeline, frame, trajectory coverage, and
parent. Verifying a publication for operational use still requires a
caller-supplied evaluation tick:

```cpp
auto history = require(rbfsafe::OccupancyPublicationHistory::open(
    "occupancy-history",
    expected_stream,
    expected_publisher,
    expected_trust_bundle,
    expected_root,
    expected_head));

auto verified = require(history.verify(expected_head, evaluation_tick));
```

The result remains `EvidenceLevel::Unknown` and
`authorizes_execution() == false`.

## Creating and appending

Create a history from an authenticated root publication and the exact
occupancy file that it signs:

```cpp
auto history = require(rbfsafe::OccupancyPublicationHistory::create(
    "occupancy-history",
    root_publication,
    "occupancy.json",
    trust_bundle,
    expected_stream,
    expected_publisher,
    trust_bundle.id(),
    root_publication.id));
```

The root must have publisher sequence 1 and an empty parent. The destination
must not exist. Creation stages the complete directory beside the destination,
replays it, and publishes it with one directory rename.

Append with the caller-retained head:

```cpp
auto record = require(history.publish(
    successor,
    "successor-occupancy.json",
    expected_head));
```

Publication takes a cross-process directory lock, reopens the history under the
expected head, verifies exact succession and stored trust, stages the payload
and publication, and commits the immutable record last. A stale expected head
fails with `IdentityMismatch`; a concurrent writer fails without waiting.

The object is refreshed after a successful append. Payload/publication files
left by an interrupted pre-commit append are non-authoritative orphans because
only a valid record chain selects active entries. A later identical append may
reuse them; inconsistent orphan bytes fail closed.

## Fork audit

`audit_occupancy_publication_histories(first, second)` requires histories with
the same pinned root and stream and returns one deterministic relation:

- `Identical`;
- `FirstExtendsSecond`;
- `SecondExtendsFirst`; or
- `Forked`.

The audit includes both heads and sizes, the common-prefix size, the last common
publication, and a deterministic audit ID. `Forked` means both observed
histories contain a valid but different successor after their common prefix.
It does not prove which branch is globally preferred.

A local comparison cannot detect a branch that was never observed. Operators
must distribute or retain authenticated head observations outside the history
directory. Likewise, replacing the entire directory with an earlier valid
prefix is detected only when the caller supplies the previously retained head.

## Resource and cancellation controls

`OccupancyPublicationHistoryLoadOptions` bounds:

- publication count;
- manifest, record, publication, and trust-bundle bytes;
- trust-bundle key count;
- total stored payload bytes; and
- all nested continuous-occupancy decode/replay budgets.

The nested occupancy cancellation token is checked during creation, directory
enumeration, replay, and append. Symbolic links are rejected for the history,
its required directories, manifests, publications, trust bundle, and active
payloads.

## Inspection

Python:

```bash
rbfsafe-inspect occupancy-history \
  --expected-occupancy-stream CELL_STREAM \
  --expected-occupancy-publisher PUBLISHER_SERVICE \
  --expected-occupancy-trust-bundle TRUST_BUNDLE_ID \
  --expected-occupancy-root ROOT_PUBLICATION_ID \
  --expected-occupancy-head HEAD_PUBLICATION_ID \
  --occupancy-evaluation-tick 31
```

Add `--compare-occupancy-history OTHER_DIRECTORY` and
`--expected-compare-occupancy-head OTHER_HEAD` to report the prefix/fork
relation.

Native:

```bash
rbfsafe-inspect occupancy-history CELL_STREAM PUBLISHER_SERVICE \
  TRUST_BUNDLE_ID ROOT_PUBLICATION_ID HEAD_PUBLICATION_ID 31
```

Use `-` instead of the tick to replay the history without evaluating the head
at a specific logical tick. Append a comparison directory and its expected
head to run a fork audit.

The storage contract is specified in
[Occupancy publication-history format](occupancy-publication-history-format.md).
This type deliberately fixes one trust bundle. Applications that require
authorized key retirement/replacement must create the independent
[trust-rotating occupancy publication history](rotating-occupancy-publication-history.md);
the fixed history is never upgraded or rewritten implicitly.
