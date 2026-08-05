# Documentation

RBF-Safe keeps short user guides separate from exact format and trust contracts. Start with the safety model and getting-started guide; use the format pages when implementing storage or interoperability.

[简体中文精选文档](zh-CN/README.md)

## Start here

- [Installation](installation.md)
- [Getting started](getting-started.md)
- [Safety model](safety-model.md)
- [Architecture](architecture.md)
- [API overview](api.md)
- [Input formats](input-formats.md)
- [Workspace envelopes](workspace-envelopes.md)
- [Kinematics and geometric Jacobian](kinematics.md)
- [Vision](vision.md)

## Certificates, regions, and planning

- [Atlas format](atlas-format.md)
- [OBB corridors, Portals, and HiPaC](corridors.md)
- [Corridor format](corridor-format.md)
- [Unified region database](region-database.md)
- [Region database format](region-database-format.md)
- [Trajectory auditor](trajectory-auditor.md)
- [Dynamic updates and Atlas versions](dynamic-updates.md)
- [Safe IK](safe-ik.md)
- [Certified planning consumers](planning-consumers.md)
- [OMPL adapter](ompl-adapter.md)
- [MoveIt 2 integration](moveit2.md)
- [Optimization adapters](optimization.md)
- [Runtime action shield](runtime-shield.md)

## Policy safety and reusable memory

- [Learning-policy safety](policy-safety.md)
- [Policy feedback format](policy-feedback-format.md)
- [Policy calibration](policy-calibration.md)
- [Calibration lifecycle](policy-calibration-lifecycle.md)
- [Persistent safety memory](safety-memory.md)
- [Safety-memory format](safety-memory-format.md)
- [Transactional safety-memory store](safety-memory-store.md)
- [Fleet-schedule archives](fleet-schedule-archive.md)

## Occupancy and coordination

- [Continuous fleet occupancy](continuous-fleet-occupancy.md)
- [Continuous fleet-occupancy format](continuous-fleet-occupancy-format.md)
- [Continuous moving obstacles](continuous-moving-obstacles.md)
- [Robot-scene occupancy format](continuous-robot-scene-occupancy-format.md)
- [Authenticated occupancy publication](authenticated-occupancy-publication.md)
- [Authenticated publication format](authenticated-occupancy-publication-format.md)
- [Occupancy publication history](occupancy-publication-history.md)
- [Publication-history format](occupancy-publication-history-format.md)
- [Trust-rotating publication history](rotating-occupancy-publication-history.md)
- [Trust-rotating history format](rotating-occupancy-publication-history-format.md)
- [Coordinated reservation agreements](coordinated-reservation-agreements.md)
- [Reservation-agreement format](coordinated-reservation-agreement-format.md)

## Identity, deployment, and audit

- [Artifact attestations](artifact-attestation.md)
- [Remote artifact service](remote-artifact-service.md)
- [Artifact-transfer journal](artifact-transfer-journal-format.md)
- [Public service identities](public-service-identities.md)
- [Service trust bundles](service-trust-bundle-format.md)
- [Service trust history](service-trust-history-format.md)
- [Service trust checkpoints](service-trust-checkpoint-format.md)
- [Reviewed deployment profiles](deployment-profile-format.md)
- [Bounded execution sessions](bounded-execution-session-format.md)
- [Execution ledger](execution-ledger-format.md)
- [Transparency log](transparency-log-format.md)
- [Witnessed transparency](witnessed-transparency.md)
- [Verifiable provenance](verifiable-provenance.md)
- [Provenance-bundle format](verifiable-provenance-format.md)

## Maintenance and releases

- [API stability](api-stability.md)
- [Versioning and compatibility](versioning.md)
- [Schema migrations](schema-migrations.md)
- [Release fixtures and benchmark](release-fixtures.md)
- [Release procedure](releasing.md)
- [Migration map](migration-map.md)
- [Provenance and reused code](provenance.md)
- [Project-scope matrix](project-scope-matrix.md)
- [Roadmap](roadmap.md)

Detailed format documents are normative for persisted fields and compatibility. A valid file, signature, log, or planner result does not imply a stronger evidence level than the corresponding safety contract states.
