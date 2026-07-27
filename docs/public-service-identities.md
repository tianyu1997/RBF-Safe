# Public-key service identities

RBF-Safe 3.7 added `RBFSafe::identity`, caller-pinned Ed25519 service
identities, monotonic trust-bundle structure, and offline verification for the
transport-neutral artifact exchanges introduced in 3.6. RBF-Safe 3.8 adds
explicit rotation permission, exact-successor signatures, and replayable local
trust history. It preserves every memory, lifecycle, request, response, byte,
media-type, sequence, resource, and cancellation check from the HMAC path.

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
detects modification after the ID has been pinned, but the ID does not
establish who authorized the root. The application must obtain and pin an
expected root bundle ID through an authenticated out-of-band process. Loading
a bundle from the same channel as the artifact and trusting its self-declared
ID is unsafe.

Each schema-2 successor can be authorized by one active predecessor key whose
immutable policy grants `allow_rotate` and whose sequence window covers the
successor. The Ed25519 message is domain separated and binds both bundle IDs,
both sequences, and the signer service/key IDs. This proves that the pinned
predecessor key authorized that exact successor; it does not make the original
root self-authenticating.

RBF-Safe does not implement a certificate authority, Web PKI, transparency
log, remote key discovery, threshold signatures, or trust-on-first-use. It
does not fetch successor bundles or publish a caller's head anchor.
Distribution, root approval, expected-head retention, and organizational
authorization policy stay with the deployment.

## Key lifecycle

Each `ServicePublicKey` binds:

- deterministic key ID, service ID, algorithm, and 32-byte public key;
- inclusive service-sequence validity window;
- `fetch`, `publish`, and/or `rotate` permission; and
- `Pending`, `Active`, `Retired`, or `Revoked` state.

`Pending` keys cannot verify transfers or authorize rotation. `Active` keys
perform permitted operations within their sequence window. `Retired` keys
require a finite upper sequence and remain usable only for historical
transfers in that window. `Revoked` keys reject all uses, including historical
ones. Revocation is intentionally stronger than expiry.

`rotate_service_trust_bundle` creates a structural successor with sequence
`previous+1` and the previous bundle ID as parent.
`authorize_service_trust_bundle_successor` then signs that exact transition.
Existing keys cannot be removed; their service, public material, initial
sequence, and permissions are immutable. Allowed state transitions are:

```text
Pending -> Pending | Active | Revoked
Active  -> Active  | Retired | Revoked
Retired -> Retired | Revoked
Revoked -> Revoked
```

This is a linear local rotation contract, not distributed consensus.
Schema-1 bundles load with `allow_rotate=false`; RBF-Safe refuses to infer
rotation authority or initialize a signed history from them.

## Trust-history workflow

```cpp
auto authorization = authorize_service_trust_bundle_successor(
    root, successor, rotation_key.service_id, rotation_key.id, secret_key);
auto history = ServiceTrustHistory::create(
    "service-trust-history", root, caller_pinned_root_id);
auto record = history.value().publish(
    successor, authorization.value(), caller_retained_current_head);
auto replayed = ServiceTrustHistory::open(
    "service-trust-history", caller_pinned_root_id, successor.id());
```

The schema-1 directory retains every complete public bundle and every
root/successor record. Replay checks deterministic filenames and IDs, strict
sequence/parent continuity, immutable key-policy transitions, and every
Ed25519 authorization. Publication takes a cross-process writer lock and
replays under that lock against the supplied expected head before adding an
immutable bundle and record.

The manifest pins only the root. The head is deliberately supplied by the
caller on every open or publish. If an attacker restores the entire directory
to an older self-consistent copy, RBF-Safe detects it only when the caller has
retained and supplies the newer expected head. Keeping root and head beside
the history and rolling all three back together provides no rollback
resistance.

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
helpers. Trust bundles, trust histories, transfer journals, inspectors, and
verified-transfer metadata never persist a seed or private key. Production
seeds must be generated with a cryptographically secure random generator and
private keys should remain in an HSM or externally governed secret manager.
The deterministic seeds in quickstarts and fixtures exist only for
reproducibility.

The library accepts the RFC 8032 64-byte secret-key representation used by
the vendored implementation. It checks that the public half derives the
declared deterministic service-key ID before signing. Applications must
still enforce key access, backup, destruction, incident response, and
operator authorization.

## Evidence boundary

Successful offline verification proves that the pinned public key signed the
declared exchange and that the exact current catalog/lifecycle/byte checks
passed. A verified successor additionally proves authorization by one
declared rotation-capable key in the pinned predecessor. It does not:

- interpret or semantically validate the payload;
- show that the original root was authorized unless it was pinned correctly;
- prove freshness beyond caller-managed request/service sequences;
- prove whole-directory freshness without a caller-retained expected head;
- provide quorum, transparency-log, certificate-authority, or remote-discovery
  semantics;
- provide non-repudiation as a legal or organizational conclusion; or
- upgrade certificate evidence or authorize robot execution.

Load transferred bytes with their artifact-specific validating reader and
repeat ordinary robot/scene compatibility checks before reuse.
