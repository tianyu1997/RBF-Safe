# Artifact transfer journal schema 1

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
- `schema`: `1`;
- the writing `library_version`;
- decimal-safe record count;
- `current_record_id` and `journal_id`; and
- SHA-256 of the exact `records.json` bytes.

`records.json` uses format `rbfsafe-artifact-transfer-records`, schema 1, and
an ordered `records` array. Every record contains a one-based decimal-string
sequence, its parent ID, its deterministic ID, and one
`VerifiedArtifactTransfer`.

The transfer stores operation, request/response, service, whole-memory,
artifact lifecycle, exact byte digest/count, media type, service sequence,
authentication algorithm, and transfer-attestation ID. It deliberately does
not store payload bytes, HMAC keys, authentication tags, or a complete remote
response.

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

The loader verifies journal integrity, not remote authenticity. Full service
authentication occurs before append while the response, payload, trusted key
ID, and key are available. The compact journal is therefore an audit index,
not a substitute for retaining externally governed evidence when policy
requires later independent cryptographic re-verification.

The fixed cross-platform fixture is
`data/artifact_transfer_journal_schema1`. Its journal ID is
`28a05f88667cf3c46fe2748cc9aff0e4a7aaebd3d267d649d708077536ac74bd`.
