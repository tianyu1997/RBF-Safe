# Verifiable provenance bundle schema 1

Schema 1 is a single UTF-8 JSON document with:

- `format`: `rbfsafe-verifiable-provenance-bundle`;
- `schema`: numeric `1`;
- `library_version`: writer version, informational only;
- `payload`: the identity-bearing bundle object;
- `checksum`: lowercase SHA-256 of the compact canonical JSON encoding of
  `payload`.

All 64-bit integers inside `payload` are unsigned decimal strings. Enum values
are bounded JSON numbers. Public keys and signatures are lowercase
hexadecimal. Object keys are canonically ordered by the RBF-Safe JSON encoder;
policy lists and report ID lists are sorted and duplicate-free.

## Payload

The payload contains:

- bundle `storage_schema`, deterministic `id`, exact `trust_bundle_id`, and
  `trust_bundle_sequence`;
- the complete subject `ServicePublicKey`;
- one complete `hardware_policy`;
- one complete `freshness_policy`;
- `hardware_statements`, ordered by sequence and ID;
- `time_assertions`, ordered by source service, source key, source sequence,
  and ID.

The bundle ID binds the subject key, policy IDs, ordered statement/assertion
IDs, trust bundle, sequence, and storage schema. Each nested policy,
statement, and assertion has an independently recomputed deterministic ID.
Statement and assertion signatures are verified only when replayed against the
exact caller-pinned `ServiceTrustBundle`.

## Compatibility and limits

There is no schema-0 or legacy-cache conversion. Unknown top-level or payload
schemas fail with `IncompatibleFormat`. Malformed JSON, identity/checksum
mismatch, invalid nested values, and indirect input paths fail closed.

`VerifiableProvenanceBundleLoadOptions` independently bounds:

- hardware statement count;
- time assertion count;
- entries in each policy list;
- total file bytes;
- cancellation.

The format stores no private key, raw vendor attestation blob, trusted clock,
geometric certificate, or execution authorization. Adapter evidence is
represented only by its exact digest and explicitly pinned normalization
metadata.
