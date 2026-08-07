# Remote artifact service contract

RBF-Safe 3.6 added `RBFSafe::remote`, a transport-neutral contract for moving
exact safety-artifact bytes across a service boundary. The module prepares
deterministic fetch and publish requests, validates service responses, binds
them to the current `SafetyMemory` state, authenticates the complete exchange,
and emits append-only audit metadata. It does not open a socket or choose an
HTTP client.

## Byte identity precondition

The remote API gives `MemoryArtifact::content_digest` a strict meaning: it must
be the lowercase SHA-256 digest of the exact bytes being transferred. A memory
record whose content digest names a logical object, directory, or some other
canonical identity is still valid for the memory API, but it is not eligible
for `prepare_artifact_publish` or a successful fetch verification.

This opt-in restriction prevents a transport from returning bytes that merely
claim to represent the catalog entry. It follows the same basic integrity
pattern as [RFC 9530 Content-Digest](https://www.rfc-editor.org/rfc/rfc9530.html)
and the [TUF targets metadata](https://theupdateframework.io/docs/metadata/),
which bind target files to both hashes and lengths. RBF-Safe hashes the
unencoded byte span supplied to its API; HTTP content coding and representation
selection remain transport concerns.

## Publish flow

1. Register an active artifact using the exact payload SHA-256 as
   `content_digest`.
2. Call `prepare_artifact_publish(memory, artifact_id, payload, service_id,
   sequence, media_type)`.
3. Serialize the returned standard-value fields into the caller's transport
   request and send the exact payload bytes.
4. The service constructs `make_artifact_publish_receipt(request,
   service_sequence)`, then calls `authenticate_artifact_publish_receipt` with
   its externally managed HMAC key.
5. The client calls `verify_artifact_publish` using a key ID and key selected
   by trusted local configuration.
6. Optionally append the `VerifiedArtifactTransfer` to an
   `ArtifactTransferJournal` using the exact expected journal head.

Preparation checks the current whole-memory identity, artifact identity,
generation, lifecycle state, content digest, byte count, media type, and
resource limits. Verification repeats those checks against the current memory
instead of trusting the request object.

## Fetch flow

`prepare_artifact_fetch` captures the same memory and lifecycle identities plus
an explicit maximum response size. After the transport returns bytes and
metadata, the service-side adapter creates `make_artifact_fetch_response` and
authenticates it with `authenticate_artifact_fetch_response`. The client then
calls `verify_artifact_fetch`.

A fetch succeeds only if:

- the exact `SafetyMemory::identity()` has not changed;
- the artifact still exists at the captured generation and state;
- the artifact is active when the default policy is used;
- the service, request, response, artifact, media type, service sequence, byte
  count, and SHA-256 values all match;
- the bytes fit both the request cap and the local verification cap; and
- the requested authentication policy succeeds.

Cancellation is checked before preparation or verification. Network
cancellation while an operation is in flight belongs to the transport
implementation.

## Service authentication and replay binding

The default policy is `HmacSha256`. `ArtifactTransferAttestation` covers:

- service and trusted key IDs;
- fetch or publish operation;
- request and response/receipt IDs;
- artifact and payload digests;
- payload length; and
- the service's monotonic sequence value.

The HMAC is compared in constant time. Binding both request and response IDs is
intentional: an ordinary artifact-byte attestation can show that exact bytes
came from a key holder, but cannot prove that the service processed this
specific request. The transfer attestation closes that replay/acknowledgement
gap for a shared-key deployment.

`ArtifactTransferAuthentication::None` is available only when selected
explicitly in the request. Verification then rejects any supplied key or
attestation and reports `authentication=none`. This mode can detect accidental
corruption and identity mismatch, but it does not protect against a malicious
transport or service.

Shared-key authentication does not provide non-repudiation or distinguish
individual holders of the same key. RBF-Safe does not store keys. Rotation,
revocation, secret erasure, key distribution, authorization, and audit access
remain deployment responsibilities.

RBF-Safe 3.7 adds `ArtifactTransferAuthentication::Ed25519` in the separate
`RBFSafe::identity` target. Service adapters sign a response or receipt with
`sign_artifact_fetch_response` or `sign_artifact_publish_receipt`; callers
verify it with `verify_artifact_fetch_offline` or
`verify_artifact_publish_offline` against an explicitly supplied,
caller-pinned `ServiceTrustBundle`. The public-key path preserves the 3.6
exchange checks and records the selected verification-key and trust-bundle
IDs. See [public-key service identities](public-service-identities.md).
RBF-Safe 3.8 additionally permits a caller-pinned schema-2 predecessor to
authorize an exact successor and records those transitions in an
expected-head guarded local history. The transport adapter still owns
root/head distribution and retention.
RBF-Safe 3.9 additionally supports schema-3 multi-signer rotation and
portable current-head checkpoints. These remain local verification artifacts:
the transport adapter still owns authenticated root/checkpoint distribution
and selection of the newest accepted checkpoint.

## Transport adapter responsibilities

An adapter may use HTTPS, a message bus, object storage, an air-gapped copy
workflow, or another protocol. It is responsible for:

- endpoint allowlists, URI parsing, DNS and redirect policy, and SSRF defense;
- TLS validation and optional mutual TLS;
- authentication and authorization outside the transfer HMAC/signature;
- timeouts, retry and idempotency policy, rate limiting, and backpressure;
- streaming or buffering without exceeding the RBF-Safe byte cap;
- mapping remote errors without converting them into verified transfers; and
- keeping HMAC and signing keys out of command-line arguments and logs.

The library deliberately does not retry. A retry policy can otherwise turn
one logical publication into multiple service operations or conceal an
ambiguous acknowledgement.

## Evidence and operational use

A `VerifiedArtifactTransfer` states that exact catalog-bound bytes and the
complete declared exchange passed the selected integrity/authentication
checks. It does not parse the payload, replay its own persistence validator,
refresh a stale certificate, authorize reuse, or raise `EvidenceLevel`.
Consumers must load the bytes through the artifact-specific reader and apply
normal robot/scene compatibility checks.

The [SLSA verification guidance](https://slsa.dev/spec/v1.2/verifying-artifacts)
similarly separates cryptographic verification from checking expected
identities and parameters. The
[NIST DevSecOps reference model](https://pages.nist.gov/nccoe-devsecops/notational-reference-model.html)
places artifact repositories and signing/verification tools alongside, rather
than inside, application validation. These references inform the boundary;
RBF-Safe does not claim TUF, SLSA, or NIST conformance.

Threshold-signature cryptography, certificate chains, remote key discovery,
TLS, endpoints,
credentials, and concrete network clients remain outside the library. Public
roots are authorized only through caller pins; later schema-2 bundles may be
authorized by replaying signed successors from that root, while schema-3
bundles require their configured independent-signature/service quorum.
Accepting any root
solely because its self-declared hash is internally consistent is not
authentication, and local replay cannot detect whole-directory rollback
without a separately retained expected head or pinned signed-checkpoint ID.
