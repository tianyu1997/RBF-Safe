# Service trust-bundle schema 1

`ServiceTrustBundle` is a bounded JSON document containing public Ed25519
verification keys and a deterministic linear rotation identity.

## Document

The top-level object contains:

- `format`: `rbfsafe-service-trust-bundle`;
- `schema`: `1`;
- writing `library_version`;
- decimal-string positive `sequence`;
- `parent_id`, empty only for sequence 1;
- deterministic `id`; and
- a non-empty `keys` array.

Keys are canonically sorted by `(service_id, id)`. Each entry stores:

- deterministic SHA-256 key `id`;
- bounded `service_id`;
- algorithm `1` (`ed25519`);
- 64-character lowercase hexadecimal public key;
- decimal-string inclusive `valid_from_sequence`;
- decimal-string `valid_through_sequence`, where `0` means unbounded;
- state `0` pending, `1` active, `2` retired, or `3` revoked; and
- `allow_fetch` and `allow_publish` booleans.

No seed, private key, signature, endpoint, credential, or network policy is
stored.

## Identities

A service key ID hashes a domain separator plus canonical service ID,
algorithm, and public-key material. Lifecycle state, validity end, and
permissions are bundle policy and do not rename the cryptographic key.

The bundle ID hashes schema, sequence, parent ID, and every complete canonical
key record. A successor's `parent_id` must equal the exact predecessor bundle
ID. This supplies deterministic integrity and rotation-chain continuity; it
does not authenticate the root. Applications must pin an expected root ID or
otherwise authorize it out of band.

## Persistence and limits

`save` writes a sibling temporary file, flushes it, and publishes only after
validation. Existing destinations are rejected unless explicit overwrite is
requested. `load` reads once under a byte cap and validates schema, field
bounds, enums, canonical ordering, duplicate keys, every key ID, and the
bundle ID.

`ServiceTrustBundleLoadOptions` defaults to 100,000 keys and 4 MiB of JSON.
Unknown schemas return `IncompatibleFormat`, malformed identities return
`CorruptData`, configured limits return `ResourceLimit`, and file failures
return `IoError`.

The fixed cross-platform fixture is
`data/service_trust_bundle_schema1/bundle.json`. Its bundle ID is
`b6f6e30bc2245e64a519c8a02e61063bdf2fe1d8dc5ee35d980f46e1e4aa584d`,
and its sole public-key ID is
`deeb721ce417eb73af26ccc9a69a2a8c4343dada00e2e6f3864084c1e328e732`.
