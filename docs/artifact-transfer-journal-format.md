# Artifact transfer journal schemas 1 and 2

`ArtifactTransferJournal` stores a deterministic, append-only index of
successfully verified remote artifact transfers. It is independent of Atlas,
safety-memory, fleet-schedule, artifact-attestation, and policy schemas.

## Directory layout

```text
journal/
  manifest.json
  records.json
```

`manifest.json` contains:

- `format`: `rbfsafe-artifact-transfer-journal`;
- `schema`: `2` for current writers; readers also accept legacy schema `1`;
- the writing `library_version`;
- decimal-safe record count;
- `current_record_id` and `journal_id`; and
- SHA-256 of the exact `records.json` bytes.

`records.json` uses format `rbfsafe-artifact-transfer-records`, schema 2, and
an ordered `records` array. Every record contains a one-based decimal-string
sequence, its parent ID, its deterministic ID, and one
`VerifiedArtifactTransfer`.

The transfer stores operation, request/response, service, whole-memory,
artifact lifecycle, exact byte digest/count, media type, service sequence,
authentication algorithm, and transfer-attestation ID. Schema 2 additionally
stores the public `verification_key_id` and `trust_bundle_id` used by a
successful Ed25519 offline verification. It deliberately does not store
payload bytes, HMAC/private keys, authentication tags/signatures, or a
complete remote response.

Schema 1 records omit the two public-key provenance fields. Their identities
remain byte-for-byte compatible with 3.6; loading does not synthesize those
fields. Current schema-2 HMAC and unauthenticated transfer IDs also preserve
their 3.6 identity because empty provenance fields are excluded from the
identity input.

## Identity chain

The transfer ID hashes all transfer fields. A record ID hashes its sequence,
parent, and transfer ID. Record zero has an empty parent; each later record
must name the preceding record. The current record and non-empty journal
identity are the final record ID. The empty journal has a stable domain-
separated identity.

`append` requires the exact current head observed by the caller. This prevents
silent in-process lost updates, but the journal is not a multi-process
transaction store. Applications with concurrent writers must serialize
publication externally or place the journal behind one writer service.

## Save and load

Saving writes a sibling temporary directory, writes both JSON documents,
computes the payload checksum, and then publishes the directory. Existing
destinations are rejected unless `SaveOptions::overwrite` is explicit.

Loading checks schema, manifest identities, record and byte budgets, payload
checksum, all enum and string bounds, transfer identities, record identities,
sequence order, parent continuity, and the declared head. Unknown schemas
return `IncompatibleFormat`; checksum or history failures return
`CorruptData`; configured limits return `ResourceLimit`.

The manifest is capped at 1 MiB. `maximum_payload_bytes` caps `records.json`;
each file is read once at its inspected length, rejects growth/truncation
during that read, and is parsed only after the checksum and bound pass.

The loader verifies journal integrity, not remote authenticity. Full service
authentication occurs before append while the response, payload, signature
or tag, trust anchor, and verification key are available. A schema-2 record
shows which public key and bundle were used, but it intentionally omits the
signed response and payload and therefore cannot repeat verification alone.
The compact journal remains an audit index, not a complete evidence archive.

The fixed cross-platform fixture is
`data/artifact_transfer_journal_schema1`. Its journal ID is
`28a05f88667cf3c46fe2748cc9aff0e4a7aaebd3d267d649d708077536ac74bd`.
The schema-2 public-key fixture is
`data/artifact_transfer_journal_schema2`. Its journal ID is
`43197ea62038f190add715bfcb0d60d9d14c118f2713e540e2716fd9daa42800`.
