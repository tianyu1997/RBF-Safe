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
| HiPaC, portal and OBB code | Deferred to v0.4 | Requires a separate certificate design |
| Experiments, manuscript and generated outputs | Excluded | Remain in RapidBoxForest |
