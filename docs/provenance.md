# Provenance

RBF-Safe is a new MIT-licensed implementation informed by the algorithms and
tests in RapidBoxForest. No legacy source file is copied unchanged.

The affine-arithmetic DH propagation and paired link-envelope algorithms are
restructured from the authors' MIT-licensed RapidBoxForest implementation into
dimension-safe value-based APIs. LECT retains the deterministic binary
partition concept but replaces legacy storage, numeric identities, journals,
and planner adapters. All other v0.1 implementation is new.

`data/golden/legacy_geometry_v1.json` was generated on 2026-07-21 from a
read-only RapidBoxForest `link_interval_envelope` reference build. It records
point-FK and IFK-AA endpoint bounds for the legacy 2-DOF, IIWA14, and UR5
models. Runtime tests consume only the checked-in fixture and never access the
legacy checkout or its binaries. The UR5 legacy file contained a seventh
unused radius without a tool transform; RBF-Safe intentionally records only
the six represented links.

The v0.3 OMPL adapter is a new implementation against OMPL's public extension
interfaces. RapidBoxForest's experiment-oriented OMPL helpers were consulted
for behavioral context only; their planner wrappers, private-member access,
deterministic subclasses, collision checker, and Python orchestration code are
not copied or migrated.

The v0.4 corridor layer is also a new implementation. Legacy OBB path-cover,
portal overlay, and HiPaC query-bridge files were reviewed only to identify
behavioral concepts and failure modes. RBF-Safe does not copy their Eigen
types, GJK/zonotope implementation, planner-private access, adaptive sweep
configuration, diagnostics, or promotion logic. Its OBBs use standard
containers, conservative AABB-enclosure proofs, bounded recursive covering,
and new subject-bound certificate identities.
