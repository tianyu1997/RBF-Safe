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
