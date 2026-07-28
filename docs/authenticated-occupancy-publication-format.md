# Authenticated occupancy publication format

## Schema 1

An authenticated occupancy publication is one checksummed UTF-8 JSON file:

```json
{
  "checksum": "<64 lowercase hex>",
  "format": "rbfsafe-continuous-fleet-occupancy-publication",
  "library_version": "4.2.0",
  "payload": {
    "algorithm": 1,
    "authentication_tag": "<128 lowercase hex>",
    "id": "<64 lowercase hex>",
    "occupancy_bundle_id": "<64 lowercase hex>",
    "parent_publication_id": "",
    "payload_bytes": "14171",
    "payload_digest": "<64 lowercase hex>",
    "publisher_key_id": "<64 lowercase hex>",
    "publisher_sequence": "1",
    "publisher_service_id": "fixture-occupancy-publisher",
    "storage_schema": 1,
    "stream_id": "fixture-cell-occupancy-stream-v1",
    "timeline_id": "fixture-cell-clock-v1",
    "trust_bundle_id": "<64 lowercase hex>",
    "valid_from_tick": "0",
    "valid_through_tick": "32",
    "workspace_frame_id": "fixture-cell-world"
  },
  "schema": 1
}
```

The top-level and payload objects have exact field sets. Unknown, missing, or
duplicate fields are rejected. Unsigned 64-bit values are canonical decimal
strings so JSON number precision cannot change their meaning. `algorithm=1`
is Ed25519. Identifiers are bounded to 256 bytes and cannot contain control
characters.

`checksum` is SHA-256 of the compact canonical JSON serialization of
`payload`, including the authentication tag. It detects storage corruption;
it is not a trust decision. `library_version` records the writer and is not
part of the publication identity.

## Identities and signature

`id` is SHA-256 of the compact canonical JSON object containing every payload
field except `id` and `authentication_tag`. The authentication tag is excluded
so the stable statement identity exists before signing and is independent of
the signature encoding.

The Ed25519 message is the compact canonical JSON object:

```json
{
  "domain": "rbfsafe-continuous-fleet-occupancy-publication-signature-v1",
  "publication_id": "<id>"
}
```

Changing any signed semantic field changes the publication ID and therefore
invalidates the signature. Changing only the signature preserves the
statement ID but fails cryptographic verification.

`VerifiedOccupancyPublication::id` is a separate SHA-256 identity over the
publication metadata plus the caller-supplied `evaluation_tick` and
`publication_id`. It records one successful verification context but is not
persisted by schema 1.

`payload_digest` is SHA-256 of the exact occupancy file bytes, and
`payload_bytes` is their exact length. Verification parses the same in-memory
byte sequence used for these comparisons, preventing a second-path read from
substituting different bytes. It additionally requires:

- `occupancy_bundle_id` to equal the decoded bundle ID;
- `timeline_id` and `workspace_frame_id` to equal the decoded fleet report;
  and
- every deployment trajectory to cover the complete closed interval
  `[valid_from_tick, valid_through_tick]`.

## Stream rules

- `publisher_sequence` is positive.
- Sequence 1 requires an empty `parent_publication_id`.
- Later sequences require a 64-character SHA-256 parent ID.
- Successor validation requires exact `previous + 1`, exact parent linkage,
  and unchanged stream, publisher service, timeline, and workspace frame.

Trust-bundle rotation is represented by changing `trust_bundle_id` and
authenticating the successor with its newly pinned bundle. Schema 1 does not
embed a trust history or infer the current trust head.

## Persistence and limits

The reader rejects symbolic links and non-regular files, enforces a
caller-provided byte limit (default 1 MiB), validates exact fields, checksum,
schema, identity, and structural signature encoding. Cryptographic signature
and external payload verification occur only in
`verify_continuous_fleet_occupancy_publication`.

Saving validates the value, writes a uniquely named sibling temporary file,
loads and checks that staged file, and then publishes it by rename. Existing
destinations are rejected by default. Explicit overwrite accepts only a
regular non-symbolic-link file and stages the old file until the replacement
is published.

Unknown schemas or algorithms return `IncompatibleFormat`; malformed known
documents return `CorruptData`, `IdentityMismatch`, `ResourceLimit`, or
`IoError` as appropriate.

## Fixed fixture

`data/occupancy_publication_schema1` authenticates the exact
`data/continuous_fleet_occupancy_schema2/occupancy.json` bytes. Its stable
identities are:

- publication:
  `90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251`;
- trust bundle:
  `89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d`;
- publisher key:
  `db69491130c011ccd19cb5d0b86259e6cc627840f0dd83f5b55ce7a37ee494dd`;
- occupancy bundle:
  `6030e3574db5634f60b6cf04ffc325077f944ef23d256ef7cc937fe857dce8d0`;
- exact payload SHA-256:
  `d5c566782ab97847dcef150b81cdaeb73c27192bfd8c12a7e34ea0787d010c2f`;
  and
- verification at tick 16:
  `9910e71348c6b69609bb2026e7a7f926d27bca243bc2140a0259b7ece9d8fe09`.

The fixture stores public trust material and a signature, never the
demonstration private seed.
