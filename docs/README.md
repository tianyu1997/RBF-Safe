# Documentation

RBF-Safe keeps short user guides separate from exact format and trust contracts. Start with the safety model and getting-started guide; use the format pages when implementing storage or interoperability.

[简体中文精选文档](zh-CN/README.md)

The directory layout follows the public module boundaries:

```text
core/          entry points, architecture, safety, and installation
envelope/      workspace-envelope semantics
geometry/      robot kinematics and conservative geometry
atlas/         Atlas storage, trajectory audit, and dynamic updates
applications/  planning/optimization and policy safety
assurance/     memory, coordination, identity, execution, and audit
maintenance/   compatibility, releases, provenance, and roadmap
zh-CN/         curated Chinese guides using the same grouping
```

## Start here

- [Installation](core/installation.md)
- [Getting started](core/getting-started.md)
- [Safety model](core/safety-model.md)
- [Architecture](core/architecture.md)
- [API overview](core/api.md)
- [Input formats](core/input-formats.md)
- [Workspace envelopes](envelope/workspace-envelopes.md)
- [Kinematics and geometric Jacobian](geometry/kinematics.md)
- [Vision](core/vision.md)

## Certificates, regions, and planning

- [Atlas format](atlas/atlas-format.md)
- [OBB corridors, Portals, and HiPaC](applications/planning/corridors.md)
- [Corridor format](applications/planning/corridor-format.md)
- [Unified region database](applications/planning/region-database.md)
- [Region database format](applications/planning/region-database-format.md)
- [Trajectory auditor](atlas/trajectory-auditor.md)
- [Dynamic updates and Atlas versions](atlas/dynamic-updates.md)
- [Safe IK](applications/planning/safe-ik.md)
- [Certified planning consumers](applications/planning/planning-consumers.md)
- [OMPL adapter](applications/planning/ompl-adapter.md)
- [MoveIt 2 integration](applications/planning/moveit2.md)
- [Optimization adapters](applications/planning/optimization.md)
- [Runtime action shield](applications/planning/runtime-shield.md)

## Policy safety and reusable memory

- [Learning-policy safety](applications/policy/policy-safety.md)
- [Policy feedback format](applications/policy/policy-feedback-format.md)
- [Policy calibration](applications/policy/policy-calibration.md)
- [Calibration lifecycle](applications/policy/policy-calibration-lifecycle.md)
- [Persistent safety memory](assurance/memory/safety-memory.md)
- [Safety-memory format](assurance/memory/safety-memory-format.md)
- [Transactional safety-memory store](assurance/memory/safety-memory-store.md)
- [Fleet-schedule archives](assurance/memory/fleet-schedule-archive.md)

## Occupancy and coordination

- [Continuous fleet occupancy](assurance/coordination/continuous-fleet-occupancy.md)
- [Continuous fleet-occupancy format](assurance/coordination/continuous-fleet-occupancy-format.md)
- [Continuous moving obstacles](assurance/coordination/continuous-moving-obstacles.md)
- [Robot-scene occupancy format](assurance/coordination/continuous-robot-scene-occupancy-format.md)
- [Authenticated occupancy publication](assurance/coordination/authenticated-occupancy-publication.md)
- [Authenticated publication format](assurance/coordination/authenticated-occupancy-publication-format.md)
- [Occupancy publication history](assurance/coordination/occupancy-publication-history.md)
- [Publication-history format](assurance/coordination/occupancy-publication-history-format.md)
- [Trust-rotating publication history](assurance/coordination/rotating-occupancy-publication-history.md)
- [Trust-rotating history format](assurance/coordination/rotating-occupancy-publication-history-format.md)
- [Coordinated reservation agreements](assurance/coordination/coordinated-reservation-agreements.md)
- [Reservation-agreement format](assurance/coordination/coordinated-reservation-agreement-format.md)

## Identity, deployment, and audit

- [Artifact attestations](assurance/identity/artifact-attestation.md)
- [Remote artifact service](assurance/identity/remote-artifact-service.md)
- [Artifact-transfer journal](assurance/identity/artifact-transfer-journal-format.md)
- [Public service identities](assurance/identity/public-service-identities.md)
- [Service trust bundles](assurance/identity/service-trust-bundle-format.md)
- [Service trust history](assurance/identity/service-trust-history-format.md)
- [Service trust checkpoints](assurance/identity/service-trust-checkpoint-format.md)
- [Reviewed deployment profiles](assurance/identity/deployment-profile-format.md)
- [Bounded execution sessions](assurance/identity/bounded-execution-session-format.md)
- [Execution ledger](assurance/identity/execution-ledger-format.md)
- [Transparency log](assurance/identity/transparency-log-format.md)
- [Witnessed transparency](assurance/identity/witnessed-transparency.md)
- [Verifiable provenance](assurance/identity/verifiable-provenance.md)
- [Provenance-bundle format](assurance/identity/verifiable-provenance-format.md)

## Maintenance and releases

- [API stability](maintenance/api-stability.md)
- [Versioning and compatibility](maintenance/versioning.md)
- [Schema migrations](maintenance/schema-migrations.md)
- [Release fixtures and benchmark](maintenance/release-fixtures.md)
- [Release procedure](maintenance/releasing.md)
- [Migration map](maintenance/migration-map.md)
- [Provenance and reused code](maintenance/provenance.md)
- [Project-scope matrix](maintenance/project-scope-matrix.md)
- [Roadmap](maintenance/roadmap.md)

Detailed format documents are normative for persisted fields and compatibility. A valid file, signature, log, or planner result does not imply a stronger evidence level than the corresponding safety contract states.
