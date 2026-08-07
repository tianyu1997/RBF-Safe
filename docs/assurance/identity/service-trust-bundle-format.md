# Service trust-bundle schemas 1, 2, and 3

`ServiceTrustBundle` is a bounded JSON document containing public Ed25519
verification/rotation policy and a deterministic linear identity. RBF-Safe
3.9 reads schemas 1, 2, and 3. `create` retains the schema-2 single-signer
contract. `create_with_rotation_policy` creates a schema-3 root with an
explicit quorum policy. Successors preserve the predecessor schema exactly;
plain save preserves every loaded schema and historical ID.

## Document

The top-level object contains:

- `format`: `rbfsafe-service-trust-bundle`;
- `schema`: `1`, `2`, or `3`;
- writing `library_version`;
- decimal-string positive `sequence`;
- `parent_id`, empty only for sequence 1;
- deterministic `id`; and
- a non-empty `keys` array; and
- in schema 3, `rotation_policy`.

Keys are canonically sorted by `(service_id, id)`. Each entry stores:

- deterministic SHA-256 key `id`;
- bounded `service_id`;
- algorithm `1` (`ed25519`);
- 64-character lowercase hexadecimal public key;
- decimal-string inclusive `valid_from_sequence`;
- decimal-string `valid_through_sequence`, where `0` means unbounded;
- state `0` pending, `1` active, `2` retired, or `3` revoked;
- `allow_fetch` and `allow_publish` booleans; and
- in schema 2, the `allow_rotate` boolean.

No seed, private key, signature, endpoint, credential, or network policy is
stored. Successor signatures belong to trust-history rotation records rather
than the bundle file.

Schema 3 stores:

- positive `minimum_signatures`; and
- `require_distinct_services`, which requires the accepted signers to cover
  at least `minimum_signatures` distinct service IDs.

The policy is immutable for the whole bundle chain and is included in every
schema-3 bundle ID. Each signature must still come from a unique active
rotation-capable predecessor key whose sequence window covers the successor.
The library counts key identities, never repeated signature entries.

## Identities

A service key ID hashes a domain separator plus canonical service ID,
algorithm, and public-key material. Lifecycle state, validity end, and
permissions are bundle policy and do not rename the cryptographic key.

The bundle ID hashes its schema, sequence, parent ID, and every complete
canonical key record. Schema-1 IDs therefore retain their 3.7 values.
Schema-2 IDs include `allow_rotate`. Schema-3 IDs additionally include the
complete rotation policy. A successor's `parent_id` must equal the exact
predecessor bundle ID. This supplies deterministic integrity and chain
continuity; it does not authenticate a root.

Existing keys cannot be removed from a successor. Public material, service,
initial validity sequence, and all operation permissions are immutable.
Lifecycle transitions and a first finite upper bound are monotonic.
Rotating between schemas is rejected. In particular, a schema-2 chain is
never silently upgraded to quorum semantics.

## Schema-1 compatibility

A schema-1 key loads with `allow_rotate=false`. The reader never infers
rotation authority from fetch/publish permission, active state, HMAC identity,
or possession of an Ed25519 private key. A schema-1 bundle remains valid for
offline transfer verification but cannot authorize a signed successor or
become the root of a `ServiceTrustHistory`.

To adopt signed history, deployments must create and authenticate a new
schema-2 or schema-3 root out of band, pin its exact ID, and deliberately
grant rotation permission. There is no implicit migration that preserves
root authority.

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

Fixed cross-platform fixtures are:

- `data/service_trust_bundle_schema1/bundle.json`, whose bundle ID is
  `b6f6e30bc2245e64a519c8a02e61063bdf2fe1d8dc5ee35d980f46e1e4aa584d`;
  and
- `data/service_trust_bundle_schema2/bundle.json`, a rotation-capable root
  also used by the schema-1 trust-history fixture, whose ID is
  `9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd`;
  and
- `data/service_trust_bundle_schema3/bundle.json`, a two-signature,
  distinct-service root used by the schema-2 history fixture, whose ID is
  `e9126145264ac126845b17db5db782a43a4816d5d4ca3a9d4f9462d874e7b89b`.

All contain only public interoperability data and are not authorized
production trust roots.
