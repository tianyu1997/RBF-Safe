# Trust-rotating occupancy publication-history format

RBF-Safe 4.5 writes
`rbfsafe-rotating-occupancy-publication-history` schema 1 as:

```text
manifest.json
trust-history/
  manifest.json
  records/
  bundles/
records/
  00000000000000000001-<record-id>.json
publications/
  00000000000000000001-<publication-id>.json
payloads/
  00000000000000000001-<payload-sha256>.bin
```

`trust-history/` is a complete service-trust-history schema 1 or 2 directory.
Its own format rules, bundle files, authorization records, resource limits,
and expected-head/checkpoint verification remain authoritative.

All sequence prefixes are fixed-width 20-digit unsigned decimals.

## Manifest

`manifest.json` has exactly:

```json
{
  "format": "rbfsafe-rotating-occupancy-publication-history",
  "identity": "<sha256>",
  "library_version": "4.5.0",
  "payload": {
    "publisher_service_id": "<service>",
    "root_publication_id": "<sha256>",
    "storage_schema": 1,
    "stream_id": "<stream>",
    "timeline_id": "<timeline>",
    "trust_root_bundle_id": "<sha256>",
    "workspace_frame_id": "<frame>"
  },
  "schema": 1
}
```

`identity` is SHA-256 of compact canonical JSON for `payload`. The manifest
contains only immutable roots and stream constraints. It intentionally stores
neither current head: readers reconstruct both chains and compare them with
caller-retained trust and publication anchors.

## Occupancy records

Records reuse `rbfsafe-occupancy-publication-history-record` schema 1. Each
document contains `format`, `schema`, `library_version`, `checksum`, and this
exact payload:

```json
{
  "authentication_tag": "<128 lowercase hex>",
  "id": "<sha256>",
  "parent_record_id": "<sha256-or-empty>",
  "payload_bytes": "<uint64 decimal>",
  "payload_digest": "<sha256>",
  "publication_id": "<sha256>",
  "sequence": "<uint64 decimal>",
  "storage_schema": 1
}
```

The checksum is SHA-256 of compact canonical payload JSON including `id`.
The record ID is SHA-256 of the same canonical payload without `id`.

The first record has sequence 1, no parent record, and names the manifest root
publication. Every successor has sequence `previous + 1` and names the exact
previous record.

## Publication and payload binding

Each publication file is an independently checksummed
`rbfsafe-continuous-fleet-occupancy-publication` schema-1 document. Each
payload is the exact signed byte sequence. A record must match the
publication's ID, sequence, signature, payload SHA-256, and byte length.

Every publication must retain the manifest stream, publisher, timeline, and
workspace frame. Its `trust_bundle_id` must resolve in the embedded trust
history. Bundle positions across occupancy records must be nondecreasing.
The root publication uses the trust head supplied during creation; new
publication commits use the current embedded trust head.

## Replay order

A schema-1 reader:

1. rejects an absent, non-directory, or symlink outer path;
2. reads the bounded manifest and verifies its exact fields and identity;
3. compares stream, publisher, trust root, and publication root with caller
   pins;
4. rejects missing or indirect `trust-history`, `records`, `publications`, or
   `payloads` directories;
5. opens the embedded trust history under the caller's expected trust head,
   or later verifies the caller's signed checkpoint;
6. boundedly enumerates and sorts occupancy records by sequence and ID;
7. requires one exact linear record for every sequence starting at 1;
8. loads every named publication and payload from the deterministic filename;
9. verifies record, byte, decoded occupancy, stream, validity-window, parent,
   and historical Ed25519 trust bindings;
10. rejects any backward trust-bundle transition;
11. reconstructs the publication head and compares it with the caller's
    expected head; and
12. when checkpoint-anchored, verifies its root, head, record, signatures,
    quorum, and caller-retained checkpoint ID.

Unknown schemas fail. Truncated, malformed, oversized, indirectly referenced,
or identity-inconsistent content is rejected.

## Commit and recovery behavior

Creation clones and replays the caller-pinned trust history in a private
staging directory, builds the complete outer sibling temporary directory, and
renames it only when all root content verifies. An existing destination is
never overwritten.

Both trust and occupancy appends take the outer `.writer-lock`. Trust rotation
delegates its commit to the embedded service trust history. Occupancy append
writes or exactly validates the payload and publication first, then publishes
the immutable record as the commit point. Compact dot-prefixed staging names
(`.record-*`, `.bundle-*`, `.payload-*`, `.publication-*`, and `.verify-*`)
avoid amplifying Windows path lengths; nested serializers may additionally use
`.tmp-*`. None of these staging files become records.

An interrupted occupancy append can leave unreferenced payload/publication
files. Readers ignore files not named by committed records. A retry reuses
only an exact byte- or identity-matching orphan.

## Fixed fixture

`data/rotating_occupancy_publication_history_schema1` contains two trust
bundles and two publications:

- trust root:
  `9767a5e8912af9192237924232daaf746c0107e98cc4a458ee8b773b6b0da051`;
- trust head:
  `938c63c1c1bdf8c309189c4ca93b1093aa68f2b72adba49ba2dbe81a940d1517`;
- publication root:
  `fc449d962cec661b52277926efa2a1d0f075a389fd488eeb7b7f9f5bd441429b`;
- publication head:
  `829328cdffeca14a187430a3964651ef37ec249669dcbcf94c92f5a0d564b601`.

The root signing key is retired by an authorized rotation; the successor
publication verifies under the new key, while the root remains verifiable
under its historical bundle. Demonstration private seeds are present only in
quickstart source and are not stored in the fixture.

## Non-authorizing status

Checksums detect accidental or malicious byte changes relative to retained
identities. Ed25519 signatures and trust authorizations authenticate retained
statements. Caller pins detect rollback only relative to separately retained
anchors. No file, record, successful replay, or audit authorizes execution;
all remain `Unknown`.
