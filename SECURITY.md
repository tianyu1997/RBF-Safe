# Security and safety reporting

[简体中文](SECURITY.zh-CN.md) | English

## Supported versions

Security and correctness fixes are provided for the latest tagged 5.x minor
release. Users should reproduce a report against the current `main` branch
when practical.

## Private reporting

Do not open a public issue for parser vulnerabilities, checksum or identity
bypasses, memory-safety defects, or a suspected false-positive
`CertifiedRegion`/connectivity result. Use GitHub's private vulnerability
reporting page:

<https://github.com/tianyu1997/RBF-Safe/security/advisories/new>

Include, when available:

- RBF-Safe version or commit SHA and platform;
- robot and scene inputs, configuration-space box, and `BuildOptions`;
- the unexpected certificate, Atlas, or malformed input;
- for transfer issues, the redacted request/response IDs, lifecycle state,
  byte counts, service sequence, and authentication mode, but never secret
  keys;
- for public-key issues, the public key ID, pinned bundle ID, bundle sequence,
  key state/window/permissions, expected root/head IDs, rotation-record and
  authorization/authorization-set IDs, checkpoint ID and signer public-key
  IDs, and redacted signed metadata; never include an Ed25519 seed or private
  key;
- for authenticated occupancy issues, the publication/stream/publisher/
  trust-bundle/parent IDs, sequence, validity and evaluation ticks, payload
  digest/length, and a redacted or synthetic reproducer; never include the
  signing key;
- for occupancy-history issues, the schema, pinned root and externally
  retained expected-head publication IDs, conflicting record/publication IDs,
  relation result, resource limits, and synthetic manifest/record layout;
  never include signing keys or sensitive production occupancy;
- for coordinated-reservation issues, the retained agreement ID, protocol,
  round/parent, evaluation tick, deployment mapping, occupancy bundle/report
  IDs, participant trust/publication roots and heads, payload digest/length,
  and a synthetic agreement/history fixture; never include private keys or
  sensitive production trajectories;
- a minimal reproducer and whether the result is deterministic;
- expected impact and any known deployed use.

The maintainer will acknowledge a complete report as soon as practical,
coordinate validation and remediation privately, and credit reporters who
request attribution. No fixed response-time SLA is currently offered.

## Safety scope

RBF-Safe certificates are geometric software claims bound to recorded robot,
scene, algorithm, and parameter identities. They do not replace controller
limits, emergency stops, independent collision monitoring, calibration
checks, or application-specific risk assessment. See
[docs/core/safety-model.md](docs/core/safety-model.md).

An occupancy publication history protects only the retained directory and the
caller-supplied pins. Whole-directory rollback is detectable only when the
expected head is retained in a separate rollback domain. Fork auditing detects
only histories supplied to the audit; it is not a gossip network or global
consensus service. Successful replay and audit remain `Unknown`,
non-authorizing evidence.

A trust-rotating occupancy history authenticates each publication under the
exact historical service-trust bundle it names and verifies every intervening
single/quorum-authorized rotation. It still depends on a caller-retained trust
root plus newest trust head or checkpoint, and publication root plus newest
publication head. A valid older signed checkpoint is not automatically fresh;
a fully rolled-back directory and rolled-back caller anchors cannot be
detected locally. Dual-chain audit compares only supplied views and does not
provide peer discovery, gossip, consensus, or execution authority.

A coordinated reservation agreement proves unanimous exact-payload
publication only among the explicit histories supplied to replay. The caller
must retain the newest accepted agreement ID and all participant heads outside
the artifacts' rollback domain. A valid agreement does not prove peer receipt,
global newest state, clock synchronization, network consensus, controller
admission, reservation enforcement, or physical execution. Agreements remain
`Unknown` and non-authorizing.
