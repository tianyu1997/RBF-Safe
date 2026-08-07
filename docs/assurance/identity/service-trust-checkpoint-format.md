# Service trust-checkpoint schema 1

`ServiceTrustCheckpoint` is a portable, bounded JSON statement that a
particular replayed trust history ended at an exact record and bundle. It lets
applications retain a compact signed head anchor outside the history
directory. It does not discover a root, select the newest checkpoint, or
establish wall-clock freshness.

## Document and identity

The top-level object contains:

- `format`: `rbfsafe-service-trust-checkpoint`;
- `schema`: `1`;
- writing `library_version`;
- caller-pinned `root_bundle_id`;
- `head_bundle_id`, decimal-string `head_sequence`, and `head_record_id`;
- a canonical non-empty `signatures` array; and
- deterministic checkpoint `id`.

Each signature stores the signer service/key IDs, algorithm `1` (`ed25519`),
and a 128-character lowercase hexadecimal authentication tag. The
domain-separated signature message binds the root and all three head fields.
The checkpoint ID binds the complete canonical payload including signatures.
Signers are sorted by `(service_id, key_id)` and duplicate key identities are
rejected.

## Authorization and verification

`sign_service_trust_checkpoint` accepts only an active `allow_rotate` key in
the current head bundle whose service-sequence window covers that head.
`assemble_service_trust_checkpoint` verifies every signature and enforces the
head bundle's immutable quorum/distinct-service policy.

`verify_service_trust_checkpoint(history, checkpoint, expected_checkpoint_id)`
then requires exact equality of:

1. the caller-retained checkpoint ID;
2. the history's caller-pinned root;
3. the replayed head bundle, sequence, and record; and
4. the canonical reassembled checkpoint and all signature policy checks.

The checkpoint-pinned `ServiceTrustHistory::open` overload first binds the
caller's root and checkpoint ID, replays the history to the checkpoint head,
then performs the same verification. No network access or trust-on-first-use
is involved.

## Persistence, limits, and rollback boundary

`save` validates and atomically publishes a sibling temporary file; overwrite
is opt-in. `load` rejects symlinks, reads once under a byte limit, accepts only
schema 1, bounds the signature count, and rechecks canonical identity.
Defaults are 100,000 signatures and 4 MiB.

A correctly signed old checkpoint remains cryptographically valid. Detecting
that it is stale requires the application to retain or distribute the newer
checkpoint ID through an authenticated channel. If the history, checkpoint,
root ID, and expected checkpoint ID are rolled back together, RBF-Safe cannot
detect the rollback. Checkpoints also do not prove wall-clock time,
transparency-log inclusion, legal non-repudiation, or robot execution safety.

The fixed fixture is
`data/service_trust_checkpoint_schema1/checkpoint.json`. It binds root
`e9126145264ac126845b17db5db782a43a4816d5d4ca3a9d4f9462d874e7b89b`
to head
`d31c074a89a038167c34b1a65934186c0696aff196165dbdac67d0839cd34fb6`
with two distinct-service signatures. Its checkpoint ID is
`9cbf2bfad11354201ec0eb79dd1f11cf78925e4ea917cc3a7e5d15f6307a2e24`.
The fixture contains public interoperability data only.
