# Occupancy publication-history format

RBF-Safe 4.3 writes occupancy publication-history schema 1 as a versioned
directory:

```text
manifest.json
trust-bundle.json
records/
  00000000000000000001-<record-id>.json
publications/
  00000000000000000001-<publication-id>.json
payloads/
  00000000000000000001-<payload-sha256>.bin
```

Sequence prefixes are fixed-width 20-digit unsigned decimals. Filenames,
record contents, publication sequence, and directory order must agree.

## Manifest

`manifest.json` has the exact top-level fields:

```json
{
  "format": "rbfsafe-occupancy-publication-history",
  "identity": "<sha256>",
  "library_version": "4.3.0",
  "payload": {
    "publisher_service_id": "<service>",
    "root_publication_id": "<sha256>",
    "storage_schema": 1,
    "stream_id": "<stream>",
    "timeline_id": "<timeline>",
    "trust_bundle_id": "<sha256>",
    "workspace_frame_id": "<frame>"
  },
  "schema": 1
}
```

`identity` is SHA-256 of the compact canonical JSON encoding of `payload`.
The manifest intentionally stores only immutable root and stream constraints;
the record chain determines the current head. Opening never trusts a head read
from the directory: the caller must supply the expected head.

`trust-bundle.json` uses the independently versioned service-trust-bundle
format and must reproduce `trust_bundle_id`. Schema 1 does not rotate this
bundle.

## Immutable records

Each record document has `format`, `schema`, `library_version`, `checksum`, and
`payload`. The payload has exactly:

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
The record ID is SHA-256 of compact canonical payload JSON with the `id` field
removed. It therefore binds the publication ID, exact Ed25519 signature tag,
payload digest/length, sequence, and previous record.

The root record has sequence 1 and an empty `parent_record_id`. Every later
record has sequence `previous + 1` and names the exact prior record.

## Publications and payloads

Publication files use
`rbfsafe-continuous-fleet-occupancy-publication` schema 1. The record must
match the publication's sequence, ID, signature tag, payload digest, and
payload length.

Payload files contain the exact bytes signed by the publication. They are not
normalized, parsed, or rewritten during history creation. Each active payload
is decoded under bounded continuous-occupancy options, and replay must match:

- SHA-256 and byte length;
- occupancy-bundle ID;
- timeline and workspace frame;
- every trajectory's coverage of the signed validity window;
- stream, publisher, trust bundle, sequence, and parent; and
- the Ed25519 signature under an active publish-authorized key.

All publications must retain the manifest stream, publisher, trust bundle,
timeline, and frame.

## Load and replay order

A schema-1 reader:

1. rejects an indirect history directory and validates the caller pins;
2. loads and authenticates the manifest identity;
3. loads the exact pinned trust bundle;
4. enumerates bounded record files and sorts by sequence then ID;
5. requires one linear record for every sequence starting at 1;
6. loads the publication and payload selected by every record;
7. verifies signatures, exact bytes, decoded identity, and succession;
8. reconstructs the head; and
9. compares the reconstructed head with the caller-retained expected head.

Duplicate or divergent records at one sequence make the directory corrupt or
forked and fail loading. Fork comparison between two individually valid
directories is performed by the public audit API instead.

## Commit and crash behavior

Creation writes a complete sibling temporary directory, replays staged
content, then renames it to a previously absent destination.

Append uses a `.writer-lock` directory for cross-process exclusion. It writes
or validates the immutable payload and publication first and publishes the
record last. The record is the commit point. Temporary names contain `.tmp-`
or `.verify-` and never authorize history entries.

An interrupted append may leave an unreferenced payload or publication. Readers
ignore data not selected by a committed record. A retry accepts an orphan only
when its exact bytes or publication ID/signature match; otherwise it reports an
identity error.

## Security boundaries

The format is append-oriented but a filesystem owner can delete or replace
files. Caller-retained root and head pins are therefore mandatory. A retained
head detects rollback; comparing independently retained valid histories
detects an observed fork. Schema 1 provides no remote replication, gossip,
quorum choice, trusted time, or globally authoritative head.

Every record, history, verification, and audit value is `Unknown` evidence and
non-authorizing.
