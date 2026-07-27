# Service trust-history schema 1

`ServiceTrustHistory` is an immutable directory that replays an explicitly
caller-pinned schema-2 root and every signed successor. It adds local
publication ordering and auditability; it is not a remote trust-distribution
protocol or a self-authenticating root.

## Layout

```text
service-trust-history/
  manifest.json
  bundles/
    00000000000000000001-<bundle-id>.json
    00000000000000000002-<bundle-id>.json
  records/
    00000000000000000001-<record-id>.json
    00000000000000000002-<record-id>.json
```

The schema-1 manifest stores:

- `format`: `rbfsafe-service-trust-history`;
- `schema`: `1`;
- writing `library_version`;
- `root_bundle_id`; and
- `identity`, the SHA-256 of the canonical format/schema/root payload.

The manifest intentionally contains no mutable head. The caller supplies an
expected head to every `open` and `publish`.

## Rotation records

Every record uses format `rbfsafe-service-trust-rotation-record`, schema 1,
a decimal-string sequence, deterministic ID, parent record ID, event type,
bundle ID, and an `authorization` object or `null`.

Record 1 is `root_pinned`, has an empty parent, names the exact root bundle,
and has no authorization. Each later `successor_authorized` record names the
previous record as parent and embeds a complete
`ServiceTrustBundleAuthorization`.

The authorization uses format
`rbfsafe-service-trust-bundle-authorization`, schema 1, and stores:

- deterministic authorization ID;
- predecessor/successor bundle IDs and decimal-string sequences;
- signer service ID and key ID;
- algorithm `1` (`ed25519`); and
- a 128-character lowercase hexadecimal Ed25519 signature.

Its domain-separated message binds every field except the authorization ID
and signature. The authorization ID additionally binds the exact signature.
No secret key is stored.

## Replay and publication

`open(directory, expected_root, expected_head, options)`:

1. rejects indirect/symlinked history metadata and data directories;
2. verifies the manifest and exact caller-pinned root;
3. bounds and sorts immutable record filenames;
4. loads the exactly named schema-2 bundle for every record;
5. verifies sequence, record parent, bundle parent, deterministic IDs,
   monotonic key policy/state, and every Ed25519 successor signature;
6. enforces per-bundle, aggregate-key, metadata-byte, bundle-byte, and
   cancellation budgets; and
7. rejects a replayed head unequal to the caller's expected head.

`publish` creates a cross-process `.writer-lock`, reopens and replays the
history under the lock against the caller's expected head, verifies the
successor and its signature, writes the complete bundle, and finally publishes
the immutable rotation record. An exact orphan bundle left by interruption is
accepted on retry only after its full identity is revalidated. Existing
record names are never overwritten.

Initial creation builds a sibling temporary directory and renames it into
place only after the root bundle, root record, and manifest have been written.
Existing destinations are rejected.

## Rollback and trust boundary

Hashes, immutable names, parent links, and signatures detect modification,
reordering, removal inside a replayed chain, and unauthorized successors.
They cannot reveal that the complete directory was restored to an older
self-consistent state.

Rollback detection therefore requires the application to retain the newest
head ID outside the rollback domain and supply it on the next open/publish.
The root ID must likewise come from an authenticated out-of-band source.
Storing root and head only beside this directory is not a trust anchor.

The fixed interoperability fixture is
`data/service_trust_history_schema1`. It contains one deterministic root and
one signed successor. Its seeds/private keys are absent; deterministic seeds
exist only in test/quickstart source used to reproduce the public records.
Its pinned root, expected head, and authorization IDs are respectively:

- `9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd`;
- `4c119f290036039ce28f4c8b8d8db572a7950cdf28e907153ef4c02445afad3b`;
  and
- `68debbfc4156e5c829d641e5b9ed4aeab04174454d655d5558783f81fc8711d3`.
