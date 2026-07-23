# Authenticated artifact attestations

RBF-Safe 3.3 adds `RBFSafe::trust`, a transport-neutral way to authenticate
the exact bytes referenced by a `SafetyMemory` artifact. It uses full-length
HMAC-SHA256 with caller-supplied shared keys, following the HMAC construction
in [RFC 2104](https://www.rfc-editor.org/info/rfc2104/). The implementation is
checked against the published HMAC-SHA256 vectors in
[RFC 4231](https://www.rfc-editor.org/info/rfc4231/).

## Workflow

The signing process supplies an immutable payload, registered memory artifact,
trusted service and key IDs, and a key obtained from outside RBF-Safe:

```cpp
auto attestation = attest_artifact_file(
    artifact, "artifacts/shelf-atlas", "factory-service", "rotation-7",
    hmac_key, 42, "application/vnd.rbfsafe.atlas");
if (!attestation)
    return attestation.error();

auto saved = save_artifact_attestation(attestation.value(),
                                       "artifacts/shelf-atlas.attestation.json");
```

The consumer selects a key using independently trusted configuration, not the
untrusted sidecar alone, and supplies the service and key IDs it expected:

```cpp
auto loaded = load_artifact_attestation("artifacts/shelf-atlas.attestation.json");
if (!loaded)
    return loaded.error();

auto verified = verify_artifact_file(
    artifact, "artifacts/shelf-atlas", loaded.value(),
    "factory-service", "rotation-7", hmac_key);
```

Verification binds the exact artifact ID, logical content digest, lifecycle
generation/state, service ID, key ID, media type, payload SHA-256 and byte
count, attestation sequence, and authentication algorithm. A lifecycle change
therefore requires a new attestation; an old active attestation fails against a
stale or quarantined memory record.

## Key contract

Keys are 32 to 4096 bytes and are never serialized by RBF-Safe. Applications
must obtain them from an operating-system credential facility, HSM, KMS, or
equivalent reviewed secret manager, restrict process access, rotate key IDs,
and erase application copies when practical. CLI verification accepts a key
file for automation, but production wrappers should prefer protected handles
or short-lived secret files with appropriate permissions.

HMAC is symmetric: every verifier holding the shared key can also create a
valid tag. It provides integrity and shared-key source authentication, not
public-key signatures, non-repudiation, signer identity, access control, or
authorization. The built-in implementation is not a claim of FIPS-validated
cryptographic-module status.

## Schema 1 sidecar

The sidecar is a bounded JSON file with format
`rbfsafe-artifact-attestation`, schema 1. Counts that may exceed binary64's
exact integer range are decimal strings. The deterministic attestation ID is a
SHA-256 of canonical unsigned fields; `authentication_tag` is HMAC-SHA256 over
a separate domain-prefixed canonical message.

`load_artifact_attestation` checks the schema, field bounds, enum values,
digests, and deterministic ID, but cannot authenticate a tag without a key.
Only `verify_artifact` or `verify_artifact_file` produces an authenticated
result. The CLI and native inspector therefore print `verified=false` when
they only parse metadata.

File helpers reject oversized payloads before allocation, read under a caller
budget, detect size changes during the read, and support cooperative
cancellation. Sidecar saving uses a same-directory temporary file, refuses
overwrite by default, and uses rename publication. Those guarantees assume a
reliable local filesystem.

## Safety boundary

Authentication proves that the shared-key holder approved specific bytes and
metadata. It does not prove that an Atlas or other payload is geometrically
correct, current for the live scene, safe to reuse, or executable. Consumers
must still load the artifact with its validating reader, check robot/scene and
memory identities, enforce lifecycle rules, and apply the runtime safety
model. RBF-Safe 3.3 does not implement TLS, network retrieval, service
discovery, secret storage, certificate authorities, encryption, distributed
consensus, or motor authorization.

Runnable examples are
[`examples/artifact_attestation_quickstart.cpp`](../examples/artifact_attestation_quickstart.cpp)
and
[`examples/artifact_attestation_quickstart.py`](../examples/artifact_attestation_quickstart.py).
`data/artifact_attestation_schema1` is a fixed interoperability fixture using
the public test-only key bytes `01 02 ... 20`; that key must never be used for
real artifacts.
