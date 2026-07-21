# Security and safety reporting

## Supported versions

Until v1.0, security and correctness fixes are provided for the latest tagged
minor release. Users should reproduce a report against the current `main`
branch when practical.

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
