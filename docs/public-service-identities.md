# Public-key service identities

RBF-Safe 3.7 adds `RBFSafe::identity`, caller-pinned Ed25519 service
identities, monotonic trust-bundle rotation, and offline verification for the
transport-neutral artifact exchanges introduced in 3.6. It preserves every
memory, lifecycle, request, response, byte, media-type, sequence, resource,
and cancellation check from the HMAC path.

The implementation follows [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032.html)
Ed25519: public keys are 32 bytes, signatures are 64 bytes, and signing is
deterministic for a given key and message. RBF-Safe vendors the portable
Monocypher 4.0.2 RFC 8032 implementation. Exact source hashes, license, and
upstream provenance are in `third_party/monocypher/README.md` and
`THIRD_PARTY_NOTICES.md`. Monocypher's earlier 3.1.1 core and optional
Ed25519 files were included in a published
[Cure53 audit](https://monocypher.org/quality-assurance/MON-01-report.pdf);
that fact is provenance, not a claim that RBF-Safe or the vendored 4.0.2
release received a new independent audit.

## Trust model

A `ServiceTrustBundle` is public verification metadata. Its deterministic ID
detects accidental or malicious modification after the ID has been pinned,
but the ID does not establish who authorized the bundle. The application must
obtain and pin an expected root bundle ID through an authenticated
out-of-band process. Loading a bundle from the same channel as the artifact
and trusting its self-declared ID is unsafe.

RBF-Safe does not implement a certificate authority, Web PKI, transparency
log, remote key discovery, threshold signatures, or trust-on-first-use. It
does not fetch a successor bundle. Distribution and authorization policy stay
with the deployment.

## Key lifecycle

Each `ServicePublicKey` binds:

- deterministic key ID, service ID, algorithm, and 32-byte public key;
- inclusive service-sequence validity window;
- `fetch` and/or `publish` permission; and
- `Pending`, `Active`, `Retired`, or `Revoked` state.

`Pending` keys cannot verify transfers. `Active` keys verify permitted
operations within their sequence window. `Retired` keys require a finite
upper sequence and remain usable only for historical transfers in that
window. `Revoked` keys reject all transfers, including historical ones.
Revocation is intentionally stronger than expiry.

`rotate_service_trust_bundle` creates a successor with sequence `previous+1`
and the previous bundle ID as parent. Existing keys cannot be removed; their
service, public material, initial sequence, and permissions are immutable.
Allowed state transitions are:

```text
Pending -> Pending | Active | Revoked
Active  -> Active  | Retired | Revoked
Retired -> Retired | Revoked
Revoked -> Revoked
```

This is a linear local rotation contract, not distributed consensus. A caller
must compare the expected current parent before publishing a new authorized
bundle and must govern rollback resistance outside the file itself.

## Offline publication verification

```cpp
auto request = prepare_artifact_publish(
    memory, artifact_id, payload, "artifact-service", 31,
    "application/vnd.rbfsafe.atlas",
    ArtifactTransferAuthentication::Ed25519);
auto receipt = make_artifact_publish_receipt(request.value(), 101);
receipt = sign_artifact_publish_receipt(
    receipt.value(), service_key.id, secret_key);
auto verified = verify_artifact_publish_offline(
    memory, request.value(), receipt.value(), payload, pinned_bundle);
```

Fetch uses `sign_artifact_fetch_response` and
`verify_artifact_fetch_offline`. Verification selects the exact
service/key ID from the caller-supplied bundle, checks state, operation, and
service-sequence window, verifies the signature, and records both the key ID
and bundle ID in `VerifiedArtifactTransfer`.

The signed message is domain separated from the 3.6 HMAC message and covers
the complete transfer attestation: service/key identity, operation, request
and response IDs, artifact and payload identities, byte count, and service
sequence. HMAC and unauthenticated requests retain their 3.6 behavior and
identities.

## Private-key boundary

`Ed25519KeyPair` and signing functions are deliberately low-level integration
helpers. Trust bundles, transfer journals, inspectors, and verified-transfer
metadata never persist a seed or private key. Production seeds must be
generated with a cryptographically secure random generator and private keys
should remain in an HSM or externally governed secret manager. The
deterministic seed in quickstarts and fixtures exists only for reproducibility.

The library accepts the RFC 8032 64-byte secret-key representation used by
the vendored implementation. It checks that the public half derives the
declared deterministic service-key ID before signing. Applications must
still enforce key access, backup, destruction, incident response, and
operator authorization.

## Evidence boundary

Successful offline verification proves that the pinned public key signed the
declared exchange and that the exact current catalog/lifecycle/byte checks
passed. It does not:

- interpret or semantically validate the payload;
- show that the signer was authorized unless the bundle was pinned correctly;
- prove freshness beyond the caller-managed request/service sequences;
- provide non-repudiation as a legal or organizational conclusion;
- upgrade certificate evidence or authorize robot execution.

Load the transferred bytes with their artifact-specific validating reader and
repeat ordinary robot/scene compatibility checks before reuse.
