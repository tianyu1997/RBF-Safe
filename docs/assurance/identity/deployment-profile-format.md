# Reviewed deployment profiles

RBF-Safe 3.10 adds a deterministic governance artifact for assumptions that
geometric certificates cannot establish: which robot, controller, compute
platform, runtime build, timing envelope, monitor, transport behavior, and
artifact-authentication policy a deployment review accepted.

A reviewed deployment profile is not an execution permit. Loading, signature
verification, or a conformant assessment always remains
`EvidenceLevel::Unknown`, and `authorizes_execution()` is always false.

## Trust model

A profile binds all of the following exact identities:

- deployment ID;
- robot, controller, platform, and runtime-build SHA-256 digests;
- caller-pinned service-trust root;
- caller-retained signed checkpoint;
- checkpoint head bundle and sequence;
- immutable runtime constraints; and
- immutable review quorum and required roles.

Approvals use RFC 8032 Ed25519 keys in the exact checkpoint-head
`ServiceTrustBundle`. A key must be active, valid at the bound bundle sequence,
and `allow_publish=true`. The publish capability means the service may publish
a reviewed profile; it does not grant motion or controller authority.

The profile policy may require:

- a minimum number of unique signer key pairs;
- that the quorum come from at least that many distinct services; and
- at least one approval for each required `Safety`, `Controls`, `Operations`,
  or `Security` role.

Each signature binds the profile ID, signer service/key, role, algorithm, and
schema. The canonical approval set sorts approvals and rejects duplicate
signer pairs. A reviewer cannot satisfy two approvals by signing twice with
the same key.

`ReviewedDeploymentProfile::create` and `load` verify the caller-pinned
checkpoint against the complete trust history, require the profile to bind
that exact root/checkpoint/head/sequence, retrieve the exact head bundle, and
verify every approval and policy constraint. A self-consistent profile,
checkpoint, or history is never a trust anchor.

## Runtime constraints

`DeploymentRuntimeConstraints` records upper bounds for:

- observation age in nanoseconds;
- command latency in nanoseconds;
- control period in nanoseconds; and
- consecutive missed control cycles.

It can additionally require an active RBF-Safe runtime monitor, fail-closed
transport, and authenticated safety artifacts.

`ReviewedDeploymentProfile::assess(snapshot)` compares the complete runtime
identity and all constraints. The deterministic result is either
`Conformant` with no violations or `Nonconformant` with sorted violation
codes. Identity mismatches are ordinary nonconformance findings; malformed
snapshots remain `InvalidArgument`.

The caller supplies measured ages, latency, period, missed cycles, and Boolean
status. RBF-Safe does not read clocks, interrogate hardware, discover software
builds, configure a controller, or prove the truth of those observations.

## Schema 1

The portable UTF-8 JSON file has:

```text
format = "rbfsafe-reviewed-deployment-profile"
schema = 1
library_version
profile
approval_set
```

`profile` stores its deterministic ID, complete identity binding, runtime
constraints, and review policy. `approval_set` stores its deterministic ID and
the canonical Ed25519 approvals. Private material is never stored.

All 64-bit integers are decimal strings so JSON number precision cannot alter
identity. Enum values and 32-bit limits use exact integral JSON numbers.
Object keys are emitted in deterministic order.

The writer uses a same-directory temporary file, refuses overwrite by
default, and rejects symlink/non-regular destinations. Default publication is
one atomic rename; explicitly requested overwrite stages a same-directory
backup and restores it if publication fails. The reader checks the file type
and byte limit before parsing, then enforces approval and role limits, schema,
every nested identity, the caller-pinned checkpoint, trust-history replay,
key permissions and validity windows, signatures, and quorum.

Default load limits are:

| Limit | Default |
|---|---:|
| approvals | 100,000 |
| required roles | 32 |
| payload bytes | 4 MiB |

Unknown schema values return `IncompatibleFormat`. Malformed known data or an
incorrect deterministic identity returns `CorruptData`; exceeded bounds
return `ResourceLimit`; wrong trust anchors, bindings, or signatures return
`IdentityMismatch`.

There is no migration from calibration-lifecycle review text, safety-memory
deployment IDs, service-trust bundles, controller configuration, URDF/SRDF,
or arbitrary deployment manifests. Those formats do not carry the exact
identity, constraint, role, checkpoint, and approval contract required here.

## Explicit exclusions

Schema 1 does not provide:

- `RuntimeExecutable` evidence or actuation authorization;
- wall-clock freshness, expiry, leases, or revocation after the bound
  checkpoint;
- controller/hardware attestation or trusted boot;
- authenticated runtime observations or measurements;
- network trust discovery, TOFU, PKI, transparency logs, or secret storage;
- emergency-stop, independent collision monitor, or safety-PLC behavior;
- validation of payload, calibration, tool, environment, or controller
  semantics behind caller-supplied digests; or
- protection when an attacker rolls back the profile, history, checkpoint,
  and every caller-retained anchor together.

Deployment owners must retain the trusted root/checkpoint IDs outside the
artifact rollback domain, authenticate runtime observations independently,
and perform application-specific safety validation.
