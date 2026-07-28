# Security and safety reporting

## Supported versions

Security and correctness fixes are provided for the latest tagged 4.x minor
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
[docs/safety-model.md](docs/safety-model.md).
