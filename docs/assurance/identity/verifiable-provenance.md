# Verifiable provenance and external time

RBF-Safe 3.15 adds an audit-only boundary for normalized hardware-key
attestation statements and signed external time assertions. The public CMake
target is `RBFSafe::provenance`; the C++ header is
`<rbfsafe/modules/assurance.h>`, and the stable high-level Python values are exported
from `rbfsafe`.

This layer answers two deliberately narrow questions:

1. Does an append-only sequence of authenticated statements bind an expected
   public service key to caller-approved attesters, adapter versions, vendors,
   and usages?
2. Do caller-approved external time sources place the same subject inside a
   caller-supplied freshness window?

It does not prove that a device is genuine, that an adapter correctly parsed a
vendor blob, that UTC is available, that a local clock is trustworthy, or that
a robot executed a command. Every statement, bundle, and report has
`EvidenceLevel::Unknown` and `authorizes_execution() == false`.

## Hardware adapter boundary

Vendor TPM, TEE, HSM, secure-element, and cloud-attestation formats remain
outside the library. An adapter normalizes its reviewed result into
`HardwareKeyAttestationInput`. The signed statement binds:

- the exact subject service ID, key ID, and Ed25519 public-key bytes;
- an adapter ID, adapter version, and statement media type;
- vendor and product identifiers;
- SHA-256 digests of the vendor evidence and challenge nonce;
- a sorted set of explicit `HardwareAttestationScope` usages;
- the exact caller-pinned trust bundle and active publication-capable
  attester key;
- a sequence number and parent statement ID.

`HardwareKeyProvenancePolicy` accepts nothing implicitly. It pins every
allowed adapter triple, attestation authority, vendor ID, required scope,
minimum statement count, distinct-attester rule, and chain bound. Replay
verifies every signature and requires a contiguous `1..N` parent chain for one
exact `ServicePublicKey`.

RBF-Safe does not ship a default vendor adapter or vendor trust root. Updating
an adapter, statement format, authority, vendor, or required usage creates a
different deterministic policy ID and requires an explicit new statement
chain.

## External signed time

`ExternalTimeAssertion` binds an exact SHA-256 subject ID to:

- an explicit clock namespace such as `unix-utc-ns`;
- an unsigned nanosecond value and uncertainty radius;
- one exact trust bundle and active publication-capable source key;
- a per-source sequence and parent assertion ID.

`ExternalTimeFreshnessPolicy` pins the clock namespace, allowed source
service/key pairs, minimum source count, distinct-service rule, maximum
uncertainty, maximum age, maximum future skew, and assertion budget.

The caller supplies `evaluated_at_ns`; the freshness evaluator never reads a
local clock.
For each source, replay requires a contiguous, nondecreasing assertion chain
and selects the newest assertion. Each newest assertion defines the closed
interval

```text
[max(0, asserted - uncertainty),
 min(UINT64_MAX, asserted + uncertainty)].
```

The evaluator intersects all selected intervals and reports:

- `FRESH`: the nonempty intersection is within the age/future policy;
- `STALE`: even its latest possible time is too old;
- `FUTURE`: even its earliest possible time exceeds the allowed future skew;
- `INCONSISTENT`: the source intervals have no common time;
- `INCOMPLETE`: the explicit source quorum is not met.

All additions use saturating unsigned arithmetic. A `FRESH` result means only
that signed inputs satisfy this policy at the caller-provided evaluation
value.

## Bundle replay

`VerifiableProvenanceBundle` combines one exact subject key, hardware policy,
freshness policy, complete hardware statement chain, and complete time-source
chains. `replay_verifiable_provenance` returns both reports. Its convenience
`ready()` value is true only for `SATISFIED + FRESH`; it remains
non-authorizing `Unknown` metadata.

Bundles use a bounded, checksummed schema-1 JSON file. Saving stages and
reopens a sibling temporary file before atomic publication, rejects overwrite
by default, and rejects indirect destinations. Loading rejects symlinks,
unknown schema, excessive bytes/counts, cancellation, malformed values,
invalid nested identities, and checksum mismatch.

Inspect a fixed bundle with the Python-package inspector, caller-retained trust
pins, and an explicit time:

```powershell
rbfsafe-inspect data/provenance_bundle_schema1/provenance.json `
  --trust-history data/provenance_bundle_schema1/trust-history `
  --trust-checkpoint data/provenance_bundle_schema1/checkpoint.json `
  --expected-trust-root af93de1f517b91d348732b72bd08becb9411ee08c1151f5f66da0291740a2865 `
  --expected-trust-checkpoint 6aa7aa5c91644205fa67e0caf6858a7f11ba0bb03aec2c548ba941624984137b `
  --evaluated-at-ns 1000000
```

The native CMake-installed inspector uses positional arguments:

```powershell
rbfsafe-inspect data/provenance_bundle_schema1/provenance.json `
  data/provenance_bundle_schema1/trust-history `
  af93de1f517b91d348732b72bd08becb9411ee08c1151f5f66da0291740a2865 `
  data/provenance_bundle_schema1/checkpoint.json `
  6aa7aa5c91644205fa67e0caf6858a7f11ba0bb03aec2c548ba941624984137b `
  1000000
```

## Integration rule

Applications may use provenance reports as an additional deployment admission
input, but that policy belongs outside RBF-Safe 3.15. Never convert
`ready()`, `FRESH`, or `SATISFIED` into `CertifiedRegion`,
`CertifiedConnectivity`, or `RuntimeExecutable`. Exact command authorization
continues to come only from the bounded execution-session API.
