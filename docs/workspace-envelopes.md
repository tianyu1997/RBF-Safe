# Workspace envelopes

RBF-Safe represents static obstacle geometry with `WorkspaceEnvelope`, a
tagged value containing one of four conservative three-dimensional shapes:

- `WorkspaceAabb`: axis-aligned lower and upper bounds;
- `WorkspaceObb`: center, right-handed row-major orthonormal basis, and three
  half-widths;
- `WorkspaceKdop`: paired projection bounds along normalized directions;
- `WorkspaceSupportHull`: the convex hull of support points, optionally
  Minkowski-expanded by a sphere radius.

`WorkspaceSupportHull` can represent a capsule without a mesh by supplying its
two segment endpoints and its radius. `WorkspaceKdop` provides standard
6-, 14-, 18-, and 26-DOP direction sets, as well as caller-supplied direction
sets.

## Conservative collision contract

All envelope types expose support points, an enclosing AABB, conservative
overlap testing, and a clearance lower bound. A non-overlap result is issued
only after finding and verifying a separating direction from the two support
mappings. If the bounded search cannot prove separation, the result is
conservatively treated as a possible overlap. This preserves the certificate
rule that `CertifiedFree` never depends on an unverified negative collision
answer. Exact AABB pairs and shapes with disjoint enclosing AABBs use direct
fast paths before the support-mapping search.

## Static scenes

`SceneObstacle::bounds` is a `WorkspaceEnvelope`. Existing C++ and Python AABB
construction remains accepted. Scene JSON schema 1 remains the legacy AABB
format. Schema 2 adds a required `type` to every obstacle and persists all four
representations. `SceneSnapshot::from_json` accepts both schemas; canonical
output stays at schema 1 when every obstacle is an AABB and uses schema 2 when
any typed obstacle is present.

Scene deltas follow the same rule. Atlas transitions containing only AABBs
retain schema 1 identities, while transitions involving OBB, k-DOP, or support
hull geometry use scene-delta schema 2.

## Endpoint AABB sources

`compute_endpoint_aabbs` supports two endpoint-box generators selected by
`EnvelopeOptions::endpoint_aabb_source`:

- `IfkAa` propagates affine arithmetic over the complete C-space box and
  returns certified conservative endpoint bounds;
- `CritSample` deterministically enumerates each joint's lower and upper
  bounds plus every interior `k*pi/2` value. Intervals narrower than `0.01`
  joint units collapse to their midpoint, and large candidate products use the
  RapidBoxForest 8192-combination reduction rule.

CritSample is deliberately reported as `certified == false`: sampled extrema
can miss interior extrema. It is useful for diagnostics, regression studies,
and envelope-tightness comparisons, but it cannot support `CertifiedRegion`
evidence. `EndpointAabbResult` reports the source, safety flag, paired endpoint
AABBs, and evaluated configuration count.

The CritSample implementation precomputes every candidate modified-DH matrix.
Its odometer traversal then rebuilds only the transform-prefix suffix affected
by the changed joint, avoiding repeated model validation, trigonometric work,
and per-configuration FK allocations.

## Per-link envelopes

The legacy `compute_ifk_aa_link_envelope` API still returns AABBs for binary
Atlas and dependency compatibility. The generalized API returns typed shapes:

```cpp
rbfsafe::EnvelopeOptions options;
options.workspace_envelope_type = rbfsafe::WorkspaceEnvelopeType::Kdop;
options.kdop_k = 26;

auto links = rbfsafe::compute_workspace_link_envelope(
    robot, robot.configuration_domain(), options);
```

The selected endpoint source first obtains proximal and distal endpoint boxes.
The selected workspace representation then contains both boxes and the
configured link radius:

- AABB takes their axis-aligned union;
- OBB uses a deterministic link-aligned orthonormal basis and outward-rounded
  projected extents;
- k-DOP projects all endpoint-box corners and expands every slab by the radius;
- SupportHull takes the convex hull of all endpoint-box corners and applies a
  spherical radius.

The explicitly named `compute_ifk_aa_link_envelope` and
`compute_ifk_aa_workspace_link_envelope` compatibility APIs always force the
certified IFK-AA source. `IfkAaWorkspaceEnvelopeValidator` likewise validates
regions only with IFK-AA and the selected shape.
It stores each typed link's enclosing AABB in `RegionValidation::envelope`, so
existing Atlas persistence and conservative scene-delta invalidation remain
compatible.

## Python

The Python module exposes the same value types plus `compute_endpoint_aabbs`,
`compute_workspace_link_envelope`, `compute_ifk_aa_workspace_link_envelope`, and
`IfkAaWorkspaceEnvelopeValidator`. For example:

```python
options = rbfsafe.EnvelopeOptions()
options.endpoint_aabb_source = rbfsafe.EndpointAabbSource.CRIT_SAMPLE
options.workspace_envelope_type = rbfsafe.WorkspaceEnvelopeType.SUPPORT_HULL
links = rbfsafe.compute_workspace_link_envelope(robot, domain, options)
assert not links.endpoint_bounds_certified
```
