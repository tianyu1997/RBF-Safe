# Migration map

The RapidBoxForest checkout is a read-only behavioral reference. RBF-Safe has
no source, build, runtime, or data-format dependency on it.

| Legacy area | v0.1 disposition | Rationale |
|---|---|---|
| `link_interval_envelope` interval/DH/AA-FK/LinkIAABB | Reimplemented against new value types | Conservative geometry core |
| CritSample, MC, GCPC, KDOP, SupportHull | Deferred | Multiple policies would enlarge the initial claim surface |
| LECT split and interval lookup concepts | Reimplemented as public `LectTree` | Stable path keys replace allocation IDs |
| LECT journal, prewarm modes, SBF adapter, old snapshots | Retired | Experiment-oriented layout is not a public contract |
| Safe box certification and query semantics | Selectively re-derived | Needed for SafeAtlas |
| Planning forest, grower, bridge, connector | Excluded | RBF-Safe v0.1 is not a planner |
| HiPaC, portal and OBB code | Re-derived in v0.4 | New OBB enclosure proofs, witness portals, and subject-bound certificates; no legacy planner code |
| Safe IK concepts and MoveIt bridge | Re-derived in v0.5 | New region-projected solver, Atlas route evidence, and current public MoveIt 2 plugin interfaces |
| Scene versions, invalidation, and repair concepts | Re-derived in v0.6 | New obstacle-ID deltas, subject-bound lineage, link-envelope dependencies, local LECT repair, and immutable version store |
| Remote artifact service and transfer audit | New in v3.6 | Transport-neutral exact-byte/lifecycle contract and request-bound authentication; no RapidBoxForest network, cache, or journal code migrated |
| Public-key service identities and trust bundles | New in v3.7 | Repository-local identity/rotation/offline-verification design plus unmodified, attributed Monocypher 4.0.2 Ed25519 primitive; no RapidBoxForest identity or crypto code migrated |
| Signed trust successors and local trust histories | New in v3.8 | Repository-local authorization, expected-head publication, bounded replay, and persistence design; no RapidBoxForest trust-history code migrated |
| Quorum trust rotation and signed head checkpoints | New in v3.9 | Repository-local canonical multi-signature policy, authorization-set/history schema, bounded checkpoint persistence, and caller-anchor design; no RapidBoxForest trust or checkpoint code migrated |
| Reviewed deployment profiles | New in v3.10 | Repository-local deterministic deployment/runtime constraints, role-aware Ed25519 approval policy, checkpoint binding, bounded persistence, and conformance reports; no RapidBoxForest deployment-governance code migrated |
| Bounded execution sessions | New in v3.11 | Repository-local exact Atlas/trajectory/profile/endpoint/approval/runtime binding, bounded persistence, and closed-window command evidence; no RapidBoxForest controller or hardware-execution code migrated |
| Revocation-aware execution ledger | New in v3.12 | Repository-local expected-head authorization/completion history, current-checkpoint reviewer revalidation, terminal event records, bounded append-only persistence, and offline audit; no RapidBoxForest controller, ledger, hardware, or revocation-service code migrated |
| Deployment and runtime transparency | New in v3.13 | Repository-local deployment anchor, independent source-attestation quorum, deterministic Merkle log, signed checkpoint, inclusion/prefix-consistency verification, bounded expected-head persistence, and audit; no RapidBoxForest transparency, observer, network, hardware-attestation, or audit-log code migrated |
| Witnessed transparency and gossip | New in v3.14 | Repository-local compact Merkle proof, independent checkpoint quorum, authenticated gossip chain, proof-graph split-view audit, and bounded append-only archive; no RapidBoxForest or third-party certificate-transparency, network, hardware-root, time, or consensus code migrated |
| Experiments, manuscript and generated outputs | Excluded | Remain in RapidBoxForest |
